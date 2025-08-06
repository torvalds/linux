/* SPDX-License-Identifier: GPL-2.0 */
/*
 * RZ xSPI Interface Registers Definitions
 *
 * Copyright (C) 2025 Renesas Electronics Corporation
 */

#ifndef __RENESAS_XSPI_IF_REGS_H__
#define __RENESAS_XSPI_IF_REGS_H__

#include <linux/bits.h>

/* xSPI Wrapper Configuration Register */
#define XSPI_WRAPCFG		0x0000

/* xSPI Bridge Configuration Register */
#define XSPI_BMCFG		0x0008
#define XSPI_BMCFG_WRMD		BIT(0)
#define XSPI_BMCFG_MWRCOMB	BIT(7)
#define XSPI_BMCFG_MWRSIZE(val)	(((val) & 0xff) << 8)
#define XSPI_BMCFG_PREEN	BIT(16)

/* xSPI Command Map Configuration Register 0 CS0 */
#define XSPI_CMCFG0CS0		0x0010
#define XSPI_CMCFG0_FFMT(val)		(((val) & 0x03) << 0)
#define XSPI_CMCFG0_ADDSIZE(val)	(((val) & 0x03) << 2)

/* xSPI Command Map Configuration Register 1 CS0 */
#define XSPI_CMCFG1CS0		0x0014
#define XSPI_CMCFG1_RDCMD(val)	(((val) & 0xffff) << 0)
#define XSPI_CMCFG1_RDCMD_UPPER_BYTE(val)	(((val) & 0xff) << 8)
#define XSPI_CMCFG1_RDLATE(val)	(((val) & 0x1f) << 16)

/* xSPI Command Map Configuration Register 2 CS0 */
#define XSPI_CMCFG2CS0		0x0018
#define XSPI_CMCFG2_WRCMD(val)	(((val) & 0xffff) << 0)
#define XSPI_CMCFG2_WRCMD_UPPER(val)	(((val) & 0xff) << 8)
#define XSPI_CMCFG2_WRLATE(val)	(((val) & 0x1f) << 16)

/* xSPI Link I/O Configuration Register CS0 */
#define XSPI_LIOCFGCS0		0x0050
#define XSPI_LIOCFG_PRTMD(val)	(((val) & 0x3ff) << 0)
#define XSPI_LIOCFG_CSMIN(val)	(((val) & 0x0f) << 16)
#define XSPI_LIOCFG_CSASTEX	BIT(20)
#define XSPI_LIOCFG_CSNEGEX	BIT(21)

/* xSPI Bridge Map Control Register 0 */
#define XSPI_BMCTL0		0x0060
#define XSPI_BMCTL0_CS0ACC(val)	(((val) & 0x03) << 0)

/* xSPI Bridge Map Control Register 1 */
#define XSPI_BMCTL1		0x0064
#define XSPI_BMCTL1_MWRPUSH	BIT(8)

/* xSPI Command Manual Control Register 0 */
#define XSPI_CDCTL0		0x0070
#define XSPI_CDCTL0_TRREQ	BIT(0)
#define XSPI_CDCTL0_CSSEL	BIT(3)
#define XSPI_CDCTL0_TRNUM(val)	(((val) & 0x03) << 4)

/* xSPI Command Manual Type Buf */
#define XSPI_CDTBUF0		0x0080
#define XSPI_CDTBUF_CMDSIZE(val)	(((val) & 0x03) << 0)
#define XSPI_CDTBUF_ADDSIZE(val)	(((val) & 0x07) << 2)
#define XSPI_CDTBUF_DATASIZE(val)	(((val) & 0x0f) << 5)
#define XSPI_CDTBUF_LATE(val)		(((val) & 0x1f) << 9)
#define XSPI_CDTBUF_TRTYPE	BIT(15)
#define XSPI_CDTBUF_CMD(val)		(((val) & 0xffff) << 16)
#define XSPI_CDTBUF_CMD_FIELD(val)	(((val) & 0xff) << 24)

/* xSPI Command Manual Address Buff */
#define XSPI_CDABUF0		0x0084

/* xSPI Command Manual Data 0 Buf */
#define XSPI_CDD0BUF0		0x0088

/* xSPI Command Manual Data 1 Buf */
#define XSPI_CDD1BUF0		0x008c

/* xSPI Command Calibration Control Register 0 CS0 */
#define XSPI_CCCTL0CS0		0x0130
#define XSPI_CCCTL0_CAEN	BIT(0)

/* xSPI Interrupt Status Register */
#define XSPI_INTS		0x0190
#define XSPI_INTS_CMDCMP	BIT(0)

/* xSPI Interrupt Clear Register */
#define XSPI_INTC		0x0194
#define XSPI_INTC_CMDCMPC	BIT(0)

/* xSPI Interrupt Enable Register */
#define XSPI_INTE		0x0198
#define XSPI_INTE_CMDCMPE	BIT(0)

/* Maximum data size of MWRSIZE*/
#define MWRSIZE_MAX		64

/* xSPI Protocol mode */
#define PROTO_1S_2S_2S		0x48
#define PROTO_2S_2S_2S		0x49
#define PROTO_1S_4S_4S		0x090
#define PROTO_4S_4S_4S		0x092

#endif /* __RENESAS_XSPI_IF_REGS_H__ */
