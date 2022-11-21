/* SPDX-License-Identifier: GPL-2.0+ */
/*****************************************************************************
 *
 *	Copyright (C) 1997-2002 Inside Out Networks, Inc.
 *
 *	Feb-16-2001	DMI	Added I2C structure definitions
 *	May-29-2002	gkh	Ported to Linux
 *
 *
 ******************************************************************************/

#ifndef _IO_TI_H_
#define _IO_TI_H_

/* Address Space */
#define DTK_ADDR_SPACE_XDATA		0x03	/* Addr is placed in XDATA space */
#define DTK_ADDR_SPACE_I2C_TYPE_II	0x82	/* Addr is placed in I2C area */
#define DTK_ADDR_SPACE_I2C_TYPE_III	0x83	/* Addr is placed in I2C area */

/* UART Defines */
#define UMPMEM_BASE_UART1		0xFFA0	/* UMP UART1 base address */
#define UMPMEM_BASE_UART2		0xFFB0	/* UMP UART2 base address */
#define UMPMEM_OFFS_UART_LSR		0x05	/* UMP UART LSR register offset */

/* Bits per character */
#define UMP_UART_CHAR5BITS		0x00
#define UMP_UART_CHAR6BITS		0x01
#define UMP_UART_CHAR7BITS		0x02
#define UMP_UART_CHAR8BITS		0x03

/* Parity */
#define UMP_UART_NOPARITY		0x00
#define UMP_UART_ODDPARITY		0x01
#define UMP_UART_EVENPARITY		0x02
#define UMP_UART_MARKPARITY		0x03
#define UMP_UART_SPACEPARITY		0x04

/* Stop bits */
#define UMP_UART_STOPBIT1		0x00
#define UMP_UART_STOPBIT15		0x01
#define UMP_UART_STOPBIT2		0x02

/* Line status register masks */
#define UMP_UART_LSR_OV_MASK		0x01
#define UMP_UART_LSR_PE_MASK		0x02
#define UMP_UART_LSR_FE_MASK		0x04
#define UMP_UART_LSR_BR_MASK		0x08
#define UMP_UART_LSR_ER_MASK		0x0F
#define UMP_UART_LSR_RX_MASK		0x10
#define UMP_UART_LSR_TX_MASK		0x20

#define UMP_UART_LSR_DATA_MASK		(LSR_PAR_ERR | LSR_FRM_ERR | LSR_BREAK)

/* Port Settings Constants) */
#define UMP_MASK_UART_FLAGS_RTS_FLOW		0x0001
#define UMP_MASK_UART_FLAGS_RTS_DISABLE		0x0002
#define UMP_MASK_UART_FLAGS_PARITY		0x0008
#define UMP_MASK_UART_FLAGS_OUT_X_DSR_FLOW	0x0010
#define UMP_MASK_UART_FLAGS_OUT_X_CTS_FLOW	0x0020
#define UMP_MASK_UART_FLAGS_OUT_X		0x0040
#define UMP_MASK_UART_FLAGS_OUT_XA		0x0080
#define UMP_MASK_UART_FLAGS_IN_X		0x0100
#define UMP_MASK_UART_FLAGS_DTR_FLOW		0x0800
#define UMP_MASK_UART_FLAGS_DTR_DISABLE		0x1000
#define UMP_MASK_UART_FLAGS_RECEIVE_MS_INT	0x2000
#define UMP_MASK_UART_FLAGS_AUTO_START_ON_ERR	0x4000

#define UMP_DMA_MODE_CONTINOUS			0x01
#define UMP_PIPE_TRANS_TIMEOUT_ENA		0x80
#define UMP_PIPE_TRANSFER_MODE_MASK		0x03
#define UMP_PIPE_TRANS_TIMEOUT_MASK		0x7C

/* Purge port Direction Mask Bits */
#define UMP_PORT_DIR_OUT			0x01
#define UMP_PORT_DIR_IN				0x02

/* Address of Port 0 */
#define UMPM_UART1_PORT				0x03

/* Commands */
#define	UMPC_SET_CONFIG			0x05
#define	UMPC_OPEN_PORT			0x06
#define	UMPC_CLOSE_PORT			0x07
#define	UMPC_START_PORT			0x08
#define	UMPC_STOP_PORT			0x09
#define	UMPC_TEST_PORT			0x0A
#define	UMPC_PURGE_PORT			0x0B

/* Force the Firmware to complete the current Read */
#define	UMPC_COMPLETE_READ		0x80
/* Force UMP back into BOOT Mode */
#define	UMPC_HARDWARE_RESET		0x81
/*
 * Copy current download image to type 0xf2 record in 16k I2C
 * firmware will change 0xff record to type 2 record when complete
 */
#define	UMPC_COPY_DNLD_TO_I2C		0x82

/*
 * Special function register commands
 * wIndex is register address
 * wValue is MSB/LSB mask/data
 */
#define	UMPC_WRITE_SFR			0x83	/* Write SFR Register */

/* wIndex is register address */
#define	UMPC_READ_SFR			0x84	/* Read SRF Register */

/* Set or Clear DTR (wValue bit 0 Set/Clear)	wIndex ModuleID (port) */
#define	UMPC_SET_CLR_DTR		0x85

/* Set or Clear RTS (wValue bit 0 Set/Clear)	wIndex ModuleID (port) */
#define	UMPC_SET_CLR_RTS		0x86

/* Set or Clear LOOPBACK (wValue bit 0 Set/Clear) wIndex ModuleID (port) */
#define	UMPC_SET_CLR_LOOPBACK		0x87

/* Set or Clear BREAK (wValue bit 0 Set/Clear)	wIndex ModuleID (port) */
#define	UMPC_SET_CLR_BREAK		0x88

/* Read MSR wIndex ModuleID (port) */
#define	UMPC_READ_MSR			0x89

/* Toolkit commands */
/* Read-write group */
#define	UMPC_MEMORY_READ		0x92
#define	UMPC_MEMORY_WRITE		0x93

/*
 *	UMP DMA Definitions
 */
#define UMPD_OEDB1_ADDRESS		0xFF08
#define UMPD_OEDB2_ADDRESS		0xFF10

struct out_endpoint_desc_block {
	u8 Configuration;
	u8 XBufAddr;
	u8 XByteCount;
	u8 Unused1;
	u8 Unused2;
	u8 YBufAddr;
	u8 YByteCount;
	u8 BufferSize;
};


/*
 * TYPE DEFINITIONS
 * Structures for Firmware commands
 */
/* UART settings */
struct ump_uart_config {
	u16 wBaudRate;		/* Baud rate                        */
	u16 wFlags;		/* Bitmap mask of flags             */
	u8 bDataBits;		/* 5..8 - data bits per character   */
	u8 bParity;		/* Parity settings                  */
	u8 bStopBits;		/* Stop bits settings               */
	char cXon;		/* XON character                    */
	char cXoff;		/* XOFF character                   */
	u8 bUartMode;		/* Will be updated when a user      */
				/* interface is defined             */
};


/*
 * TYPE DEFINITIONS
 * Structures for USB interrupts
 */
/* Interrupt packet structure */
struct ump_interrupt {
	u8 bICode;			/* Interrupt code (interrupt num)   */
	u8 bIInfo;			/* Interrupt information            */
};


#define TIUMP_GET_PORT_FROM_CODE(c)	(((c) >> 6) & 0x01)
#define TIUMP_GET_FUNC_FROM_CODE(c)	((c) & 0x0f)
#define TIUMP_INTERRUPT_CODE_LSR	0x03
#define TIUMP_INTERRUPT_CODE_MSR	0x04

#endif
