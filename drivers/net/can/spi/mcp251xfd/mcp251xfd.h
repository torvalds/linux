/* SPDX-License-Identifier: GPL-2.0
 *
 * mcp251xfd - Microchip MCP251xFD Family CAN controller driver
 *
 * Copyright (c) 2019 Pengutronix,
 *                    Marc Kleine-Budde <kernel@pengutronix.de>
 * Copyright (c) 2019 Martin Sperl <kernel@martin.sperl.org>
 */

#ifndef _MCP251XFD_H
#define _MCP251XFD_H

#include <linux/can/core.h>
#include <linux/can/dev.h>
#include <linux/can/rx-offload.h>
#include <linux/gpio/consumer.h>
#include <linux/kernel.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/spi/spi.h>

/* MPC251x registers */

/* CAN FD Controller Module SFR */
#define MCP251XFD_REG_CON 0x00
#define MCP251XFD_REG_CON_TXBWS_MASK GENMASK(31, 28)
#define MCP251XFD_REG_CON_ABAT BIT(27)
#define MCP251XFD_REG_CON_REQOP_MASK GENMASK(26, 24)
#define MCP251XFD_REG_CON_MODE_MIXED 0
#define MCP251XFD_REG_CON_MODE_SLEEP 1
#define MCP251XFD_REG_CON_MODE_INT_LOOPBACK 2
#define MCP251XFD_REG_CON_MODE_LISTENONLY 3
#define MCP251XFD_REG_CON_MODE_CONFIG 4
#define MCP251XFD_REG_CON_MODE_EXT_LOOPBACK 5
#define MCP251XFD_REG_CON_MODE_CAN2_0 6
#define MCP251XFD_REG_CON_MODE_RESTRICTED 7
#define MCP251XFD_REG_CON_OPMOD_MASK GENMASK(23, 21)
#define MCP251XFD_REG_CON_TXQEN BIT(20)
#define MCP251XFD_REG_CON_STEF BIT(19)
#define MCP251XFD_REG_CON_SERR2LOM BIT(18)
#define MCP251XFD_REG_CON_ESIGM BIT(17)
#define MCP251XFD_REG_CON_RTXAT BIT(16)
#define MCP251XFD_REG_CON_BRSDIS BIT(12)
#define MCP251XFD_REG_CON_BUSY BIT(11)
#define MCP251XFD_REG_CON_WFT_MASK GENMASK(10, 9)
#define MCP251XFD_REG_CON_WFT_T00FILTER 0x0
#define MCP251XFD_REG_CON_WFT_T01FILTER 0x1
#define MCP251XFD_REG_CON_WFT_T10FILTER 0x2
#define MCP251XFD_REG_CON_WFT_T11FILTER 0x3
#define MCP251XFD_REG_CON_WAKFIL BIT(8)
#define MCP251XFD_REG_CON_PXEDIS BIT(6)
#define MCP251XFD_REG_CON_ISOCRCEN BIT(5)
#define MCP251XFD_REG_CON_DNCNT_MASK GENMASK(4, 0)

#define MCP251XFD_REG_NBTCFG 0x04
#define MCP251XFD_REG_NBTCFG_BRP_MASK GENMASK(31, 24)
#define MCP251XFD_REG_NBTCFG_TSEG1_MASK GENMASK(23, 16)
#define MCP251XFD_REG_NBTCFG_TSEG2_MASK GENMASK(14, 8)
#define MCP251XFD_REG_NBTCFG_SJW_MASK GENMASK(6, 0)

#define MCP251XFD_REG_DBTCFG 0x08
#define MCP251XFD_REG_DBTCFG_BRP_MASK GENMASK(31, 24)
#define MCP251XFD_REG_DBTCFG_TSEG1_MASK GENMASK(20, 16)
#define MCP251XFD_REG_DBTCFG_TSEG2_MASK GENMASK(11, 8)
#define MCP251XFD_REG_DBTCFG_SJW_MASK GENMASK(3, 0)

#define MCP251XFD_REG_TDC 0x0c
#define MCP251XFD_REG_TDC_EDGFLTEN BIT(25)
#define MCP251XFD_REG_TDC_SID11EN BIT(24)
#define MCP251XFD_REG_TDC_TDCMOD_MASK GENMASK(17, 16)
#define MCP251XFD_REG_TDC_TDCMOD_AUTO 2
#define MCP251XFD_REG_TDC_TDCMOD_MANUAL 1
#define MCP251XFD_REG_TDC_TDCMOD_DISABLED 0
#define MCP251XFD_REG_TDC_TDCO_MASK GENMASK(14, 8)
#define MCP251XFD_REG_TDC_TDCV_MASK GENMASK(5, 0)

#define MCP251XFD_REG_TBC 0x10

#define MCP251XFD_REG_TSCON 0x14
#define MCP251XFD_REG_TSCON_TSRES BIT(18)
#define MCP251XFD_REG_TSCON_TSEOF BIT(17)
#define MCP251XFD_REG_TSCON_TBCEN BIT(16)
#define MCP251XFD_REG_TSCON_TBCPRE_MASK GENMASK(9, 0)

#define MCP251XFD_REG_VEC 0x18
#define MCP251XFD_REG_VEC_RXCODE_MASK GENMASK(30, 24)
#define MCP251XFD_REG_VEC_TXCODE_MASK GENMASK(22, 16)
#define MCP251XFD_REG_VEC_FILHIT_MASK GENMASK(12, 8)
#define MCP251XFD_REG_VEC_ICODE_MASK GENMASK(6, 0)

