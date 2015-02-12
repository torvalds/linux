/*
 * SD-SPI Protocol Standard
 *
 * $ Copyright Open Broadcom Corporation $
 *
 * $Id: sdspi.h 382882 2013-02-04 23:24:31Z $
 */
#ifndef	_SD_SPI_H
#define	_SD_SPI_H

#define SPI_START_M		BITFIELD_MASK(1)	/* Bit [31] 	- Start Bit */
#define SPI_START_S		31
#define SPI_DIR_M		BITFIELD_MASK(1)	/* Bit [30] 	- Direction */
#define SPI_DIR_S		30
#define SPI_CMD_INDEX_M		BITFIELD_MASK(6)	/* Bits [29:24] - Command number */
#define SPI_CMD_INDEX_S		24
#define SPI_RW_M		BITFIELD_MASK(1)	/* Bit [23] 	- Read=0, Write=1 */
#define SPI_RW_S		23
#define SPI_FUNC_M		BITFIELD_MASK(3)	/* Bits [22:20]	- Function Number */
#define SPI_FUNC_S		20
#define SPI_RAW_M		BITFIELD_MASK(1)	/* Bit [19] 	- Read After Wr */
#define SPI_RAW_S		19
#define SPI_STUFF_M		BITFIELD_MASK(1)	/* Bit [18] 	- Stuff bit */
#define SPI_STUFF_S		18
#define SPI_BLKMODE_M		BITFIELD_MASK(1)	/* Bit [19] 	- Blockmode 1=blk */
#define SPI_BLKMODE_S		19
#define SPI_OPCODE_M		BITFIELD_MASK(1)	/* Bit [18] 	- OP Code */
#define SPI_OPCODE_S		18
#define SPI_ADDR_M		BITFIELD_MASK(17)	/* Bits [17:1] 	- Address */
#define SPI_ADDR_S		1
#define SPI_STUFF0_M		BITFIELD_MASK(1)	/* Bit [0] 	- Stuff bit */
#define SPI_STUFF0_S		0

#define SPI_RSP_START_M		BITFIELD_MASK(1)	/* Bit [7] 	- Start Bit (always 0) */
#define SPI_RSP_START_S		7
#define SPI_RSP_PARAM_ERR_M	BITFIELD_MASK(1)	/* Bit [6] 	- Parameter Error */
#define SPI_RSP_PARAM_ERR_S	6
#define SPI_RSP_RFU5_M		BITFIELD_MASK(1)	/* Bit [5] 	- RFU (Always 0) */
#define SPI_RSP_RFU5_S		5
#define SPI_RSP_FUNC_ERR_M	BITFIELD_MASK(1)	/* Bit [4] 	- Function number error */
#define SPI_RSP_FUNC_ERR_S	4
#define SPI_RSP_CRC_ERR_M	BITFIELD_MASK(1)	/* Bit [3] 	- COM CRC Error */
#define SPI_RSP_CRC_ERR_S	3
#define SPI_RSP_ILL_CMD_M	BITFIELD_MASK(1)	/* Bit [2] 	- Illegal Command error */
#define SPI_RSP_ILL_CMD_S	2
#define SPI_RSP_RFU1_M		BITFIELD_MASK(1)	/* Bit [1] 	- RFU (Always 0) */
#define SPI_RSP_RFU1_S		1
#define SPI_RSP_IDLE_M		BITFIELD_MASK(1)	/* Bit [0] 	- In idle state */
#define SPI_RSP_IDLE_S		0

/* SD-SPI Protocol Definitions */
#define SDSPI_COMMAND_LEN	6	/* Number of bytes in an SD command */
#define SDSPI_START_BLOCK	0xFE	/* SD Start Block Token */
#define SDSPI_IDLE_PAD		0xFF	/* SD-SPI idle value for MOSI */
#define SDSPI_START_BIT_MASK	0x80

#endif /* _SD_SPI_H */
