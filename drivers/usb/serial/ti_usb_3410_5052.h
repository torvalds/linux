/* vi: ts=8 sw=8
 *
 * TI 3410/5052 USB Serial Driver Header
 *
 * Copyright (C) 2004 Texas Instruments
 *
 * This driver is based on the Linux io_ti driver, which is
 *   Copyright (C) 2000-2002 Inside Out Networks
 *   Copyright (C) 2001-2002 Greg Kroah-Hartman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * For questions or problems with this driver, contact Texas Instruments
 * technical support, or Al Borchers <alborchers@steinerpoint.com>, or
 * Peter Berger <pberger@brimson.com>.
 */

#ifndef _TI_3410_5052_H_
#define _TI_3410_5052_H_

/* Configuration ids */
#define TI_BOOT_CONFIG			1
#define TI_ACTIVE_CONFIG		2

/* Vendor and product ids */
#define TI_VENDOR_ID			0x0451
#define IBM_VENDOR_ID			0x04b3
#define TI_3410_PRODUCT_ID		0x3410
#define IBM_4543_PRODUCT_ID		0x4543
#define IBM_454B_PRODUCT_ID		0x454b
#define IBM_454C_PRODUCT_ID		0x454c
#define TI_3410_EZ430_ID		0xF430  /* TI ez430 development tool */
#define TI_5052_BOOT_PRODUCT_ID		0x5052	/* no EEPROM, no firmware */
#define TI_5152_BOOT_PRODUCT_ID		0x5152	/* no EEPROM, no firmware */
#define TI_5052_EEPROM_PRODUCT_ID	0x505A	/* EEPROM, no firmware */
#define TI_5052_FIRMWARE_PRODUCT_ID	0x505F	/* firmware is running */
#define FRI2_PRODUCT_ID			0x5053  /* Fish River Island II */

/* Multi-Tech vendor and product ids */
#define MTS_VENDOR_ID			0x06E0
#define MTS_GSM_NO_FW_PRODUCT_ID	0xF108
#define MTS_CDMA_NO_FW_PRODUCT_ID	0xF109
#define MTS_CDMA_PRODUCT_ID		0xF110
#define MTS_GSM_PRODUCT_ID		0xF111
#define MTS_EDGE_PRODUCT_ID		0xF112
#define MTS_MT9234MU_PRODUCT_ID		0xF114
#define MTS_MT9234ZBA_PRODUCT_ID	0xF115
#define MTS_MT9234ZBAOLD_PRODUCT_ID	0x0319

/* Abbott Diabetics vendor and product ids */
#define ABBOTT_VENDOR_ID		0x1a61
#define ABBOTT_STEREO_PLUG_ID		0x3410
#define ABBOTT_PRODUCT_ID		ABBOTT_STEREO_PLUG_ID
#define ABBOTT_STRIP_PORT_ID		0x3420

/* Honeywell vendor and product IDs */
#define HONEYWELL_VENDOR_ID		0x10ac
#define HONEYWELL_HGI80_PRODUCT_ID	0x0102  /* Honeywell HGI80 */

/* Commands */
#define TI_GET_VERSION			0x01
#define TI_GET_PORT_STATUS		0x02
#define TI_GET_PORT_DEV_INFO		0x03
#define TI_GET_CONFIG			0x04
#define TI_SET_CONFIG			0x05
#define TI_OPEN_PORT			0x06
#define TI_CLOSE_PORT			0x07
#define TI_START_PORT			0x08
#define TI_STOP_PORT			0x09
#define TI_TEST_PORT			0x0A
#define TI_PURGE_PORT			0x0B
#define TI_RESET_EXT_DEVICE		0x0C
#define TI_WRITE_DATA			0x80
#define TI_READ_DATA			0x81
#define TI_REQ_TYPE_CLASS		0x82

/* Module identifiers */
#define TI_I2C_PORT			0x01
#define TI_IEEE1284_PORT		0x02
#define TI_UART1_PORT			0x03
#define TI_UART2_PORT			0x04
#define TI_RAM_PORT			0x05

/* Modem status */
#define TI_MSR_DELTA_CTS		0x01
#define TI_MSR_DELTA_DSR		0x02
#define TI_MSR_DELTA_RI			0x04
#define TI_MSR_DELTA_CD			0x08
#define TI_MSR_CTS			0x10
#define TI_MSR_DSR			0x20
#define TI_MSR_RI			0x40
#define TI_MSR_CD			0x80
#define TI_MSR_DELTA_MASK		0x0F
#define TI_MSR_MASK			0xF0

/* Line status */
#define TI_LSR_OVERRUN_ERROR		0x01
#define TI_LSR_PARITY_ERROR		0x02
#define TI_LSR_FRAMING_ERROR		0x04
#define TI_LSR_BREAK			0x08
#define TI_LSR_ERROR			0x0F
#define TI_LSR_RX_FULL			0x10
#define TI_LSR_TX_EMPTY			0x20

/* Line control */
#define TI_LCR_BREAK			0x40

/* Modem control */
#define TI_MCR_LOOP			0x04
#define TI_MCR_DTR			0x10
#define TI_MCR_RTS			0x20