#define MCP251XFD_REG_INT 0x1c
#define MCP251XFD_REG_INT_IF_MASK GENMASK(15, 0)
#define MCP251XFD_REG_INT_IE_MASK GENMASK(31, 16)
#define MCP251XFD_REG_INT_IVMIE BIT(31)
#define MCP251XFD_REG_INT_WAKIE BIT(30)
#define MCP251XFD_REG_INT_CERRIE BIT(29)
#define MCP251XFD_REG_INT_SERRIE BIT(28)
#define MCP251XFD_REG_INT_RXOVIE BIT(27)
#define MCP251XFD_REG_INT_TXATIE BIT(26)
#define MCP251XFD_REG_INT_SPICRCIE BIT(25)
#define MCP251XFD_REG_INT_ECCIE BIT(24)
#define MCP251XFD_REG_INT_TEFIE BIT(20)
#define MCP251XFD_REG_INT_MODIE BIT(19)
#define MCP251XFD_REG_INT_TBCIE BIT(18)
#define MCP251XFD_REG_INT_RXIE BIT(17)
#define MCP251XFD_REG_INT_TXIE BIT(16)
#define MCP251XFD_REG_INT_IVMIF BIT(15)
#define MCP251XFD_REG_INT_WAKIF BIT(14)
#define MCP251XFD_REG_INT_CERRIF BIT(13)
#define MCP251XFD_REG_INT_SERRIF BIT(12)
#define MCP251XFD_REG_INT_RXOVIF BIT(11)
#define MCP251XFD_REG_INT_TXATIF BIT(10)
#define MCP251XFD_REG_INT_SPICRCIF BIT(9)
#define MCP251XFD_REG_INT_ECCIF BIT(8)
#define MCP251XFD_REG_INT_TEFIF BIT(4)
#define MCP251XFD_REG_INT_MODIF BIT(3)
#define MCP251XFD_REG_INT_TBCIF BIT(2)
#define MCP251XFD_REG_INT_RXIF BIT(1)
#define MCP251XFD_REG_INT_TXIF BIT(0)
/* These IRQ flags must be cleared by SW in the CAN_INT register */
#define MCP251XFD_REG_INT_IF_CLEARABLE_MASK \
	(MCP251XFD_REG_INT_IVMIF | MCP251XFD_REG_INT_WAKIF | \
	 MCP251XFD_REG_INT_CERRIF |  MCP251XFD_REG_INT_SERRIF | \
	 MCP251XFD_REG_INT_MODIF)

#define MCP251XFD_REG_RXIF 0x20
#define MCP251XFD_REG_TXIF 0x24
#define MCP251XFD_REG_RXOVIF 0x28
#define MCP251XFD_REG_TXATIF 0x2c
#define MCP251XFD_REG_TXREQ 0x30

#define MCP251XFD_REG_TREC 0x34
#define MCP251XFD_REG_TREC_TXBO BIT(21)
#define MCP251XFD_REG_TREC_TXBP BIT(20)
#define MCP251XFD_REG_TREC_RXBP BIT(19)
#define MCP251XFD_REG_TREC_TXWARN BIT(18)
#define MCP251XFD_REG_TREC_RXWARN BIT(17)
#define MCP251XFD_REG_TREC_EWARN BIT(16)
#define MCP251XFD_REG_TREC_TEC_MASK GENMASK(15, 8)
#define MCP251XFD_REG_TREC_REC_MASK GENMASK(7, 0)

#define MCP251XFD_REG_BDIAG0 0x38
#define MCP251XFD_REG_BDIAG0_DTERRCNT_MASK GENMASK(31, 24)
#define MCP251XFD_REG_BDIAG0_DRERRCNT_MASK GENMASK(23, 16)
#define MCP251XFD_REG_BDIAG0_NTERRCNT_MASK GENMASK(15, 8)
#define MCP251XFD_REG_BDIAG0_NRERRCNT_MASK GENMASK(7, 0)

#define MCP251XFD_REG_BDIAG1 0x3c
#define MCP251XFD_REG_BDIAG1_DLCMM BIT(31)
#define MCP251XFD_REG_BDIAG1_ESI BIT(30)
#define MCP251XFD_REG_BDIAG1_DCRCERR BIT(29)
#define MCP251XFD_REG_BDIAG1_DSTUFERR BIT(28)
#define MCP251XFD_REG_BDIAG1_DFORMERR BIT(27)
#define MCP251XFD_REG_BDIAG1_DBIT1ERR BIT(25)
#define MCP251XFD_REG_BDIAG1_DBIT0ERR BIT(24)
#define MCP251XFD_REG_BDIAG1_TXBOERR BIT(23)
#define MCP251XFD_REG_BDIAG1_NCRCERR BIT(21)
#define MCP251XFD_REG_BDIAG1_NSTUFERR BIT(20)
#define MCP251XFD_REG_BDIAG1_NFORMERR BIT(19)
#define MCP251XFD_REG_BDIAG1_NACKERR BIT(18)
#define MCP251XFD_REG_BDIAG1_NBIT1ERR BIT(17)
#define MCP251XFD_REG_BDIAG1_NBIT0ERR BIT(16)
#define MCP251XFD_REG_BDIAG1_BERR_MASK \
	(MCP251XFD_REG_BDIAG1_DLCMM | MCP251XFD_REG_BDIAG1_ESI | \
	 MCP251XFD_REG_BDIAG1_DCRCERR | MCP251XFD_REG_BDIAG1_DSTUFERR | \
	 MCP251XFD_REG_BDIAG1_DFORMERR | MCP251XFD_REG_BDIAG1_DBIT1ERR | \
	 MCP251XFD_REG_BDIAG1_DBIT0ERR | MCP251XFD_REG_BDIAG1_TXBOERR | \
	 MCP251XFD_REG_BDIAG1_NCRCERR | MCP251XFD_REG_BDIAG1_NSTUFERR | \
	 MCP251XFD_REG_BDIAG1_NFORMERR | MCP251XFD_REG_BDIAG1_NACKERR | \
	 MCP251XFD_REG_BDIAG1_NBIT1ERR | MCP251XFD_REG_BDIAG1_NBIT0ERR)
