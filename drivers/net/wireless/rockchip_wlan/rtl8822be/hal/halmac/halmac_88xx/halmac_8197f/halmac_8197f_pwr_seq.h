#ifndef HALMAC_POWER_SEQUENCE_8197F
#define HALMAC_POWER_SEQUENCE_8197F

#include "../../halmac_pwr_seq_cmd.h"

/*
 *      There are 6 HW Power States:
 *      0: POFF--Power Off
 *      1: PDN--Power Down
 *      2: CARDEMU--Card Emulation
 *      3: ACT--Active Mode
 *      4: LPS--Low Power State
 *      5: SUS--Suspend
 *
 *      The transition from different states are defined below
 *      TRANS_CARDEMU_TO_ACT
 *      TRANS_ACT_TO_CARDEMU
 *      TRANS_CARDEMU_TO_SUS
 *      TRANS_SUS_TO_CARDEMU
 *      TRANS_CARDEMU_TO_PDN
 *      TRANS_ACT_TO_LPS
 *      TRANS_LPS_TO_ACT
 *
 *      TRANS_END
 */

#define HALMAC_8197f_TRANS_CARDEMU_TO_ACT_STEPS 15
#define HALMAC_8197f_TRANS_ACT_TO_CARDEMU_STEPS 15
#define HALMAC_8197f_TRANS_CARDEMU_TO_SUS_STEPS 15
#define HALMAC_8197f_TRANS_SUS_TO_CARDEMU_STEPS 15
#define HALMAC_8197f_TRANS_CARDEMU_TO_PDN_STEPS 15
#define HALMAC_8197f_TRANS_PDN_TO_CARDEMU_STEPS 15
#define HALMAC_8197f_TRANS_ACT_TO_LPS_STEPS             20
#define HALMAC_8197f_TRANS_LPS_TO_ACT_STEPS             15
#define HALMAC_8197f_TRANS_END_STEPS                    1


#define RTL8197f_TRANS_CARDEMU_TO_ACT                                                                                                           \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(2), 0 },                  /* disable SW LPS 0x04[10]=0*/      \
	{ 0x0006, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_POLLING, BIT(1), BIT(1) },           /* wait till 0x04[17] = 1    power ready*/   \
	{ 0x0006, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(0), BIT(0) },             /* release WLON reset  0x04[16]=1*/    \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(7), 0 },                  /* disable HWPDN 0x04[15]=0*/       \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, (BIT(4) | BIT(3)), 0 },       /* disable WL suspend*/    \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(0), BIT(0) },             /* polling until return 0*/    \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_POLLING, BIT(0), 0 },                /**/      \
	{ 0x0026, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(4), BIT(4) },             /*MAC CLK = 40M*/      \
	{ 0x0073, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(2), BIT(2) },             /**/

#define RTL8197f_TRANS_ACT_TO_CARDEMU                                                                                                   \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x001F, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0 },            /*0x1F[7:0] = 0 turn off RF*/ \
	{ 0x004E, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(7), 0 },          /*0x4C[23] = 0x4E[7] = 0, switch DPDT_SEL_P output from register 0x65[2] */ \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(1), BIT(1) },     /*0x04[9] = 1 turn off MAC by HW state machine*/      \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_POLLING, BIT(1), 0 },        /*wait till 0x04[9] = 0 polling until return 0 to disable*/

#define RTL8197f_TRANS_CARDEMU_TO_SUS                                                                                                   \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_PCI_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(4) | BIT(3), (BIT(4) | BIT(3)) },                 /*0x04[12:11] = 2b'11 enable WL suspend for PCIe*/    \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_USB_MSK | HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) }, /*0x04[12:11] = 2b'01 enable WL suspend*/     \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_PCI_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) | BIT(4) },                   /*0x04[12:11] = 2b'11 enable WL suspend for PCIe*/      \
	{ 0x0086, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_WRITE, BIT(0), BIT(0) },                                   /*Set SDIO suspend local register*/ \
	{ 0x0086, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_POLLING, BIT(1), 0 },                                      /*wait power state to suspend*/

#define RTL8197f_TRANS_SUS_TO_CARDEMU                                                                                                   \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x0086, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_WRITE, BIT(0), 0 },        /*Set SDIO suspend local register*/      \
	{ 0x0086, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_POLLING, BIT(1), BIT(1) }, /*wait power state to suspend*/ \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(3) | BIT(4), 0 }, /*0x04[12:11] = 2b'01enable WL suspend*/

