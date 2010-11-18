/*
 *	linux/drivers/net/wireless/libertas/if_spi.c
 *
 *	Driver for Marvell SPI WLAN cards.
 *
 *	Copyright 2008 Analog Devices Inc.
 *
 *	Authors:
 *	Andrey Yurovsky <andrey@cozybit.com>
 *	Colin McCabe <colin@cozybit.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef _LBS_IF_SPI_H_
#define _LBS_IF_SPI_H_

#define IPFIELD_ALIGN_OFFSET 2
#define IF_SPI_CMD_BUF_SIZE 2400

/***************** Firmware *****************/

#define IF_SPI_FW_NAME_MAX 30

#define MAX_MAIN_FW_LOAD_CRC_ERR 10

/* Chunk size when loading the helper firmware */
#define HELPER_FW_LOAD_CHUNK_SZ 64

/* Value to write to indicate end of helper firmware dnld */
#define FIRMWARE_DNLD_OK 0x0000

/* Value to check once the main firmware is downloaded */
#define SUCCESSFUL_FW_DOWNLOAD_MAGIC 0x88888888

/***************** SPI Interface Unit *****************/
/* Masks used in SPI register read/write operations */
#define IF_SPI_READ_OPERATION_MASK 0x0
#define IF_SPI_WRITE_OPERATION_MASK 0x8000

/* SPI register offsets. 4-byte aligned. */
#define IF_SPI_DEVICEID_CTRL_REG 0x00	/* DeviceID controller reg */
#define IF_SPI_IO_READBASE_REG 0x04 	/* Read I/O base reg */
#define IF_SPI_IO_WRITEBASE_REG 0x08	/* Write I/O base reg */
#define IF_SPI_IO_RDWRPORT_REG 0x0C	/* Read/Write I/O port reg */

#define IF_SPI_CMD_READBASE_REG 0x10	/* Read command base reg */
#define IF_SPI_CMD_WRITEBASE_REG 0x14	/* Write command base reg */
#define IF_SPI_CMD_RDWRPORT_REG 0x18	/* Read/Write command port reg */

#define IF_SPI_DATA_READBASE_REG 0x1C	/* Read data base reg */
#define IF_SPI_DATA_WRITEBASE_REG 0x20	/* Write data base reg */
#define IF_SPI_DATA_RDWRPORT_REG 0x24	/* Read/Write data port reg */

#define IF_SPI_SCRATCH_1_REG 0x28	/* Scratch reg 1 */
#define IF_SPI_SCRATCH_2_REG 0x2C	/* Scratch reg 2 */
#define IF_SPI_SCRATCH_3_REG 0x30	/* Scratch reg 3 */
#define IF_SPI_SCRATCH_4_REG 0x34	/* Scratch reg 4 */

#define IF_SPI_TX_FRAME_SEQ_NUM_REG 0x38 /* Tx frame sequence number reg */
#define IF_SPI_TX_FRAME_STATUS_REG 0x3C	/* Tx frame status reg */

#define IF_SPI_HOST_INT_CTRL_REG 0x40	/* Host interrupt controller reg */

#define IF_SPI_CARD_INT_CAUSE_REG 0x44	/* Card interrupt cause reg */
#define IF_SPI_CARD_INT_STATUS_REG 0x48 /* Card interupt status reg */
#define IF_SPI_CARD_INT_EVENT_MASK_REG 0x4C /* Card interrupt event mask */
#define IF_SPI_CARD_INT_STATUS_MASK_REG	0x50 /* Card interrupt status mask */

#define IF_SPI_CARD_INT_RESET_SELECT_REG 0x54 /* Card interrupt reset select */

#define IF_SPI_HOST_INT_CAUSE_REG 0x58	/* Host interrupt cause reg */
#define IF_SPI_HOST_INT_STATUS_REG 0x5C	/* Host interrupt status reg */
#define IF_SPI_HOST_INT_EVENT_MASK_REG 0x60 /* Host interrupt event mask */
#define IF_SPI_HOST_INT_STATUS_MASK_REG	0x64 /* Host interrupt status mask */
#define IF_SPI_HOST_INT_RESET_SELECT_REG 0x68 /* Host interrupt reset select */

#define IF_SPI_DELAY_READ_REG 0x6C	/* Delay read reg */
#define IF_SPI_SPU_BUS_MODE_REG 0x70	/* SPU BUS mode reg */