#define MCP251XFD_REG_BDIAG1_EFMSGCNT_MASK GENMASK(15, 0)

#define MCP251XFD_REG_TEFCON 0x40
#define MCP251XFD_REG_TEFCON_FSIZE_MASK GENMASK(28, 24)
#define MCP251XFD_REG_TEFCON_FRESET BIT(10)
#define MCP251XFD_REG_TEFCON_UINC BIT(8)
#define MCP251XFD_REG_TEFCON_TEFTSEN BIT(5)
#define MCP251XFD_REG_TEFCON_TEFOVIE BIT(3)
#define MCP251XFD_REG_TEFCON_TEFFIE BIT(2)
#define MCP251XFD_REG_TEFCON_TEFHIE BIT(1)
#define MCP251XFD_REG_TEFCON_TEFNEIE BIT(0)

#define MCP251XFD_REG_TEFSTA 0x44
#define MCP251XFD_REG_TEFSTA_TEFOVIF BIT(3)
#define MCP251XFD_REG_TEFSTA_TEFFIF BIT(2)
#define MCP251XFD_REG_TEFSTA_TEFHIF BIT(1)
#define MCP251XFD_REG_TEFSTA_TEFNEIF BIT(0)

#define MCP251XFD_REG_TEFUA 0x48

#define MCP251XFD_REG_TXQCON 0x50
#define MCP251XFD_REG_TXQCON_PLSIZE_MASK GENMASK(31, 29)
#define MCP251XFD_REG_TXQCON_PLSIZE_8 0
#define MCP251XFD_REG_TXQCON_PLSIZE_12 1
#define MCP251XFD_REG_TXQCON_PLSIZE_16 2
#define MCP251XFD_REG_TXQCON_PLSIZE_20 3
#define MCP251XFD_REG_TXQCON_PLSIZE_24 4
#define MCP251XFD_REG_TXQCON_PLSIZE_32 5
#define MCP251XFD_REG_TXQCON_PLSIZE_48 6
#define MCP251XFD_REG_TXQCON_PLSIZE_64 7
#define MCP251XFD_REG_TXQCON_FSIZE_MASK GENMASK(28, 24)
#define MCP251XFD_REG_TXQCON_TXAT_UNLIMITED 3
#define MCP251XFD_REG_TXQCON_TXAT_THREE_SHOT 1
#define MCP251XFD_REG_TXQCON_TXAT_ONE_SHOT 0
#define MCP251XFD_REG_TXQCON_TXAT_MASK GENMASK(22, 21)
#define MCP251XFD_REG_TXQCON_TXPRI_MASK GENMASK(20, 16)
#define MCP251XFD_REG_TXQCON_FRESET BIT(10)
#define MCP251XFD_REG_TXQCON_TXREQ BIT(9)
#define MCP251XFD_REG_TXQCON_UINC BIT(8)
#define MCP251XFD_REG_TXQCON_TXEN BIT(7)
#define MCP251XFD_REG_TXQCON_TXATIE BIT(4)
#define MCP251XFD_REG_TXQCON_TXQEIE BIT(2)
#define MCP251XFD_REG_TXQCON_TXQNIE BIT(0)

#define MCP251XFD_REG_TXQSTA 0x54
#define MCP251XFD_REG_TXQSTA_TXQCI_MASK GENMASK(12, 8)
#define MCP251XFD_REG_TXQSTA_TXABT BIT(7)
#define MCP251XFD_REG_TXQSTA_TXLARB BIT(6)
#define MCP251XFD_REG_TXQSTA_TXERR BIT(5)
#define MCP251XFD_REG_TXQSTA_TXATIF BIT(4)
#define MCP251XFD_REG_TXQSTA_TXQEIF BIT(2)
#define MCP251XFD_REG_TXQSTA_TXQNIF BIT(0)

#define MCP251XFD_REG_TXQUA 0x58