#define RTL8197f_TRANS_CARDEMU_TO_CARDDIS                                                                                                       \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x0007, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0x20 },                                        /*0x07=0x20 , SOP option to disable BG/MB*/      \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_USB_MSK | HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(3) | BIT(4), BIT(3) }, /*0x04[12:11] = 2b'01 enable WL suspend*/     \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_PCI_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(2), BIT(2) },                                     /*0x04[10] = 1, enable SW LPS*/       \
	{ 0x0086, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_WRITE, BIT(0), BIT(0) },                                   /*Set SDIO suspend local register*/ \
	{ 0x0086, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_POLLING, BIT(1), 0 },                                      /*wait power state to suspend*/

#define RTL8197f_TRANS_CARDDIS_TO_CARDEMU                                                                                                       \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x0086, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_WRITE, BIT(0), 0 },        /*Set SDIO suspend local register*/      \
	{ 0x0086, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_POLLING, BIT(1), BIT(1) }, /*wait power state to suspend*/ \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(3) | BIT(4), 0 }, /*0x04[12:11] = 2b'01enable WL suspend*/ \
	{ 0x0301, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_PCI_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0 },            /*PCIe DMA start*/


#define RTL8197f_TRANS_CARDEMU_TO_PDN                                                                                           \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x0006, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(0), 0 },          /* 0x04[16] = 0*/ \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(7), BIT(7) },     /* 0x04[15] = 1*/

#define RTL8197f_TRANS_PDN_TO_CARDEMU                                                                                           \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x0005, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(7), 0 },/* 0x04[15] = 0*/

#define RTL8197f_TRANS_ACT_TO_LPS                                                                                                               \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0x1138, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, BIT(0) | BIT(1) },      /*set RPWM IMR*/  \
	{ 0x0194, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(0), BIT(0) },             /* enable 32K CLK*/ \
	/*{0x0093, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK,HALMAC_PWR_BASEADDR_MAC,HALMAC_PWR_CMD_WRITE, 0xff, 0x42}, *//* LPS Option WL partial ON*/ \
	{ 0x0093, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xff, 0x40 },                 /* LPS Option WL partial ON disable*/ \
	{ 0x0522, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0xFF },                 /*Tx Pause*/       \
	{ 0x05F8, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_POLLING, 0xFF, 0 },                  /*Should be zero if no packet is transmitting*/     \
	{ 0x05F9, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_POLLING, 0xFF, 0 },                  /*Should be zero if no packet is transmitting*/     \
	{ 0x05FA, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_POLLING, 0xFF, 0 },                  /*Should be zero if no packet is transmitting*/     \
	{ 0x0002, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(0), 0 },                  /*CCK and OFDM are disabled,and clock are gated*/   \
	{ 0x0002, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_DELAY, 0, HALMAC_PWRSEQ_DELAY_US },  /*Delay 1us*/       \
	{ 0x0002, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(1), 0 },                  /*Whole BB is reset*/       \
	{ 0x0100, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0x3F },                 /*Reset MAC TRX*/  \
	{ 0x0101, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(1), 0 },                  /*check if removed later*/  \
	{ 0x0553, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(5), BIT(5) },             /*Respond TxOK to scheduler*/  \
	{ 0x0029, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(6) | BIT(7), 0x00 },      /*gated BB clock*/        \
	{ 0x0008, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(4), BIT(4) },             /* switch TSF clock to 32K*/   \
	{ 0x0090, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(0), BIT(0) },             /* enable WL_LPS_EN*/


