/*
 * SPI device spec header file
 *
 * Copyright (C) 1999-2017, Broadcom Corporation
 * 
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 * 
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 * 
 *      Notwithstanding the above, under no circumstances may you combine this
 * software in any way with any other Broadcom software provided under a license
 * other than the GPL, without Broadcom's express prior written consent.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: spid.h 514727 2014-11-12 03:02:48Z $
 */

#ifndef	_SPI_H
#define	_SPI_H

/*
 * Brcm SPI Device Register Map.
 *
 */

typedef volatile struct {
	uint8	config;			/* 0x00, len, endian, clock, speed, polarity, wakeup */
	uint8	response_delay;		/* 0x01, read response delay in bytes (corerev < 3) */
	uint8	status_enable;		/* 0x02, status-enable, intr with status, response_delay
					 * function selection, command/data error check
					 */
	uint8	reset_bp;		/* 0x03, reset on wlan/bt backplane reset (corerev >= 1) */
	uint16	intr_reg;		/* 0x04, Intr status register */
	uint16	intr_en_reg;		/* 0x06, Intr mask register */
	uint32	status_reg;		/* 0x08, RO, Status bits of last spi transfer */
	uint16	f1_info_reg;		/* 0x0c, RO, enabled, ready for data transfer, blocksize */
	uint16	f2_info_reg;		/* 0x0e, RO, enabled, ready for data transfer, blocksize */
	uint16	f3_info_reg;		/* 0x10, RO, enabled, ready for data transfer, blocksize */
	uint32	test_read;		/* 0x14, RO 0xfeedbead signature */
	uint32	test_rw;		/* 0x18, RW */
	uint8	resp_delay_f0;		/* 0x1c, read resp delay bytes for F0 (corerev >= 3) */
	uint8	resp_delay_f1;		/* 0x1d, read resp delay bytes for F1 (corerev >= 3) */
	uint8	resp_delay_f2;		/* 0x1e, read resp delay bytes for F2 (corerev >= 3) */
	uint8	resp_delay_f3;		/* 0x1f, read resp delay bytes for F3 (corerev >= 3) */
} spi_regs_t;

/* SPI device register offsets */
#define SPID_CONFIG			0x00
#define SPID_RESPONSE_DELAY		0x01
#define SPID_STATUS_ENABLE		0x02
#define SPID_RESET_BP			0x03	/* (corerev >= 1) */
#define SPID_INTR_REG			0x04	/* 16 bits - Interrupt status */
#define SPID_INTR_EN_REG		0x06	/* 16 bits - Interrupt mask */
#define SPID_STATUS_REG			0x08	/* 32 bits */
#define SPID_F1_INFO_REG		0x0C	/* 16 bits */
#define SPID_F2_INFO_REG		0x0E	/* 16 bits */
#define SPID_F3_INFO_REG		0x10	/* 16 bits */
#define SPID_TEST_READ			0x14	/* 32 bits */
#define SPID_TEST_RW			0x18	/* 32 bits */
#define SPID_RESP_DELAY_F0		0x1c	/* 8 bits (corerev >= 3) */
#define SPID_RESP_DELAY_F1		0x1d	/* 8 bits (corerev >= 3) */
#define SPID_RESP_DELAY_F2		0x1e	/* 8 bits (corerev >= 3) */
#define SPID_RESP_DELAY_F3		0x1f	/* 8 bits (corerev >= 3) */

/* Bit masks for SPID_CONFIG device register */
#define WORD_LENGTH_32	0x1	/* 0/1 16/32 bit word length */
#define ENDIAN_BIG	0x2	/* 0/1 Little/Big Endian */
#define CLOCK_PHASE	0x4	/* 0/1 clock phase delay */
#define CLOCK_POLARITY	0x8	/* 0/1 Idle state clock polarity is low/high */
#define HIGH_SPEED_MODE	0x10	/* 1/0 High Speed mode / Normal mode */
#define INTR_POLARITY	0x20	/* 1/0 Interrupt active polarity is high/low */
#define WAKE_UP		0x80	/* 0/1 Wake-up command from Host to WLAN */

/* Bit mask for SPID_RESPONSE_DELAY device register */
#define RESPONSE_DELAY_MASK	0xFF	/* Configurable rd response delay in multiples of 8 bits */