#define MCP251XFD_REG_FIFOCON(x) (0x50 + 0xc * (x))
#define MCP251XFD_REG_FIFOCON_PLSIZE_MASK GENMASK(31, 29)
#define MCP251XFD_REG_FIFOCON_PLSIZE_8 0
#define MCP251XFD_REG_FIFOCON_PLSIZE_12 1
#define MCP251XFD_REG_FIFOCON_PLSIZE_16 2
#define MCP251XFD_REG_FIFOCON_PLSIZE_20 3
#define MCP251XFD_REG_FIFOCON_PLSIZE_24 4
#define MCP251XFD_REG_FIFOCON_PLSIZE_32 5
#define MCP251XFD_REG_FIFOCON_PLSIZE_48 6
#define MCP251XFD_REG_FIFOCON_PLSIZE_64 7
#define MCP251XFD_REG_FIFOCON_FSIZE_MASK GENMASK(28, 24)
#define MCP251XFD_REG_FIFOCON_TXAT_MASK GENMASK(22, 21)
#define MCP251XFD_REG_FIFOCON_TXAT_ONE_SHOT 0
#define MCP251XFD_REG_FIFOCON_TXAT_THREE_SHOT 1
#define MCP251XFD_REG_FIFOCON_TXAT_UNLIMITED 3
#define MCP251XFD_REG_FIFOCON_TXPRI_MASK GENMASK(20, 16)
#define MCP251XFD_REG_FIFOCON_FRESET BIT(10)
#define MCP251XFD_REG_FIFOCON_TXREQ BIT(9)
#define MCP251XFD_REG_FIFOCON_UINC BIT(8)
#define MCP251XFD_REG_FIFOCON_TXEN BIT(7)
#define MCP251XFD_REG_FIFOCON_RTREN BIT(6)
#define MCP251XFD_REG_FIFOCON_RXTSEN BIT(5)
#define MCP251XFD_REG_FIFOCON_TXATIE BIT(4)
#define MCP251XFD_REG_FIFOCON_RXOVIE BIT(3)
#define MCP251XFD_REG_FIFOCON_TFERFFIE BIT(2)
#define MCP251XFD_REG_FIFOCON_TFHRFHIE BIT(1)
#define MCP251XFD_REG_FIFOCON_TFNRFNIE BIT(0)

#define MCP251XFD_REG_FIFOSTA(x) (0x54 + 0xc * (x))
#define MCP251XFD_REG_FIFOSTA_FIFOCI_MASK GENMASK(12, 8)
#define MCP251XFD_REG_FIFOSTA_TXABT BIT(7)
#define MCP251XFD_REG_FIFOSTA_TXLARB BIT(6)
#define MCP251XFD_REG_FIFOSTA_TXERR BIT(5)
#define MCP251XFD_REG_FIFOSTA_TXATIF BIT(4)
#define MCP251XFD_REG_FIFOSTA_RXOVIF BIT(3)
#define MCP251XFD_REG_FIFOSTA_TFERFFIF BIT(2)
#define MCP251XFD_REG_FIFOSTA_TFHRFHIF BIT(1)
#define MCP251XFD_REG_FIFOSTA_TFNRFNIF BIT(0)

#define MCP251XFD_REG_FIFOUA(x) (0x58 + 0xc * (x))

#define MCP251XFD_REG_FLTCON(x) (0x1d0 + 0x4 * (x))
#define MCP251XFD_REG_FLTCON_FLTEN3 BIT(31)
#define MCP251XFD_REG_FLTCON_F3BP_MASK GENMASK(28, 24)
#define MCP251XFD_REG_FLTCON_FLTEN2 BIT(23)
#define MCP251XFD_REG_FLTCON_F2BP_MASK GENMASK(20, 16)
#define MCP251XFD_REG_FLTCON_FLTEN1 BIT(15)
#define MCP251XFD_REG_FLTCON_F1BP_MASK GENMASK(12, 8)
#define MCP251XFD_REG_FLTCON_FLTEN0 BIT(7)
#define MCP251XFD_REG_FLTCON_F0BP_MASK GENMASK(4, 0)
#define MCP251XFD_REG_FLTCON_FLTEN(x) (BIT(7) << 8 * ((x) & 0x3))
#define MCP251XFD_REG_FLTCON_FLT_MASK(x) (GENMASK(7, 0) << (8 * ((x) & 0x3)))
#define MCP251XFD_REG_FLTCON_FBP(x, fifo) ((fifo) << 8 * ((x) & 0x3))

#define MCP251XFD_REG_FLTOBJ(x) (0x1f0 + 0x8 * (x))
#define MCP251XFD_REG_FLTOBJ_EXIDE BIT(30)
#define MCP251XFD_REG_FLTOBJ_SID11 BIT(29)
#define MCP251XFD_REG_FLTOBJ_EID_MASK GENMASK(28, 11)
#define MCP251XFD_REG_FLTOBJ_SID_MASK GENMASK(10, 0)

#define MCP251XFD_REG_FLTMASK(x) (0x1f4 + 0x8 * (x))
#define MCP251XFD_REG_MASK_MIDE BIT(30)
#define MCP251XFD_REG_MASK_MSID11 BIT(29)
#define MCP251XFD_REG_MASK_MEID_MASK GENMASK(28, 11)
#define MCP251XFD_REG_MASK_MSID_MASK GENMASK(10, 0)

/* RAM */
#define MCP251XFD_RAM_START 0x400
#define MCP251XFD_RAM_SIZE SZ_2K

/* Message Object */
#define MCP251XFD_OBJ_ID_SID11 BIT(29)
#define MCP251XFD_OBJ_ID_EID_MASK GENMASK(28, 11)
#define MCP251XFD_OBJ_ID_SID_MASK GENMASK(10, 0)
#define MCP251XFD_OBJ_FLAGS_SEQ_MCP2518FD_MASK GENMASK(31, 9)
#define MCP251XFD_OBJ_FLAGS_SEQ_MCP2517FD_MASK GENMASK(15, 9)
#define MCP251XFD_OBJ_FLAGS_SEQ_MASK MCP251XFD_OBJ_FLAGS_SEQ_MCP2518FD_MASK
#define MCP251XFD_OBJ_FLAGS_ESI BIT(8)
#define MCP251XFD_OBJ_FLAGS_FDF BIT(7)
#define MCP251XFD_OBJ_FLAGS_BRS BIT(6)
#define MCP251XFD_OBJ_FLAGS_RTR BIT(5)
#define MCP251XFD_OBJ_FLAGS_IDE BIT(4)
#define MCP251XFD_OBJ_FLAGS_DLC GENMASK(3, 0)