/***************** IF_SPI_DEVICEID_CTRL_REG *****************/
#define IF_SPI_DEVICEID_CTRL_REG_TO_CARD_ID(dc) ((dc & 0xffff0000)>>16)
#define IF_SPI_DEVICEID_CTRL_REG_TO_CARD_REV(dc) (dc & 0x000000ff)

/***************** IF_SPI_HOST_INT_CTRL_REG *****************/
/** Host Interrupt Control bit : Wake up */
#define IF_SPI_HICT_WAKE_UP				(1<<0)
/** Host Interrupt Control bit : WLAN ready */
#define IF_SPI_HICT_WLAN_READY				(1<<1)
/*#define IF_SPI_HICT_FIFO_FIRST_HALF_EMPTY		(1<<2) */
/*#define IF_SPI_HICT_FIFO_SECOND_HALF_EMPTY		(1<<3) */
/*#define IF_SPI_HICT_IRQSRC_WLAN			(1<<4) */
/** Host Interrupt Control bit : Tx auto download */
#define IF_SPI_HICT_TX_DOWNLOAD_OVER_AUTO		(1<<5)
/** Host Interrupt Control bit : Rx auto upload */
#define IF_SPI_HICT_RX_UPLOAD_OVER_AUTO			(1<<6)
/** Host Interrupt Control bit : Command auto download */
#define IF_SPI_HICT_CMD_DOWNLOAD_OVER_AUTO		(1<<7)
/** Host Interrupt Control bit : Command auto upload */
#define IF_SPI_HICT_CMD_UPLOAD_OVER_AUTO		(1<<8)

/***************** IF_SPI_CARD_INT_CAUSE_REG *****************/
/** Card Interrupt Case bit : Tx download over */
#define IF_SPI_CIC_TX_DOWNLOAD_OVER			(1<<0)
/** Card Interrupt Case bit : Rx upload over */
#define IF_SPI_CIC_RX_UPLOAD_OVER			(1<<1)
/** Card Interrupt Case bit : Command download over */
#define IF_SPI_CIC_CMD_DOWNLOAD_OVER			(1<<2)
/** Card Interrupt Case bit : Host event */
#define IF_SPI_CIC_HOST_EVENT				(1<<3)
/** Card Interrupt Case bit : Command upload over */
#define IF_SPI_CIC_CMD_UPLOAD_OVER			(1<<4)
/** Card Interrupt Case bit : Power down */
#define IF_SPI_CIC_POWER_DOWN				(1<<5)

/***************** IF_SPI_CARD_INT_STATUS_REG *****************/
#define IF_SPI_CIS_TX_DOWNLOAD_OVER			(1<<0)
#define IF_SPI_CIS_RX_UPLOAD_OVER			(1<<1)
#define IF_SPI_CIS_CMD_DOWNLOAD_OVER			(1<<2)
#define IF_SPI_CIS_HOST_EVENT				(1<<3)
#define IF_SPI_CIS_CMD_UPLOAD_OVER			(1<<4)
#define IF_SPI_CIS_POWER_DOWN				(1<<5)

/***************** IF_SPI_HOST_INT_CAUSE_REG *****************/
#define IF_SPI_HICU_TX_DOWNLOAD_RDY			(1<<0)
#define IF_SPI_HICU_RX_UPLOAD_RDY			(1<<1)
#define IF_SPI_HICU_CMD_DOWNLOAD_RDY			(1<<2)
#define IF_SPI_HICU_CARD_EVENT				(1<<3)
#define IF_SPI_HICU_CMD_UPLOAD_RDY			(1<<4)
#define IF_SPI_HICU_IO_WR_FIFO_OVERFLOW			(1<<5)
#define IF_SPI_HICU_IO_RD_FIFO_UNDERFLOW		(1<<6)
#define IF_SPI_HICU_DATA_WR_FIFO_OVERFLOW		(1<<7)
#define IF_SPI_HICU_DATA_RD_FIFO_UNDERFLOW		(1<<8)
#define IF_SPI_HICU_CMD_WR_FIFO_OVERFLOW		(1<<9)
#define IF_SPI_HICU_CMD_RD_FIFO_UNDERFLOW		(1<<10)