/* Bit mask for SPID_STATUS_ENABLE device register */
#define STATUS_ENABLE		0x1	/* 1/0 Status sent/not sent to host after read/write */
#define INTR_WITH_STATUS	0x2	/* 0/1 Do-not / do-interrupt if status is sent */
#define RESP_DELAY_ALL		0x4	/* Applicability of resp delay to F1 or all func's read */
#define DWORD_PKT_LEN_EN	0x8	/* Packet len denoted in dwords instead of bytes */
#define CMD_ERR_CHK_EN		0x20	/* Command error check enable */
#define DATA_ERR_CHK_EN		0x40	/* Data error check enable */

/* Bit mask for SPID_RESET_BP device register */
#define RESET_ON_WLAN_BP_RESET	0x4	/* enable reset for WLAN backplane */
#define RESET_ON_BT_BP_RESET	0x8	/* enable reset for BT backplane */
#define RESET_SPI		0x80	/* reset the above enabled logic */

/* Bit mask for SPID_INTR_REG device register */
#define DATA_UNAVAILABLE	0x0001	/* Requested data not available; Clear by writing a "1" */
#define F2_F3_FIFO_RD_UNDERFLOW	0x0002
#define F2_F3_FIFO_WR_OVERFLOW	0x0004
#define COMMAND_ERROR		0x0008	/* Cleared by writing 1 */
#define DATA_ERROR		0x0010	/* Cleared by writing 1 */
#define F2_PACKET_AVAILABLE	0x0020
#define F3_PACKET_AVAILABLE	0x0040
#define F1_OVERFLOW		0x0080	/* Due to last write. Bkplane has pending write requests */
#define MISC_INTR0		0x0100
#define MISC_INTR1		0x0200
#define MISC_INTR2		0x0400
#define MISC_INTR3		0x0800
#define MISC_INTR4		0x1000
#define F1_INTR			0x2000
#define F2_INTR			0x4000
#define F3_INTR			0x8000

/* Bit mask for 32bit SPID_STATUS_REG device register */
#define STATUS_DATA_NOT_AVAILABLE	0x00000001
#define STATUS_UNDERFLOW		0x00000002
#define STATUS_OVERFLOW			0x00000004
#define STATUS_F2_INTR			0x00000008
#define STATUS_F3_INTR			0x00000010
#define STATUS_F2_RX_READY		0x00000020
#define STATUS_F3_RX_READY		0x00000040
#define STATUS_HOST_CMD_DATA_ERR	0x00000080
#define STATUS_F2_PKT_AVAILABLE		0x00000100
#define STATUS_F2_PKT_LEN_MASK		0x000FFE00
#define STATUS_F2_PKT_LEN_SHIFT		9
#define STATUS_F3_PKT_AVAILABLE		0x00100000
#define STATUS_F3_PKT_LEN_MASK		0xFFE00000
#define STATUS_F3_PKT_LEN_SHIFT		21

/* Bit mask for 16 bits SPID_F1_INFO_REG device register */
#define F1_ENABLED 			0x0001
#define F1_RDY_FOR_DATA_TRANSFER	0x0002
#define F1_MAX_PKT_SIZE			0x01FC

/* Bit mask for 16 bits SPID_F2_INFO_REG device register */
#define F2_ENABLED 			0x0001
#define F2_RDY_FOR_DATA_TRANSFER	0x0002
#define F2_MAX_PKT_SIZE			0x3FFC

/* Bit mask for 16 bits SPID_F3_INFO_REG device register */
#define F3_ENABLED 			0x0001
#define F3_RDY_FOR_DATA_TRANSFER	0x0002
#define F3_MAX_PKT_SIZE			0x3FFC

/* Bit mask for 32 bits SPID_TEST_READ device register read in 16bit LE mode */
#define TEST_RO_DATA_32BIT_LE		0xFEEDBEAD

/* Maximum number of I/O funcs */
#define SPI_MAX_IOFUNCS		4

#define SPI_MAX_PKT_LEN		(2048*4)

/* Misc defines */
#define SPI_FUNC_0		0
#define SPI_FUNC_1		1
#define SPI_FUNC_2		2
#define SPI_FUNC_3		3

#define WAIT_F2RXFIFORDY	100
#define WAIT_F2RXFIFORDY_DELAY	20

#endif /* _SPI_H */