#define MCP251XFD_REG_FRAME_EFF_SID_MASK GENMASK(28, 18)
#define MCP251XFD_REG_FRAME_EFF_EID_MASK GENMASK(17, 0)

/* MCP2517/18FD SFR */
#define MCP251XFD_REG_OSC 0xe00
#define MCP251XFD_REG_OSC_SCLKRDY BIT(12)
#define MCP251XFD_REG_OSC_OSCRDY BIT(10)
#define MCP251XFD_REG_OSC_PLLRDY BIT(8)
#define MCP251XFD_REG_OSC_CLKODIV_10 3
#define MCP251XFD_REG_OSC_CLKODIV_4 2
#define MCP251XFD_REG_OSC_CLKODIV_2 1
#define MCP251XFD_REG_OSC_CLKODIV_1 0
#define MCP251XFD_REG_OSC_CLKODIV_MASK GENMASK(6, 5)
#define MCP251XFD_REG_OSC_SCLKDIV BIT(4)
#define MCP251XFD_REG_OSC_LPMEN BIT(3)	/* MCP2518FD only */
#define MCP251XFD_REG_OSC_OSCDIS BIT(2)
#define MCP251XFD_REG_OSC_PLLEN BIT(0)

#define MCP251XFD_REG_IOCON 0xe04
#define MCP251XFD_REG_IOCON_INTOD BIT(30)
#define MCP251XFD_REG_IOCON_SOF BIT(29)
#define MCP251XFD_REG_IOCON_TXCANOD BIT(28)
#define MCP251XFD_REG_IOCON_PM1 BIT(25)
#define MCP251XFD_REG_IOCON_PM0 BIT(24)
#define MCP251XFD_REG_IOCON_GPIO1 BIT(17)
#define MCP251XFD_REG_IOCON_GPIO0 BIT(16)
#define MCP251XFD_REG_IOCON_LAT1 BIT(9)
#define MCP251XFD_REG_IOCON_LAT0 BIT(8)
#define MCP251XFD_REG_IOCON_XSTBYEN BIT(6)
#define MCP251XFD_REG_IOCON_TRIS1 BIT(1)
#define MCP251XFD_REG_IOCON_TRIS0 BIT(0)

#define MCP251XFD_REG_CRC 0xe08
#define MCP251XFD_REG_CRC_FERRIE BIT(25)
#define MCP251XFD_REG_CRC_CRCERRIE BIT(24)
#define MCP251XFD_REG_CRC_FERRIF BIT(17)
#define MCP251XFD_REG_CRC_CRCERRIF BIT(16)
#define MCP251XFD_REG_CRC_IF_MASK GENMASK(17, 16)
#define MCP251XFD_REG_CRC_MASK GENMASK(15, 0)

#define MCP251XFD_REG_ECCCON 0xe0c
#define MCP251XFD_REG_ECCCON_PARITY_MASK GENMASK(14, 8)
#define MCP251XFD_REG_ECCCON_DEDIE BIT(2)
#define MCP251XFD_REG_ECCCON_SECIE BIT(1)
#define MCP251XFD_REG_ECCCON_ECCEN BIT(0)

#define MCP251XFD_REG_ECCSTAT 0xe10
#define MCP251XFD_REG_ECCSTAT_ERRADDR_MASK GENMASK(27, 16)
#define MCP251XFD_REG_ECCSTAT_IF_MASK GENMASK(2, 1)
#define MCP251XFD_REG_ECCSTAT_DEDIF BIT(2)
#define MCP251XFD_REG_ECCSTAT_SECIF BIT(1)

#define MCP251XFD_REG_DEVID 0xe14	/* MCP2518FD only */
#define MCP251XFD_REG_DEVID_ID_MASK GENMASK(7, 4)
#define MCP251XFD_REG_DEVID_REV_MASK GENMASK(3, 0)

/* number of TX FIFO objects, depending on CAN mode
 *
 * FIFO setup: tef: 8*12 bytes = 96 bytes, tx: 8*16 bytes = 128 bytes
 * FIFO setup: tef: 4*12 bytes = 48 bytes, tx: 4*72 bytes = 288 bytes
 */
#define MCP251XFD_RX_OBJ_NUM_MAX 32
#define MCP251XFD_TX_OBJ_NUM_CAN 8
#define MCP251XFD_TX_OBJ_NUM_CANFD 4

#if MCP251XFD_TX_OBJ_NUM_CAN > MCP251XFD_TX_OBJ_NUM_CANFD
#define MCP251XFD_TX_OBJ_NUM_MAX MCP251XFD_TX_OBJ_NUM_CAN
#else
#define MCP251XFD_TX_OBJ_NUM_MAX MCP251XFD_TX_OBJ_NUM_CANFD
#endif

#define MCP251XFD_NAPI_WEIGHT 32
#define MCP251XFD_TX_FIFO 1
#define MCP251XFD_RX_FIFO(x) (MCP251XFD_TX_FIFO + 1 + (x))