/* Mask settings */
#define TI_UART_ENABLE_RTS_IN		0x0001
#define TI_UART_DISABLE_RTS		0x0002
#define TI_UART_ENABLE_PARITY_CHECKING	0x0008
#define TI_UART_ENABLE_DSR_OUT		0x0010
#define TI_UART_ENABLE_CTS_OUT		0x0020
#define TI_UART_ENABLE_X_OUT		0x0040
#define TI_UART_ENABLE_XA_OUT		0x0080
#define TI_UART_ENABLE_X_IN		0x0100
#define TI_UART_ENABLE_DTR_IN		0x0800
#define TI_UART_DISABLE_DTR		0x1000
#define TI_UART_ENABLE_MS_INTS		0x2000
#define TI_UART_ENABLE_AUTO_START_DMA	0x4000

/* Parity */
#define TI_UART_NO_PARITY		0x00
#define TI_UART_ODD_PARITY		0x01
#define TI_UART_EVEN_PARITY		0x02
#define TI_UART_MARK_PARITY		0x03
#define TI_UART_SPACE_PARITY		0x04

/* Stop bits */
#define TI_UART_1_STOP_BITS		0x00
#define TI_UART_1_5_STOP_BITS		0x01
#define TI_UART_2_STOP_BITS		0x02

/* Bits per character */
#define TI_UART_5_DATA_BITS		0x00
#define TI_UART_6_DATA_BITS		0x01
#define TI_UART_7_DATA_BITS		0x02
#define TI_UART_8_DATA_BITS		0x03

/* 232/485 modes */
#define TI_UART_232			0x00
#define TI_UART_485_RECEIVER_DISABLED	0x01
#define TI_UART_485_RECEIVER_ENABLED	0x02

/* Pipe transfer mode and timeout */
#define TI_PIPE_MODE_CONTINOUS		0x01
#define TI_PIPE_MODE_MASK		0x03
#define TI_PIPE_TIMEOUT_MASK		0x7C
#define TI_PIPE_TIMEOUT_ENABLE		0x80

/* Config struct */
struct ti_uart_config {
	__u16	wBaudRate;
	__u16	wFlags;
	__u8	bDataBits;
	__u8	bParity;
	__u8	bStopBits;
	char	cXon;
	char	cXoff;
	__u8	bUartMode;
} __attribute__((packed));

/* Get port status */
struct ti_port_status {
	__u8	bCmdCode;
	__u8	bModuleId;
	__u8	bErrorCode;
	__u8	bMSR;
	__u8	bLSR;
} __attribute__((packed));

/* Purge modes */
#define TI_PURGE_OUTPUT			0x00
#define TI_PURGE_INPUT			0x80

/* Read/Write data */
#define TI_RW_DATA_ADDR_SFR		0x10
#define TI_RW_DATA_ADDR_IDATA		0x20
#define TI_RW_DATA_ADDR_XDATA		0x30
#define TI_RW_DATA_ADDR_CODE		0x40
#define TI_RW_DATA_ADDR_GPIO		0x50
#define TI_RW_DATA_ADDR_I2C		0x60
#define TI_RW_DATA_ADDR_FLASH		0x70
#define TI_RW_DATA_ADDR_DSP		0x80

#define TI_RW_DATA_UNSPECIFIED		0x00
#define TI_RW_DATA_BYTE			0x01
#define TI_RW_DATA_WORD			0x02
#define TI_RW_DATA_DOUBLE_WORD		0x04

struct ti_write_data_bytes {
	__u8	bAddrType;
	__u8	bDataType;
	__u8	bDataCounter;
	__be16	wBaseAddrHi;
	__be16	wBaseAddrLo;
	__u8	bData[0];
} __attribute__((packed));

struct ti_read_data_request {
	__u8	bAddrType;
	__u8	bDataType;
	__u8	bDataCounter;
	__be16	wBaseAddrHi;
	__be16	wBaseAddrLo;
} __attribute__((packed));

struct ti_read_data_bytes {
	__u8	bCmdCode;
	__u8	bModuleId;
	__u8	bErrorCode;
	__u8	bData[0];
} __attribute__((packed));

/* Interrupt struct */
struct ti_interrupt {
	__u8	bICode;
	__u8	bIInfo;
} __attribute__((packed));

/* Interrupt codes */
#define TI_GET_PORT_FROM_CODE(c)	(((c) >> 4) - 3)
#define TI_GET_FUNC_FROM_CODE(c)	((c) & 0x0f)
#define TI_CODE_HARDWARE_ERROR		0xFF
#define TI_CODE_DATA_ERROR		0x03
#define TI_CODE_MODEM_STATUS		0x04

/* Download firmware max packet size */
#define TI_DOWNLOAD_MAX_PACKET_SIZE	64

/* Firmware image header */
struct ti_firmware_header {
	__le16	wLength;
	__u8	bCheckSum;
} __attribute__((packed));

/* UART addresses */
#define TI_UART1_BASE_ADDR		0xFFA0	/* UART 1 base address */
#define TI_UART2_BASE_ADDR		0xFFB0	/* UART 2 base address */
#define TI_UART_OFFSET_LCR		0x0002	/* UART MCR register offset */
#define TI_UART_OFFSET_MCR		0x0004	/* UART MCR register offset */

#endif /* _TI_3410_5052_H_ */