/***************** IF_SPI_HOST_INT_STATUS_REG *****************/
/** Host Interrupt Status bit : Tx download ready */
#define IF_SPI_HIST_TX_DOWNLOAD_RDY			(1<<0)
/** Host Interrupt Status bit : Rx upload ready */
#define IF_SPI_HIST_RX_UPLOAD_RDY			(1<<1)
/** Host Interrupt Status bit : Command download ready */
#define IF_SPI_HIST_CMD_DOWNLOAD_RDY			(1<<2)
/** Host Interrupt Status bit : Card event */
#define IF_SPI_HIST_CARD_EVENT				(1<<3)
/** Host Interrupt Status bit : Command upload ready */
#define IF_SPI_HIST_CMD_UPLOAD_RDY			(1<<4)
/** Host Interrupt Status bit : I/O write FIFO overflow */
#define IF_SPI_HIST_IO_WR_FIFO_OVERFLOW			(1<<5)
/** Host Interrupt Status bit : I/O read FIFO underflow */
#define IF_SPI_HIST_IO_RD_FIFO_UNDRFLOW			(1<<6)
/** Host Interrupt Status bit : Data write FIFO overflow */
#define IF_SPI_HIST_DATA_WR_FIFO_OVERFLOW		(1<<7)
/** Host Interrupt Status bit : Data read FIFO underflow */
#define IF_SPI_HIST_DATA_RD_FIFO_UNDERFLOW		(1<<8)
/** Host Interrupt Status bit : Command write FIFO overflow */
#define IF_SPI_HIST_CMD_WR_FIFO_OVERFLOW		(1<<9)
/** Host Interrupt Status bit : Command read FIFO underflow */
#define IF_SPI_HIST_CMD_RD_FIFO_UNDERFLOW		(1<<10)

/***************** IF_SPI_HOST_INT_STATUS_MASK_REG *****************/
/** Host Interrupt Status Mask bit : Tx download ready */
#define IF_SPI_HISM_TX_DOWNLOAD_RDY			(1<<0)
/** Host Interrupt Status Mask bit : Rx upload ready */
#define IF_SPI_HISM_RX_UPLOAD_RDY			(1<<1)
/** Host Interrupt Status Mask bit : Command download ready */
#define IF_SPI_HISM_CMD_DOWNLOAD_RDY			(1<<2)
/** Host Interrupt Status Mask bit : Card event */
#define IF_SPI_HISM_CARDEVENT				(1<<3)
/** Host Interrupt Status Mask bit : Command upload ready */
#define IF_SPI_HISM_CMD_UPLOAD_RDY			(1<<4)
/** Host Interrupt Status Mask bit : I/O write FIFO overflow */
#define IF_SPI_HISM_IO_WR_FIFO_OVERFLOW			(1<<5)
/** Host Interrupt Status Mask bit : I/O read FIFO underflow */
#define IF_SPI_HISM_IO_RD_FIFO_UNDERFLOW		(1<<6)
/** Host Interrupt Status Mask bit : Data write FIFO overflow */
#define IF_SPI_HISM_DATA_WR_FIFO_OVERFLOW		(1<<7)
/** Host Interrupt Status Mask bit : Data write FIFO underflow */
#define IF_SPI_HISM_DATA_RD_FIFO_UNDERFLOW		(1<<8)
/** Host Interrupt Status Mask bit : Command write FIFO overflow */
#define IF_SPI_HISM_CMD_WR_FIFO_OVERFLOW		(1<<9)
/** Host Interrupt Status Mask bit : Command write FIFO underflow */
#define IF_SPI_HISM_CMD_RD_FIFO_UNDERFLOW		(1<<10)

/***************** IF_SPI_SPU_BUS_MODE_REG *****************/
/* SCK edge on which the WLAN module outputs data on MISO */
#define IF_SPI_BUS_MODE_SPI_CLOCK_PHASE_FALLING 0x8
#define IF_SPI_BUS_MODE_SPI_CLOCK_PHASE_RISING 0x0

/* In a SPU read operation, there is a delay between writing the SPU
 * register name and getting back data from the WLAN module.
 * This can be specified in terms of nanoseconds or in terms of dummy
 * clock cycles which the master must output before receiving a response. */
#define IF_SPI_BUS_MODE_DELAY_METHOD_DUMMY_CLOCK 0x4
#define IF_SPI_BUS_MODE_DELAY_METHOD_TIMED 0x0

/* Some different modes of SPI operation */
#define IF_SPI_BUS_MODE_8_BIT_ADDRESS_16_BIT_DATA 0x00
#define IF_SPI_BUS_MODE_8_BIT_ADDRESS_32_BIT_DATA 0x01
#define IF_SPI_BUS_MODE_16_BIT_ADDRESS_16_BIT_DATA 0x02
#define IF_SPI_BUS_MODE_16_BIT_ADDRESS_32_BIT_DATA 0x03

#endif