/* SPI commands */
#define MCP251XFD_SPI_INSTRUCTION_RESET 0x0000
#define MCP251XFD_SPI_INSTRUCTION_WRITE 0x2000
#define MCP251XFD_SPI_INSTRUCTION_READ 0x3000
#define MCP251XFD_SPI_INSTRUCTION_WRITE_CRC 0xa000
#define MCP251XFD_SPI_INSTRUCTION_READ_CRC 0xb000
#define MCP251XFD_SPI_INSTRUCTION_WRITE_CRC_SAFE 0xc000
#define MCP251XFD_SPI_ADDRESS_MASK GENMASK(11, 0)

#define MCP251XFD_SYSCLOCK_HZ_MAX 40000000
#define MCP251XFD_SYSCLOCK_HZ_MIN 1000000
#define MCP251XFD_SPICLOCK_HZ_MAX 20000000
#define MCP251XFD_OSC_PLL_MULTIPLIER 10
#define MCP251XFD_OSC_STAB_SLEEP_US (3 * USEC_PER_MSEC)
#define MCP251XFD_OSC_STAB_TIMEOUT_US (10 * MCP251XFD_OSC_STAB_SLEEP_US)
#define MCP251XFD_POLL_SLEEP_US (10)
#define MCP251XFD_POLL_TIMEOUT_US (USEC_PER_MSEC)
#define MCP251XFD_SOFTRESET_RETRIES_MAX 3
#define MCP251XFD_READ_CRC_RETRIES_MAX 3
#define MCP251XFD_ECC_CNT_MAX 2
#define MCP251XFD_SANITIZE_SPI 1
#define MCP251XFD_SANITIZE_CAN 1

/* Silence TX MAB overflow warnings */
#define MCP251XFD_QUIRK_MAB_NO_WARN BIT(0)
/* Use CRC to access registers */
#define MCP251XFD_QUIRK_CRC_REG BIT(1)
/* Use CRC to access RX/TEF-RAM */
#define MCP251XFD_QUIRK_CRC_RX BIT(2)
/* Use CRC to access TX-RAM */
#define MCP251XFD_QUIRK_CRC_TX BIT(3)
/* Enable ECC for RAM */
#define MCP251XFD_QUIRK_ECC BIT(4)
/* Use Half Duplex SPI transfers */
#define MCP251XFD_QUIRK_HALF_DUPLEX BIT(5)

struct mcp251xfd_hw_tef_obj {
	u32 id;
	u32 flags;
	u32 ts;
};

/* The tx_obj_raw version is used in spi async, i.e. without
 * regmap. We have to take care of endianness ourselves.
 */
struct mcp251xfd_hw_tx_obj_raw {
	__le32 id;
	__le32 flags;
	u8 data[sizeof_field(struct canfd_frame, data)];
};

struct mcp251xfd_hw_tx_obj_can {
	u32 id;
	u32 flags;
	u8 data[sizeof_field(struct can_frame, data)];
};

struct mcp251xfd_hw_tx_obj_canfd {
	u32 id;
	u32 flags;
	u8 data[sizeof_field(struct canfd_frame, data)];
};

struct mcp251xfd_hw_rx_obj_can {
	u32 id;
	u32 flags;
	u32 ts;
	u8 data[sizeof_field(struct can_frame, data)];
};

struct mcp251xfd_hw_rx_obj_canfd {
	u32 id;
	u32 flags;
	u32 ts;
	u8 data[sizeof_field(struct canfd_frame, data)];
};

struct mcp251xfd_tef_ring {
	unsigned int head;
	unsigned int tail;

	/* u8 obj_num equals tx_ring->obj_num */
	/* u8 obj_size equals sizeof(struct mcp251xfd_hw_tef_obj) */
};

struct __packed mcp251xfd_buf_cmd {
	__be16 cmd;
};

struct __packed mcp251xfd_buf_cmd_crc {
	__be16 cmd;
	u8 len;
};

union mcp251xfd_tx_obj_load_buf {
	struct __packed {
		struct mcp251xfd_buf_cmd cmd;
		struct mcp251xfd_hw_tx_obj_raw hw_tx_obj;
	} nocrc;
	struct __packed {
		struct mcp251xfd_buf_cmd_crc cmd;
		struct mcp251xfd_hw_tx_obj_raw hw_tx_obj;
		__be16 crc;
	} crc;
} ____cacheline_aligned;

union mcp251xfd_write_reg_buf {
	struct __packed {
		struct mcp251xfd_buf_cmd cmd;
		u8 data[4];
	} nocrc;
	struct __packed {
		struct mcp251xfd_buf_cmd_crc cmd;
		u8 data[4];
		__be16 crc;
	} crc;
} ____cacheline_aligned;

struct mcp251xfd_tx_obj {
	struct spi_message msg;
	struct spi_transfer xfer[2];
	union mcp251xfd_tx_obj_load_buf buf;
};

struct mcp251xfd_tx_ring {
	unsigned int head;
	unsigned int tail;

	u16 base;
	u8 obj_num;
	u8 obj_size;

	struct mcp251xfd_tx_obj obj[MCP251XFD_TX_OBJ_NUM_MAX];
	union mcp251xfd_write_reg_buf rts_buf;
};

struct mcp251xfd_rx_ring {
	unsigned int head;
	unsigned int tail;

	u16 base;
	u8 nr;
	u8 fifo_nr;
	u8 obj_num;
	u8 obj_size;

	union mcp251xfd_write_reg_buf uinc_buf;
	struct spi_transfer uinc_xfer[MCP251XFD_RX_OBJ_NUM_MAX];
	struct mcp251xfd_hw_rx_obj_canfd obj[];
};