#define RTL8197f_TRANS_LPS_TO_ACT                                                                                                                       \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	/*{0x0080, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK,HALMAC_PWR_BASEADDR_SDIO,HALMAC_PWR_CMD_WRITE, 0xFF, 0x84}, *//*SDIO RPWM*/ \
	{ 0x0080, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_WRITE, BIT(7), BIT(7) },                   /*SDIO RPWM*/ \
	{ 0x0080, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_SDIO_MSK, HALMAC_PWR_BASEADDR_SDIO, HALMAC_PWR_CMD_WRITE, BIT(7), 0 },                        /*SDIO RPWM*/ \
	{ 0xFE58, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_USB_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0x84 },                         /*USB RPWM*/ \
	{ 0x0361, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_PCI_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0x84 },                         /*PCIe RPWM*/ \
	{ 0x0002, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_DELAY, 0, HALMAC_PWRSEQ_DELAY_MS },          /*Delay*/ \
	{ 0x0008, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(4), 0 },                          /*.	0x08[4] = 0		 switch TSF to 40M*/ \
	{ 0x0002, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_DELAY, 0, HALMAC_PWRSEQ_DELAY_MS },          /*Delay*/ \
	{ 0x0109, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_POLLING, BIT(7), 0 },                        /*Polling 0x109[7]=0  TSF in 40M*/ \
	{ 0x0029, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(6) | BIT(7), BIT(6) | BIT(7) },   /*.	0x29[7:6] = 2b'00	 enable BB clock*/ \
	{ 0x0101, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(1), BIT(1) },                     /*.	0x101[1] = 1*/ \
	{ 0x0100, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0xFF },                         /*.	0x100[7:0] = 0xFF	 enable WMAC TRX*/ \
	{ 0x0002, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, BIT(1) | BIT(0), BIT(1) | BIT(0) },   /*.	0x02[1:0] = 2b'11	 enable BB macro*/ \
	{ 0x0522, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0 },                            /*.	0x522 = 0*/ \
	{ 0x113C, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, HALMAC_PWR_BASEADDR_MAC, HALMAC_PWR_CMD_WRITE, 0xFF, 0 },                            /*clear RPWM INT*/

#define RTL8197f_TRANS_END                                                                                                                      \
	/* format */                                                                                                                            \
	/* { offset, cut_msk, fab_msk|interface_msk, base|cmd, msk, value }, // comments here*/                                                         \
	{ 0xFFFF, HALMAC_PWR_CUT_ALL_MSK, HALMAC_PWR_FAB_ALL_MSK, HALMAC_PWR_INTF_ALL_MSK, 0, HALMAC_PWR_CMD_END, 0, 0 },


extern HALMAC_WLAN_PWR_CFG halmac_8197f_power_on_flow[HALMAC_8197f_TRANS_CARDEMU_TO_ACT_STEPS + HALMAC_8197f_TRANS_END_STEPS];
extern HALMAC_WLAN_PWR_CFG halmac_8197f_radio_off_flow[HALMAC_8197f_TRANS_ACT_TO_CARDEMU_STEPS + HALMAC_8197f_TRANS_END_STEPS];
extern HALMAC_WLAN_PWR_CFG halmac_8197f_card_disable_flow[HALMAC_8197f_TRANS_ACT_TO_CARDEMU_STEPS + HALMAC_8197f_TRANS_CARDEMU_TO_PDN_STEPS + HALMAC_8197f_TRANS_END_STEPS];
extern HALMAC_WLAN_PWR_CFG halmac_8197f_card_enable_flow[HALMAC_8197f_TRANS_ACT_TO_CARDEMU_STEPS + HALMAC_8197f_TRANS_CARDEMU_TO_PDN_STEPS + HALMAC_8197f_TRANS_END_STEPS];
extern HALMAC_WLAN_PWR_CFG halmac_8197f_suspend_flow[HALMAC_8197f_TRANS_ACT_TO_CARDEMU_STEPS + HALMAC_8197f_TRANS_CARDEMU_TO_SUS_STEPS + HALMAC_8197f_TRANS_END_STEPS];
extern HALMAC_WLAN_PWR_CFG halmac_8197f_resume_flow[HALMAC_8197f_TRANS_ACT_TO_CARDEMU_STEPS + HALMAC_8197f_TRANS_CARDEMU_TO_SUS_STEPS + HALMAC_8197f_TRANS_END_STEPS];
extern HALMAC_WLAN_PWR_CFG halmac_8197f_hwpdn_flow[HALMAC_8197f_TRANS_ACT_TO_CARDEMU_STEPS + HALMAC_8197f_TRANS_CARDEMU_TO_PDN_STEPS + HALMAC_8197f_TRANS_END_STEPS];
extern HALMAC_WLAN_PWR_CFG halmac_8197f_enter_lps_flow[HALMAC_8197f_TRANS_ACT_TO_LPS_STEPS + HALMAC_8197f_TRANS_END_STEPS];
extern HALMAC_WLAN_PWR_CFG halmac_8197f_leave_lps_flow[HALMAC_8197f_TRANS_LPS_TO_ACT_STEPS + HALMAC_8197f_TRANS_END_STEPS];

#endif