struct __packed mcp251xfd_map_buf_nocrc {
	struct mcp251xfd_buf_cmd cmd;
	u8 data[256];
} ____cacheline_aligned;

struct __packed mcp251xfd_map_buf_crc {
	struct mcp251xfd_buf_cmd_crc cmd;
	u8 data[256 - 4];
	__be16 crc;
} ____cacheline_aligned;

struct mcp251xfd_ecc {
	u32 ecc_stat;
	int cnt;
};

struct mcp251xfd_regs_status {
	u32 intf;
};

enum mcp251xfd_model {
	MCP251XFD_MODEL_MCP2517FD = 0x2517,
	MCP251XFD_MODEL_MCP2518FD = 0x2518,
	MCP251XFD_MODEL_MCP251XFD = 0xffff,	/* autodetect model */
};

struct mcp251xfd_devtype_data {
	enum mcp251xfd_model model;
	u32 quirks;
};

struct mcp251xfd_priv {
	struct can_priv can;
	struct can_rx_offload offload;
	struct net_device *ndev;

	struct regmap *map_reg;			/* register access */
	struct regmap *map_rx;			/* RX/TEF RAM access */

	struct regmap *map_nocrc;
	struct mcp251xfd_map_buf_nocrc *map_buf_nocrc_rx;
	struct mcp251xfd_map_buf_nocrc *map_buf_nocrc_tx;

	struct regmap *map_crc;
	struct mcp251xfd_map_buf_crc *map_buf_crc_rx;
	struct mcp251xfd_map_buf_crc *map_buf_crc_tx;

	struct spi_device *spi;
	u32 spi_max_speed_hz_orig;

	struct mcp251xfd_tef_ring tef;
	struct mcp251xfd_tx_ring tx[1];
	struct mcp251xfd_rx_ring *rx[1];

	u8 rx_ring_num;

	struct mcp251xfd_ecc ecc;
	struct mcp251xfd_regs_status regs_status;

	struct gpio_desc *rx_int;
	struct clk *clk;
	struct regulator *reg_vdd;
	struct regulator *reg_xceiver;

	struct mcp251xfd_devtype_data devtype_data;
	struct can_berr_counter bec;
};

#define MCP251XFD_IS(_model) \
static inline bool \
mcp251xfd_is_##_model(const struct mcp251xfd_priv *priv) \
{ \
	return priv->devtype_data.model == MCP251XFD_MODEL_MCP##_model##FD; \
}

MCP251XFD_IS(2517);
MCP251XFD_IS(2518);
MCP251XFD_IS(251X);

static inline u8 mcp251xfd_first_byte_set(u32 mask)
{
	return (mask & 0x0000ffff) ?
		((mask & 0x000000ff) ? 0 : 1) :
		((mask & 0x00ff0000) ? 2 : 3);
}

static inline u8 mcp251xfd_last_byte_set(u32 mask)
{
	return (mask & 0xffff0000) ?
		((mask & 0xff000000) ? 3 : 2) :
		((mask & 0x0000ff00) ? 1 : 0);
}

static inline __be16 mcp251xfd_cmd_reset(void)
{
	return cpu_to_be16(MCP251XFD_SPI_INSTRUCTION_RESET);
}

static inline void
mcp251xfd_spi_cmd_read_nocrc(struct mcp251xfd_buf_cmd *cmd, u16 addr)
{
	cmd->cmd = cpu_to_be16(MCP251XFD_SPI_INSTRUCTION_READ | addr);
}

static inline void
mcp251xfd_spi_cmd_write_nocrc(struct mcp251xfd_buf_cmd *cmd, u16 addr)
{
	cmd->cmd = cpu_to_be16(MCP251XFD_SPI_INSTRUCTION_WRITE | addr);
}

static inline bool mcp251xfd_reg_in_ram(unsigned int reg)
{
	static const struct regmap_range range =
		regmap_reg_range(MCP251XFD_RAM_START,
				 MCP251XFD_RAM_START + MCP251XFD_RAM_SIZE - 4);

	return regmap_reg_in_range(reg, &range);
}

static inline void
__mcp251xfd_spi_cmd_crc_set_len(struct mcp251xfd_buf_cmd_crc *cmd,
				u16 len, bool in_ram)
{
	/* Number of u32 for RAM access, number of u8 otherwise. */
	if (in_ram)
		cmd->len = len >> 2;
	else
		cmd->len = len;
}

static inline void
mcp251xfd_spi_cmd_crc_set_len_in_ram(struct mcp251xfd_buf_cmd_crc *cmd, u16 len)
{
	__mcp251xfd_spi_cmd_crc_set_len(cmd, len, true);
}

static inline void
mcp251xfd_spi_cmd_crc_set_len_in_reg(struct mcp251xfd_buf_cmd_crc *cmd, u16 len)
{
	__mcp251xfd_spi_cmd_crc_set_len(cmd, len, false);
}

static inline void
mcp251xfd_spi_cmd_read_crc_set_addr(struct mcp251xfd_buf_cmd_crc *cmd, u16 addr)
{
	cmd->cmd = cpu_to_be16(MCP251XFD_SPI_INSTRUCTION_READ_CRC | addr);
}

static inline void
mcp251xfd_spi_cmd_read_crc(struct mcp251xfd_buf_cmd_crc *cmd,
			   u16 addr, u16 len)
{
	mcp251xfd_spi_cmd_read_crc_set_addr(cmd, addr);
	__mcp251xfd_spi_cmd_crc_set_len(cmd, len, mcp251xfd_reg_in_ram(addr));
}

static inline void
mcp251xfd_spi_cmd_write_crc_set_addr(struct mcp251xfd_buf_cmd_crc *cmd,
				     u16 addr)
{
	cmd->cmd = cpu_to_be16(MCP251XFD_SPI_INSTRUCTION_WRITE_CRC | addr);
}

static inline void
mcp251xfd_spi_cmd_write_crc(struct mcp251xfd_buf_cmd_crc *cmd,
			    u16 addr, u16 len)
{
	mcp251xfd_spi_cmd_write_crc_set_addr(cmd, addr);
	__mcp251xfd_spi_cmd_crc_set_len(cmd, len, mcp251xfd_reg_in_ram(addr));
}

static inline u8 *
mcp251xfd_spi_cmd_write(const struct mcp251xfd_priv *priv,
			union mcp251xfd_write_reg_buf *write_reg_buf,
			u16 addr)
{
	u8 *data;

	if (priv->devtype_data.quirks & MCP251XFD_QUIRK_CRC_REG) {
		mcp251xfd_spi_cmd_write_crc_set_addr(&write_reg_buf->crc.cmd,
						     addr);
		data = write_reg_buf->crc.data;
	} else {
		mcp251xfd_spi_cmd_write_nocrc(&write_reg_buf->nocrc.cmd,
					      addr);
		data = write_reg_buf->nocrc.data;
	}

	return data;
}

static inline u16 mcp251xfd_get_tef_obj_addr(u8 n)
{
	return MCP251XFD_RAM_START +
		sizeof(struct mcp251xfd_hw_tef_obj) * n;
}

static inline u16
mcp251xfd_get_tx_obj_addr(const struct mcp251xfd_tx_ring *ring, u8 n)
{
	return ring->base + ring->obj_size * n;
}

static inline u16
mcp251xfd_get_rx_obj_addr(const struct mcp251xfd_rx_ring *ring, u8 n)
{
	return ring->base + ring->obj_size * n;
}

static inline u8 mcp251xfd_get_tef_head(const struct mcp251xfd_priv *priv)
{
	return priv->tef.head & (priv->tx->obj_num - 1);
}

static inline u8 mcp251xfd_get_tef_tail(const struct mcp251xfd_priv *priv)
{
	return priv->tef.tail & (priv->tx->obj_num - 1);
}

static inline u8 mcp251xfd_get_tef_len(const struct mcp251xfd_priv *priv)
{
	return priv->tef.head - priv->tef.tail;
}

static inline u8 mcp251xfd_get_tef_linear_len(const struct mcp251xfd_priv *priv)
{
	u8 len;

	len = mcp251xfd_get_tef_len(priv);

	return min_t(u8, len, priv->tx->obj_num - mcp251xfd_get_tef_tail(priv));
}

static inline u8 mcp251xfd_get_tx_head(const struct mcp251xfd_tx_ring *ring)
{
	return ring->head & (ring->obj_num - 1);
}

static inline u8 mcp251xfd_get_tx_tail(const struct mcp251xfd_tx_ring *ring)
{
	return ring->tail & (ring->obj_num - 1);
}

static inline u8 mcp251xfd_get_tx_free(const struct mcp251xfd_tx_ring *ring)
{
	return ring->obj_num - (ring->head - ring->tail);
}

static inline int
mcp251xfd_get_tx_nr_by_addr(const struct mcp251xfd_tx_ring *tx_ring, u8 *nr,
			    u16 addr)
{
	if (addr < mcp251xfd_get_tx_obj_addr(tx_ring, 0) ||
	    addr >= mcp251xfd_get_tx_obj_addr(tx_ring, tx_ring->obj_num))
		return -ENOENT;

	*nr = (addr - mcp251xfd_get_tx_obj_addr(tx_ring, 0)) /
		tx_ring->obj_size;

	return 0;
}

static inline u8 mcp251xfd_get_rx_head(const struct mcp251xfd_rx_ring *ring)
{
	return ring->head & (ring->obj_num - 1);
}

static inline u8 mcp251xfd_get_rx_tail(const struct mcp251xfd_rx_ring *ring)
{
	return ring->tail & (ring->obj_num - 1);
}

static inline u8 mcp251xfd_get_rx_len(const struct mcp251xfd_rx_ring *ring)
{
	return ring->head - ring->tail;
}

static inline u8
mcp251xfd_get_rx_linear_len(const struct mcp251xfd_rx_ring *ring)
{
	u8 len;

	len = mcp251xfd_get_rx_len(ring);

	return min_t(u8, len, ring->obj_num - mcp251xfd_get_rx_tail(ring));
}

#define mcp251xfd_for_each_tx_obj(ring, _obj, n) \
	for ((n) = 0, (_obj) = &(ring)->obj[(n)]; \
	     (n) < (ring)->obj_num; \
	     (n)++, (_obj) = &(ring)->obj[(n)])

#define mcp251xfd_for_each_rx_ring(priv, ring, n) \
	for ((n) = 0, (ring) = *((priv)->rx + (n)); \
	     (n) < (priv)->rx_ring_num; \
	     (n)++, (ring) = *((priv)->rx + (n)))

int mcp251xfd_regmap_init(struct mcp251xfd_priv *priv);
u16 mcp251xfd_crc16_compute2(const void *cmd, size_t cmd_size,
			     const void *data, size_t data_size);
u16 mcp251xfd_crc16_compute(const void *data, size_t data_size);

#endif
