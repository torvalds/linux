/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2009-2011 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */


#ifndef MCDI_PCOL_H
#define MCDI_PCOL_H

/* Values to be written into FMCR_CZ_RESET_STATE_REG to control boot. */
/* Power-on reset state */
#define MC_FW_STATE_POR (1)
/* If this is set in MC_RESET_STATE_REG then it should be
 * possible to jump into IMEM without loading code from flash. */
#define MC_FW_WARM_BOOT_OK (2)
/* The MC main image has started to boot. */
#define MC_FW_STATE_BOOTING (4)
/* The Scheduler has started. */
#define MC_FW_STATE_SCHED (8)

/* Siena MC shared memmory offsets */
/* The 'doorbell' addresses are hard-wired to alert the MC when written */
#define	MC_SMEM_P0_DOORBELL_OFST	0x000
#define	MC_SMEM_P1_DOORBELL_OFST	0x004
/* The rest of these are firmware-defined */
#define	MC_SMEM_P0_PDU_OFST		0x008
#define	MC_SMEM_P1_PDU_OFST		0x108
#define	MC_SMEM_PDU_LEN			0x100
#define	MC_SMEM_P0_PTP_TIME_OFST	0x7f0
#define	MC_SMEM_P0_STATUS_OFST		0x7f8
#define	MC_SMEM_P1_STATUS_OFST		0x7fc

/* Values to be written to the per-port status dword in shared
 * memory on reboot and assert */
#define MC_STATUS_DWORD_REBOOT (0xb007b007)
#define MC_STATUS_DWORD_ASSERT (0xdeaddead)

/* The current version of the MCDI protocol.
 *
 * Note that the ROM burnt into the card only talks V0, so at the very
 * least every driver must support version 0 and MCDI_PCOL_VERSION
 */
#define MCDI_PCOL_VERSION 1

/* Unused commands: 0x23, 0x27, 0x30, 0x31 */

/* MCDI version 1
 *
 * Each MCDI request starts with an MCDI_HEADER, which is a 32byte
 * structure, filled in by the client.
 *
 *       0       7  8     16    20     22  23  24    31
 *      | CODE | R | LEN | SEQ | Rsvd | E | R | XFLAGS |
 *               |                      |   |
 *               |                      |   \--- Response
 *               |                      \------- Error
 *               \------------------------------ Resync (always set)
 *
 * The client writes it's request into MC shared memory, and rings the
 * doorbell. Each request is completed by either by the MC writting
 * back into shared memory, or by writting out an event.
 *
 * All MCDI commands support completion by shared memory response. Each
 * request may also contain additional data (accounted for by HEADER.LEN),
 * and some response's may also contain additional data (again, accounted
 * for by HEADER.LEN).
 *
 * Some MCDI commands support completion by event, in which any associated
 * response data is included in the event.
 *
 * The protocol requires one response to be delivered for every request, a
 * request should not be sent unless the response for the previous request
 * has been received (either by polling shared memory, or by receiving
 * an event).
 */

/** Request/Response structure */
#define MCDI_HEADER_OFST 0
#define MCDI_HEADER_CODE_LBN 0
#define MCDI_HEADER_CODE_WIDTH 7
#define MCDI_HEADER_RESYNC_LBN 7
#define MCDI_HEADER_RESYNC_WIDTH 1
#define MCDI_HEADER_DATALEN_LBN 8
#define MCDI_HEADER_DATALEN_WIDTH 8
#define MCDI_HEADER_SEQ_LBN 16
#define MCDI_HEADER_RSVD_LBN 20
#define MCDI_HEADER_RSVD_WIDTH 2
#define MCDI_HEADER_SEQ_WIDTH 4
#define MCDI_HEADER_ERROR_LBN 22
#define MCDI_HEADER_ERROR_WIDTH 1
#define MCDI_HEADER_RESPONSE_LBN 23
#define MCDI_HEADER_RESPONSE_WIDTH 1
#define MCDI_HEADER_XFLAGS_LBN 24
#define MCDI_HEADER_XFLAGS_WIDTH 8
/* Request response using event */
#define MCDI_HEADER_XFLAGS_EVREQ 0x01

/* Maximum number of payload bytes */
#define MCDI_CTL_SDU_LEN_MAX 0xfc

/* The MC can generate events for two reasons:
 *   - To complete a shared memory request if XFLAGS_EVREQ was set
 *   - As a notification (link state, i2c event), controlled
 *     via MC_CMD_LOG_CTRL
 *
 * Both events share a common structure:
 *
 *  0      32     33      36    44     52     60
 * | Data | Cont | Level | Src | Code | Rsvd |
 *           |
 *           \ There is another event pending in this notification
 *
 * If Code==CMDDONE, then the fields are further interpreted as:
 *
 *   - LEVEL==INFO    Command succeeded
 *   - LEVEL==ERR     Command failed
 *
 *    0     8         16      24     32
 *   | Seq | Datalen | Errno | Rsvd |
 *
 *   These fields are taken directly out of the standard MCDI header, i.e.,
 *   LEVEL==ERR, Datalen == 0 => Reboot
 *
 * Events can be squirted out of the UART (using LOG_CTRL) without a
 * MCDI header.  An event can be distinguished from a MCDI response by
 * examining the first byte which is 0xc0.  This corresponds to the
 * non-existent MCDI command MC_CMD_DEBUG_LOG.
 *
 *      0         7        8
 *     | command | Resync |     = 0xc0
 *
 * Since the event is written in big-endian byte order, this works
 * providing bits 56-63 of the event are 0xc0.
 *
 *      56     60  63
 *     | Rsvd | Code |    = 0xc0
 *
 * Which means for convenience the event code is 0xc for all MC
 * generated events.
 */
#define FSE_AZ_EV_CODE_MCDI_EVRESPONSE 0xc


/* Non-existent command target */
#define MC_CMD_ERR_ENOENT 2
/* assert() has killed the MC */
#define MC_CMD_ERR_EINTR 4
/* Caller does not hold required locks */
#define MC_CMD_ERR_EACCES 13
/* Resource is currently unavailable (e.g. lock contention) */
#define MC_CMD_ERR_EBUSY 16
/* Invalid argument to target */
#define MC_CMD_ERR_EINVAL 22
/* Non-recursive resource is already acquired */
#define MC_CMD_ERR_EDEADLK 35
/* Operation not implemented */
#define MC_CMD_ERR_ENOSYS 38
/* Operation timed out */
#define MC_CMD_ERR_ETIME 62

#define MC_CMD_ERR_CODE_OFST 0

/* We define 8 "escape" commands to allow
   for command number space extension */

#define MC_CMD_CMD_SPACE_ESCAPE_0	      0x78
#define MC_CMD_CMD_SPACE_ESCAPE_1	      0x79
#define MC_CMD_CMD_SPACE_ESCAPE_2	      0x7A
#define MC_CMD_CMD_SPACE_ESCAPE_3	      0x7B
#define MC_CMD_CMD_SPACE_ESCAPE_4	      0x7C
#define MC_CMD_CMD_SPACE_ESCAPE_5	      0x7D
#define MC_CMD_CMD_SPACE_ESCAPE_6	      0x7E
#define MC_CMD_CMD_SPACE_ESCAPE_7	      0x7F

/* Vectors in the boot ROM */
/* Point to the copycode entry point. */
#define MC_BOOTROM_COPYCODE_VEC (0x7f4)
/* Points to the recovery mode entry point. */
#define MC_BOOTROM_NOFLASH_VEC (0x7f8)

/* The command set exported by the boot ROM (MCDI v0) */
#define MC_CMD_GET_VERSION_V0_SUPPORTED_FUNCS {		\
	(1 << MC_CMD_READ32)	|			\
	(1 << MC_CMD_WRITE32)	|			\
	(1 << MC_CMD_COPYCODE)	|			\
	(1 << MC_CMD_GET_VERSION),			\
	0, 0, 0 }

#define MC_CMD_SENSOR_INFO_OUT_OFFSET_OFST(_x)		\
	(MC_CMD_SENSOR_ENTRY_OFST + (_x))

#define MC_CMD_DBI_WRITE_IN_ADDRESS_OFST(n)		\
	(MC_CMD_DBI_WRITE_IN_DBIWROP_OFST +		\
	 MC_CMD_DBIWROP_TYPEDEF_ADDRESS_OFST +		\
	 (n) * MC_CMD_DBIWROP_TYPEDEF_LEN)

#define MC_CMD_DBI_WRITE_IN_BYTE_MASK_OFST(n)		\
	(MC_CMD_DBI_WRITE_IN_DBIWROP_OFST +		\
	 MC_CMD_DBIWROP_TYPEDEF_BYTE_MASK_OFST +	\
	 (n) * MC_CMD_DBIWROP_TYPEDEF_LEN)

#define MC_CMD_DBI_WRITE_IN_VALUE_OFST(n)		\
	(MC_CMD_DBI_WRITE_IN_DBIWROP_OFST +		\
	 MC_CMD_DBIWROP_TYPEDEF_VALUE_OFST +		\
	 (n) * MC_CMD_DBIWROP_TYPEDEF_LEN)


/* MCDI_EVENT structuredef */
#define    MCDI_EVENT_LEN 8
#define       MCDI_EVENT_CONT_LBN 32
#define       MCDI_EVENT_CONT_WIDTH 1
#define       MCDI_EVENT_LEVEL_LBN 33
#define       MCDI_EVENT_LEVEL_WIDTH 3
#define          MCDI_EVENT_LEVEL_INFO  0x0 /* enum */
#define          MCDI_EVENT_LEVEL_WARN 0x1 /* enum */
#define          MCDI_EVENT_LEVEL_ERR 0x2 /* enum */
#define          MCDI_EVENT_LEVEL_FATAL 0x3 /* enum */
#define       MCDI_EVENT_DATA_OFST 0
#define        MCDI_EVENT_CMDDONE_SEQ_LBN 0
#define        MCDI_EVENT_CMDDONE_SEQ_WIDTH 8
#define        MCDI_EVENT_CMDDONE_DATALEN_LBN 8
#define        MCDI_EVENT_CMDDONE_DATALEN_WIDTH 8
#define        MCDI_EVENT_CMDDONE_ERRNO_LBN 16
#define        MCDI_EVENT_CMDDONE_ERRNO_WIDTH 8
#define        MCDI_EVENT_LINKCHANGE_LP_CAP_LBN 0
#define        MCDI_EVENT_LINKCHANGE_LP_CAP_WIDTH 16
#define        MCDI_EVENT_LINKCHANGE_SPEED_LBN 16
#define        MCDI_EVENT_LINKCHANGE_SPEED_WIDTH 4
#define          MCDI_EVENT_LINKCHANGE_SPEED_100M  0x1 /* enum */
#define          MCDI_EVENT_LINKCHANGE_SPEED_1G  0x2 /* enum */
#define          MCDI_EVENT_LINKCHANGE_SPEED_10G  0x3 /* enum */
#define        MCDI_EVENT_LINKCHANGE_FCNTL_LBN 20
#define        MCDI_EVENT_LINKCHANGE_FCNTL_WIDTH 4
#define        MCDI_EVENT_LINKCHANGE_LINK_FLAGS_LBN 24
#define        MCDI_EVENT_LINKCHANGE_LINK_FLAGS_WIDTH 8
#define        MCDI_EVENT_SENSOREVT_MONITOR_LBN 0
#define        MCDI_EVENT_SENSOREVT_MONITOR_WIDTH 8
#define        MCDI_EVENT_SENSOREVT_STATE_LBN 8
#define        MCDI_EVENT_SENSOREVT_STATE_WIDTH 8
#define        MCDI_EVENT_SENSOREVT_VALUE_LBN 16
#define        MCDI_EVENT_SENSOREVT_VALUE_WIDTH 16
#define        MCDI_EVENT_FWALERT_DATA_LBN 8
#define        MCDI_EVENT_FWALERT_DATA_WIDTH 24
#define        MCDI_EVENT_FWALERT_REASON_LBN 0
#define        MCDI_EVENT_FWALERT_REASON_WIDTH 8
#define          MCDI_EVENT_FWALERT_REASON_SRAM_ACCESS 0x1 /* enum */
#define        MCDI_EVENT_FLR_VF_LBN 0
#define        MCDI_EVENT_FLR_VF_WIDTH 8
#define        MCDI_EVENT_TX_ERR_TXQ_LBN 0
#define        MCDI_EVENT_TX_ERR_TXQ_WIDTH 12
#define        MCDI_EVENT_TX_ERR_TYPE_LBN 12
#define        MCDI_EVENT_TX_ERR_TYPE_WIDTH 4
#define          MCDI_EVENT_TX_ERR_DL_FAIL 0x1 /* enum */
#define          MCDI_EVENT_TX_ERR_NO_EOP 0x2 /* enum */
#define          MCDI_EVENT_TX_ERR_2BIG 0x3 /* enum */
#define        MCDI_EVENT_TX_ERR_INFO_LBN 16
#define        MCDI_EVENT_TX_ERR_INFO_WIDTH 16
#define        MCDI_EVENT_TX_FLUSH_TXQ_LBN 0
#define        MCDI_EVENT_TX_FLUSH_TXQ_WIDTH 12
#define        MCDI_EVENT_PTP_ERR_TYPE_LBN 0
#define        MCDI_EVENT_PTP_ERR_TYPE_WIDTH 8
#define          MCDI_EVENT_PTP_ERR_PLL_LOST 0x1 /* enum */
#define          MCDI_EVENT_PTP_ERR_FILTER 0x2 /* enum */
#define          MCDI_EVENT_PTP_ERR_FIFO 0x3 /* enum */
#define          MCDI_EVENT_PTP_ERR_QUEUE 0x4 /* enum */
#define       MCDI_EVENT_DATA_LBN 0
#define       MCDI_EVENT_DATA_WIDTH 32
#define       MCDI_EVENT_SRC_LBN 36
#define       MCDI_EVENT_SRC_WIDTH 8
#define       MCDI_EVENT_EV_CODE_LBN 60
#define       MCDI_EVENT_EV_CODE_WIDTH 4
#define       MCDI_EVENT_CODE_LBN 44
#define       MCDI_EVENT_CODE_WIDTH 8
#define          MCDI_EVENT_CODE_BADSSERT 0x1 /* enum */
#define          MCDI_EVENT_CODE_PMNOTICE 0x2 /* enum */
#define          MCDI_EVENT_CODE_CMDDONE 0x3 /* enum */
#define          MCDI_EVENT_CODE_LINKCHANGE 0x4 /* enum */
#define          MCDI_EVENT_CODE_SENSOREVT 0x5 /* enum */
#define          MCDI_EVENT_CODE_SCHEDERR 0x6 /* enum */
#define          MCDI_EVENT_CODE_REBOOT 0x7 /* enum */
#define          MCDI_EVENT_CODE_MAC_STATS_DMA 0x8 /* enum */
#define          MCDI_EVENT_CODE_FWALERT 0x9 /* enum */
#define          MCDI_EVENT_CODE_FLR 0xa /* enum */
#define          MCDI_EVENT_CODE_TX_ERR 0xb /* enum */
#define          MCDI_EVENT_CODE_TX_FLUSH  0xc /* enum */
#define          MCDI_EVENT_CODE_PTP_RX  0xd /* enum */
#define          MCDI_EVENT_CODE_PTP_FAULT  0xe /* enum */
#define          MCDI_EVENT_CODE_PTP_PPS  0xf /* enum */
#define       MCDI_EVENT_CMDDONE_DATA_OFST 0
#define       MCDI_EVENT_CMDDONE_DATA_LBN 0
#define       MCDI_EVENT_CMDDONE_DATA_WIDTH 32
#define       MCDI_EVENT_LINKCHANGE_DATA_OFST 0
#define       MCDI_EVENT_LINKCHANGE_DATA_LBN 0
#define       MCDI_EVENT_LINKCHANGE_DATA_WIDTH 32
#define       MCDI_EVENT_SENSOREVT_DATA_OFST 0
#define       MCDI_EVENT_SENSOREVT_DATA_LBN 0
#define       MCDI_EVENT_SENSOREVT_DATA_WIDTH 32
#define       MCDI_EVENT_MAC_STATS_DMA_GENERATION_OFST 0
#define       MCDI_EVENT_MAC_STATS_DMA_GENERATION_LBN 0
#define       MCDI_EVENT_MAC_STATS_DMA_GENERATION_WIDTH 32
#define       MCDI_EVENT_TX_ERR_DATA_OFST 0
#define       MCDI_EVENT_TX_ERR_DATA_LBN 0
#define       MCDI_EVENT_TX_ERR_DATA_WIDTH 32
#define       MCDI_EVENT_PTP_SECONDS_OFST 0
#define       MCDI_EVENT_PTP_SECONDS_LBN 0
#define       MCDI_EVENT_PTP_SECONDS_WIDTH 32
#define       MCDI_EVENT_PTP_NANOSECONDS_OFST 0
#define       MCDI_EVENT_PTP_NANOSECONDS_LBN 0
#define       MCDI_EVENT_PTP_NANOSECONDS_WIDTH 32
#define       MCDI_EVENT_PTP_UUID_OFST 0
#define       MCDI_EVENT_PTP_UUID_LBN 0
#define       MCDI_EVENT_PTP_UUID_WIDTH 32


/***********************************/
/* MC_CMD_READ32
 * Read multiple 32byte words from MC memory.
 */
#define MC_CMD_READ32 0x1

/* MC_CMD_READ32_IN msgrequest */
#define    MC_CMD_READ32_IN_LEN 8
#define       MC_CMD_READ32_IN_ADDR_OFST 0
#define       MC_CMD_READ32_IN_NUMWORDS_OFST 4

/* MC_CMD_READ32_OUT msgresponse */
#define    MC_CMD_READ32_OUT_LENMIN 4
#define    MC_CMD_READ32_OUT_LENMAX 252
#define    MC_CMD_READ32_OUT_LEN(num) (0+4*(num))
#define       MC_CMD_READ32_OUT_BUFFER_OFST 0
#define       MC_CMD_READ32_OUT_BUFFER_LEN 4
#define       MC_CMD_READ32_OUT_BUFFER_MINNUM 1
#define       MC_CMD_READ32_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_WRITE32
 * Write multiple 32byte words to MC memory.
 */
#define MC_CMD_WRITE32 0x2

/* MC_CMD_WRITE32_IN msgrequest */
#define    MC_CMD_WRITE32_IN_LENMIN 8
#define    MC_CMD_WRITE32_IN_LENMAX 252
#define    MC_CMD_WRITE32_IN_LEN(num) (4+4*(num))
#define       MC_CMD_WRITE32_IN_ADDR_OFST 0
#define       MC_CMD_WRITE32_IN_BUFFER_OFST 4
#define       MC_CMD_WRITE32_IN_BUFFER_LEN 4
#define       MC_CMD_WRITE32_IN_BUFFER_MINNUM 1
#define       MC_CMD_WRITE32_IN_BUFFER_MAXNUM 62

/* MC_CMD_WRITE32_OUT msgresponse */
#define    MC_CMD_WRITE32_OUT_LEN 0


/***********************************/
/* MC_CMD_COPYCODE
 * Copy MC code between two locations and jump.
 */
#define MC_CMD_COPYCODE 0x3

/* MC_CMD_COPYCODE_IN msgrequest */
#define    MC_CMD_COPYCODE_IN_LEN 16
#define       MC_CMD_COPYCODE_IN_SRC_ADDR_OFST 0
#define       MC_CMD_COPYCODE_IN_DEST_ADDR_OFST 4
#define       MC_CMD_COPYCODE_IN_NUMWORDS_OFST 8
#define       MC_CMD_COPYCODE_IN_JUMP_OFST 12
#define          MC_CMD_COPYCODE_JUMP_NONE 0x1 /* enum */

/* MC_CMD_COPYCODE_OUT msgresponse */
#define    MC_CMD_COPYCODE_OUT_LEN 0


/***********************************/
/* MC_CMD_SET_FUNC
 */
#define MC_CMD_SET_FUNC 0x4

/* MC_CMD_SET_FUNC_IN msgrequest */
#define    MC_CMD_SET_FUNC_IN_LEN 4
#define       MC_CMD_SET_FUNC_IN_FUNC_OFST 0

/* MC_CMD_SET_FUNC_OUT msgresponse */
#define    MC_CMD_SET_FUNC_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_BOOT_STATUS
 */
#define MC_CMD_GET_BOOT_STATUS 0x5

/* MC_CMD_GET_BOOT_STATUS_IN msgrequest */
#define    MC_CMD_GET_BOOT_STATUS_IN_LEN 0

/* MC_CMD_GET_BOOT_STATUS_OUT msgresponse */
#define    MC_CMD_GET_BOOT_STATUS_OUT_LEN 8
#define       MC_CMD_GET_BOOT_STATUS_OUT_BOOT_OFFSET_OFST 0
#define       MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_OFST 4
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_WATCHDOG_LBN 0
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_WATCHDOG_WIDTH 1
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_PRIMARY_LBN 1
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_PRIMARY_WIDTH 1
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_BACKUP_LBN 2
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_BACKUP_WIDTH 1


/***********************************/
/* MC_CMD_GET_ASSERTS
 * Get and clear any assertion status.
 */
#define MC_CMD_GET_ASSERTS 0x6

/* MC_CMD_GET_ASSERTS_IN msgrequest */
#define    MC_CMD_GET_ASSERTS_IN_LEN 4
#define       MC_CMD_GET_ASSERTS_IN_CLEAR_OFST 0

/* MC_CMD_GET_ASSERTS_OUT msgresponse */
#define    MC_CMD_GET_ASSERTS_OUT_LEN 140
#define       MC_CMD_GET_ASSERTS_OUT_GLOBAL_FLAGS_OFST 0
#define          MC_CMD_GET_ASSERTS_FLAGS_NO_FAILS 0x1 /* enum */
#define          MC_CMD_GET_ASSERTS_FLAGS_SYS_FAIL 0x2 /* enum */
#define          MC_CMD_GET_ASSERTS_FLAGS_THR_FAIL 0x3 /* enum */
#define          MC_CMD_GET_ASSERTS_FLAGS_WDOG_FIRED 0x4 /* enum */
#define       MC_CMD_GET_ASSERTS_OUT_SAVED_PC_OFFS_OFST 4
#define       MC_CMD_GET_ASSERTS_OUT_GP_REGS_OFFS_OFST 8
#define       MC_CMD_GET_ASSERTS_OUT_GP_REGS_OFFS_LEN 4
#define       MC_CMD_GET_ASSERTS_OUT_GP_REGS_OFFS_NUM 31
#define       MC_CMD_GET_ASSERTS_OUT_THREAD_OFFS_OFST 132
#define       MC_CMD_GET_ASSERTS_OUT_RESERVED_OFST 136


/***********************************/
/* MC_CMD_LOG_CTRL
 * Configure the output stream for various events and messages.
 */
#define MC_CMD_LOG_CTRL 0x7

/* MC_CMD_LOG_CTRL_IN msgrequest */
#define    MC_CMD_LOG_CTRL_IN_LEN 8
#define       MC_CMD_LOG_CTRL_IN_LOG_DEST_OFST 0
#define          MC_CMD_LOG_CTRL_IN_LOG_DEST_UART 0x1 /* enum */
#define          MC_CMD_LOG_CTRL_IN_LOG_DEST_EVQ 0x2 /* enum */
#define       MC_CMD_LOG_CTRL_IN_LOG_DEST_EVQ_OFST 4

/* MC_CMD_LOG_CTRL_OUT msgresponse */
#define    MC_CMD_LOG_CTRL_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_VERSION
 * Get version information about the MC firmware.
 */
#define MC_CMD_GET_VERSION 0x8

/* MC_CMD_GET_VERSION_IN msgrequest */
#define    MC_CMD_GET_VERSION_IN_LEN 0

/* MC_CMD_GET_VERSION_V0_OUT msgresponse */
#define    MC_CMD_GET_VERSION_V0_OUT_LEN 4
#define       MC_CMD_GET_VERSION_OUT_FIRMWARE_OFST 0
#define          MC_CMD_GET_VERSION_OUT_FIRMWARE_ANY 0xffffffff /* enum */
#define          MC_CMD_GET_VERSION_OUT_FIRMWARE_BOOTROM 0xb0070000 /* enum */

/* MC_CMD_GET_VERSION_OUT msgresponse */
#define    MC_CMD_GET_VERSION_OUT_LEN 32
/*            MC_CMD_GET_VERSION_OUT_FIRMWARE_OFST 0 */
/*            Enum values, see field(s): */
/*               MC_CMD_GET_VERSION_V0_OUT/MC_CMD_GET_VERSION_OUT_FIRMWARE */
#define       MC_CMD_GET_VERSION_OUT_PCOL_OFST 4
#define       MC_CMD_GET_VERSION_OUT_SUPPORTED_FUNCS_OFST 8
#define       MC_CMD_GET_VERSION_OUT_SUPPORTED_FUNCS_LEN 16
#define       MC_CMD_GET_VERSION_OUT_VERSION_OFST 24
#define       MC_CMD_GET_VERSION_OUT_VERSION_LEN 8
#define       MC_CMD_GET_VERSION_OUT_VERSION_LO_OFST 24
#define       MC_CMD_GET_VERSION_OUT_VERSION_HI_OFST 28


/***********************************/
/* MC_CMD_GET_FPGAREG
 * Read multiple bytes from PTP FPGA.
 */
#define MC_CMD_GET_FPGAREG 0x9

/* MC_CMD_GET_FPGAREG_IN msgrequest */
#define    MC_CMD_GET_FPGAREG_IN_LEN 8
#define       MC_CMD_GET_FPGAREG_IN_ADDR_OFST 0
#define       MC_CMD_GET_FPGAREG_IN_NUMBYTES_OFST 4

/* MC_CMD_GET_FPGAREG_OUT msgresponse */
#define    MC_CMD_GET_FPGAREG_OUT_LENMIN 1
#define    MC_CMD_GET_FPGAREG_OUT_LENMAX 252
#define    MC_CMD_GET_FPGAREG_OUT_LEN(num) (0+1*(num))
#define       MC_CMD_GET_FPGAREG_OUT_BUFFER_OFST 0
#define       MC_CMD_GET_FPGAREG_OUT_BUFFER_LEN 1
#define       MC_CMD_GET_FPGAREG_OUT_BUFFER_MINNUM 1
#define       MC_CMD_GET_FPGAREG_OUT_BUFFER_MAXNUM 252


/***********************************/
/* MC_CMD_PUT_FPGAREG
 * Write multiple bytes to PTP FPGA.
 */
#define MC_CMD_PUT_FPGAREG 0xa

/* MC_CMD_PUT_FPGAREG_IN msgrequest */
#define    MC_CMD_PUT_FPGAREG_IN_LENMIN 5
#define    MC_CMD_PUT_FPGAREG_IN_LENMAX 252
#define    MC_CMD_PUT_FPGAREG_IN_LEN(num) (4+1*(num))
#define       MC_CMD_PUT_FPGAREG_IN_ADDR_OFST 0
#define       MC_CMD_PUT_FPGAREG_IN_BUFFER_OFST 4
#define       MC_CMD_PUT_FPGAREG_IN_BUFFER_LEN 1
#define       MC_CMD_PUT_FPGAREG_IN_BUFFER_MINNUM 1
#define       MC_CMD_PUT_FPGAREG_IN_BUFFER_MAXNUM 248

/* MC_CMD_PUT_FPGAREG_OUT msgresponse */
#define    MC_CMD_PUT_FPGAREG_OUT_LEN 0


/***********************************/
/* MC_CMD_PTP
 * Perform PTP operation
 */
#define MC_CMD_PTP 0xb

/* MC_CMD_PTP_IN msgrequest */
#define    MC_CMD_PTP_IN_LEN 1
#define       MC_CMD_PTP_IN_OP_OFST 0
#define       MC_CMD_PTP_IN_OP_LEN 1
#define          MC_CMD_PTP_OP_ENABLE 0x1 /* enum */
#define          MC_CMD_PTP_OP_DISABLE 0x2 /* enum */
#define          MC_CMD_PTP_OP_TRANSMIT 0x3 /* enum */
#define          MC_CMD_PTP_OP_READ_NIC_TIME 0x4 /* enum */
#define          MC_CMD_PTP_OP_STATUS 0x5 /* enum */
#define          MC_CMD_PTP_OP_ADJUST 0x6 /* enum */
#define          MC_CMD_PTP_OP_SYNCHRONIZE 0x7 /* enum */
#define          MC_CMD_PTP_OP_MANFTEST_BASIC 0x8 /* enum */
#define          MC_CMD_PTP_OP_MANFTEST_PACKET 0x9 /* enum */
#define          MC_CMD_PTP_OP_RESET_STATS 0xa /* enum */
#define          MC_CMD_PTP_OP_DEBUG 0xb /* enum */
#define          MC_CMD_PTP_OP_MAX 0xc /* enum */

/* MC_CMD_PTP_IN_ENABLE msgrequest */
#define    MC_CMD_PTP_IN_ENABLE_LEN 16
#define       MC_CMD_PTP_IN_CMD_OFST 0
#define       MC_CMD_PTP_IN_PERIPH_ID_OFST 4
#define       MC_CMD_PTP_IN_ENABLE_QUEUE_OFST 8
#define       MC_CMD_PTP_IN_ENABLE_MODE_OFST 12
#define          MC_CMD_PTP_MODE_V1 0x0 /* enum */
#define          MC_CMD_PTP_MODE_V1_VLAN 0x1 /* enum */
#define          MC_CMD_PTP_MODE_V2 0x2 /* enum */
#define          MC_CMD_PTP_MODE_V2_VLAN 0x3 /* enum */
#define          MC_CMD_PTP_MODE_V2_ENHANCED 0x4 /* enum */

/* MC_CMD_PTP_IN_DISABLE msgrequest */
#define    MC_CMD_PTP_IN_DISABLE_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_TRANSMIT msgrequest */
#define    MC_CMD_PTP_IN_TRANSMIT_LENMIN 13
#define    MC_CMD_PTP_IN_TRANSMIT_LENMAX 252
#define    MC_CMD_PTP_IN_TRANSMIT_LEN(num) (12+1*(num))
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
#define       MC_CMD_PTP_IN_TRANSMIT_LENGTH_OFST 8
#define       MC_CMD_PTP_IN_TRANSMIT_PACKET_OFST 12
#define       MC_CMD_PTP_IN_TRANSMIT_PACKET_LEN 1
#define       MC_CMD_PTP_IN_TRANSMIT_PACKET_MINNUM 1
#define       MC_CMD_PTP_IN_TRANSMIT_PACKET_MAXNUM 240

/* MC_CMD_PTP_IN_READ_NIC_TIME msgrequest */
#define    MC_CMD_PTP_IN_READ_NIC_TIME_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_STATUS msgrequest */
#define    MC_CMD_PTP_IN_STATUS_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_ADJUST msgrequest */
#define    MC_CMD_PTP_IN_ADJUST_LEN 24
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
#define       MC_CMD_PTP_IN_ADJUST_FREQ_OFST 8
#define       MC_CMD_PTP_IN_ADJUST_FREQ_LEN 8
#define       MC_CMD_PTP_IN_ADJUST_FREQ_LO_OFST 8
#define       MC_CMD_PTP_IN_ADJUST_FREQ_HI_OFST 12
#define          MC_CMD_PTP_IN_ADJUST_BITS 0x28 /* enum */
#define       MC_CMD_PTP_IN_ADJUST_SECONDS_OFST 16
#define       MC_CMD_PTP_IN_ADJUST_NANOSECONDS_OFST 20

/* MC_CMD_PTP_IN_SYNCHRONIZE msgrequest */
#define    MC_CMD_PTP_IN_SYNCHRONIZE_LEN 20
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
#define       MC_CMD_PTP_IN_SYNCHRONIZE_NUMTIMESETS_OFST 8
#define       MC_CMD_PTP_IN_SYNCHRONIZE_START_ADDR_OFST 12
#define       MC_CMD_PTP_IN_SYNCHRONIZE_START_ADDR_LEN 8
#define       MC_CMD_PTP_IN_SYNCHRONIZE_START_ADDR_LO_OFST 12
#define       MC_CMD_PTP_IN_SYNCHRONIZE_START_ADDR_HI_OFST 16

/* MC_CMD_PTP_IN_MANFTEST_BASIC msgrequest */
#define    MC_CMD_PTP_IN_MANFTEST_BASIC_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_MANFTEST_PACKET msgrequest */
#define    MC_CMD_PTP_IN_MANFTEST_PACKET_LEN 12
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
#define       MC_CMD_PTP_IN_MANFTEST_PACKET_TEST_ENABLE_OFST 8

/* MC_CMD_PTP_IN_RESET_STATS msgrequest */
#define    MC_CMD_PTP_IN_RESET_STATS_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_DEBUG msgrequest */
#define    MC_CMD_PTP_IN_DEBUG_LEN 12
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
#define       MC_CMD_PTP_IN_DEBUG_DEBUG_PARAM_OFST 8

/* MC_CMD_PTP_OUT msgresponse */
#define    MC_CMD_PTP_OUT_LEN 0

/* MC_CMD_PTP_OUT_TRANSMIT msgresponse */
#define    MC_CMD_PTP_OUT_TRANSMIT_LEN 8
#define       MC_CMD_PTP_OUT_TRANSMIT_SECONDS_OFST 0
#define       MC_CMD_PTP_OUT_TRANSMIT_NANOSECONDS_OFST 4

/* MC_CMD_PTP_OUT_READ_NIC_TIME msgresponse */
#define    MC_CMD_PTP_OUT_READ_NIC_TIME_LEN 8
#define       MC_CMD_PTP_OUT_READ_NIC_TIME_SECONDS_OFST 0
#define       MC_CMD_PTP_OUT_READ_NIC_TIME_NANOSECONDS_OFST 4

/* MC_CMD_PTP_OUT_STATUS msgresponse */
#define    MC_CMD_PTP_OUT_STATUS_LEN 64
#define       MC_CMD_PTP_OUT_STATUS_CLOCK_FREQ_OFST 0
#define       MC_CMD_PTP_OUT_STATUS_STATS_TX_OFST 4
#define       MC_CMD_PTP_OUT_STATUS_STATS_RX_OFST 8
#define       MC_CMD_PTP_OUT_STATUS_STATS_TS_OFST 12
#define       MC_CMD_PTP_OUT_STATUS_STATS_FM_OFST 16
#define       MC_CMD_PTP_OUT_STATUS_STATS_NFM_OFST 20
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFLOW_OFST 24
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_BAD_OFST 28
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MIN_OFST 32
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MAX_OFST 36
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_LAST_OFST 40
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MEAN_OFST 44
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_MIN_OFST 48
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_MAX_OFST 52
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_LAST_OFST 56
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_MEAN_OFST 60

/* MC_CMD_PTP_OUT_SYNCHRONIZE msgresponse */
#define    MC_CMD_PTP_OUT_SYNCHRONIZE_LENMIN 20
#define    MC_CMD_PTP_OUT_SYNCHRONIZE_LENMAX 240
#define    MC_CMD_PTP_OUT_SYNCHRONIZE_LEN(num) (0+20*(num))
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_OFST 0
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_LEN 20
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_MINNUM 1
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_MAXNUM 12
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_HOSTSTART_OFST 0
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_SECONDS_OFST 4
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_NANOSECONDS_OFST 8
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_HOSTEND_OFST 12
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_WAITNS_OFST 16

/* MC_CMD_PTP_OUT_MANFTEST_BASIC msgresponse */
#define    MC_CMD_PTP_OUT_MANFTEST_BASIC_LEN 8
#define       MC_CMD_PTP_OUT_MANFTEST_BASIC_TEST_RESULT_OFST 0
#define          MC_CMD_PTP_MANF_SUCCESS 0x0 /* enum */
#define          MC_CMD_PTP_MANF_FPGA_LOAD 0x1 /* enum */
#define          MC_CMD_PTP_MANF_FPGA_VERSION 0x2 /* enum */
#define          MC_CMD_PTP_MANF_FPGA_REGISTERS 0x3 /* enum */
#define          MC_CMD_PTP_MANF_OSCILLATOR 0x4 /* enum */
#define          MC_CMD_PTP_MANF_TIMESTAMPS 0x5 /* enum */
#define          MC_CMD_PTP_MANF_PACKET_COUNT 0x6 /* enum */
#define          MC_CMD_PTP_MANF_FILTER_COUNT 0x7 /* enum */
#define          MC_CMD_PTP_MANF_PACKET_ENOUGH 0x8 /* enum */
#define          MC_CMD_PTP_MANF_GPIO_TRIGGER 0x9 /* enum */
#define       MC_CMD_PTP_OUT_MANFTEST_BASIC_TEST_EXTOSC_OFST 4

/* MC_CMD_PTP_OUT_MANFTEST_PACKET msgresponse */
#define    MC_CMD_PTP_OUT_MANFTEST_PACKET_LEN 12
#define       MC_CMD_PTP_OUT_MANFTEST_PACKET_TEST_RESULT_OFST 0
#define       MC_CMD_PTP_OUT_MANFTEST_PACKET_TEST_FPGACOUNT_OFST 4
#define       MC_CMD_PTP_OUT_MANFTEST_PACKET_TEST_FILTERCOUNT_OFST 8


/***********************************/
/* MC_CMD_CSR_READ32
 * Read 32bit words from the indirect memory map.
 */
#define MC_CMD_CSR_READ32 0xc

/* MC_CMD_CSR_READ32_IN msgrequest */
#define    MC_CMD_CSR_READ32_IN_LEN 12
#define       MC_CMD_CSR_READ32_IN_ADDR_OFST 0
#define       MC_CMD_CSR_READ32_IN_STEP_OFST 4
#define       MC_CMD_CSR_READ32_IN_NUMWORDS_OFST 8

/* MC_CMD_CSR_READ32_OUT msgresponse */
#define    MC_CMD_CSR_READ32_OUT_LENMIN 4
#define    MC_CMD_CSR_READ32_OUT_LENMAX 252
#define    MC_CMD_CSR_READ32_OUT_LEN(num) (0+4*(num))
#define       MC_CMD_CSR_READ32_OUT_BUFFER_OFST 0
#define       MC_CMD_CSR_READ32_OUT_BUFFER_LEN 4
#define       MC_CMD_CSR_READ32_OUT_BUFFER_MINNUM 1
#define       MC_CMD_CSR_READ32_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_CSR_WRITE32
 * Write 32bit dwords to the indirect memory map.
 */
#define MC_CMD_CSR_WRITE32 0xd

/* MC_CMD_CSR_WRITE32_IN msgrequest */
#define    MC_CMD_CSR_WRITE32_IN_LENMIN 12
#define    MC_CMD_CSR_WRITE32_IN_LENMAX 252
#define    MC_CMD_CSR_WRITE32_IN_LEN(num) (8+4*(num))
#define       MC_CMD_CSR_WRITE32_IN_ADDR_OFST 0
#define       MC_CMD_CSR_WRITE32_IN_STEP_OFST 4
#define       MC_CMD_CSR_WRITE32_IN_BUFFER_OFST 8
#define       MC_CMD_CSR_WRITE32_IN_BUFFER_LEN 4
#define       MC_CMD_CSR_WRITE32_IN_BUFFER_MINNUM 1
#define       MC_CMD_CSR_WRITE32_IN_BUFFER_MAXNUM 61

/* MC_CMD_CSR_WRITE32_OUT msgresponse */
#define    MC_CMD_CSR_WRITE32_OUT_LEN 4
#define       MC_CMD_CSR_WRITE32_OUT_STATUS_OFST 0


/***********************************/
/* MC_CMD_STACKINFO
 * Get stack information.
 */
#define MC_CMD_STACKINFO 0xf

/* MC_CMD_STACKINFO_IN msgrequest */
#define    MC_CMD_STACKINFO_IN_LEN 0

/* MC_CMD_STACKINFO_OUT msgresponse */
#define    MC_CMD_STACKINFO_OUT_LENMIN 12
#define    MC_CMD_STACKINFO_OUT_LENMAX 252
#define    MC_CMD_STACKINFO_OUT_LEN(num) (0+12*(num))
#define       MC_CMD_STACKINFO_OUT_THREAD_INFO_OFST 0
#define       MC_CMD_STACKINFO_OUT_THREAD_INFO_LEN 12
#define       MC_CMD_STACKINFO_OUT_THREAD_INFO_MINNUM 1
#define       MC_CMD_STACKINFO_OUT_THREAD_INFO_MAXNUM 21


/***********************************/
/* MC_CMD_MDIO_READ
 * MDIO register read.
 */
#define MC_CMD_MDIO_READ 0x10

/* MC_CMD_MDIO_READ_IN msgrequest */
#define    MC_CMD_MDIO_READ_IN_LEN 16
#define       MC_CMD_MDIO_READ_IN_BUS_OFST 0
#define          MC_CMD_MDIO_BUS_INTERNAL 0x0 /* enum */
#define          MC_CMD_MDIO_BUS_EXTERNAL 0x1 /* enum */
#define       MC_CMD_MDIO_READ_IN_PRTAD_OFST 4
#define       MC_CMD_MDIO_READ_IN_DEVAD_OFST 8
#define          MC_CMD_MDIO_CLAUSE22 0x20 /* enum */
#define       MC_CMD_MDIO_READ_IN_ADDR_OFST 12

/* MC_CMD_MDIO_READ_OUT msgresponse */
#define    MC_CMD_MDIO_READ_OUT_LEN 8
#define       MC_CMD_MDIO_READ_OUT_VALUE_OFST 0
#define       MC_CMD_MDIO_READ_OUT_STATUS_OFST 4
#define          MC_CMD_MDIO_STATUS_GOOD 0x8 /* enum */


/***********************************/
/* MC_CMD_MDIO_WRITE
 * MDIO register write.
 */
#define MC_CMD_MDIO_WRITE 0x11

/* MC_CMD_MDIO_WRITE_IN msgrequest */
#define    MC_CMD_MDIO_WRITE_IN_LEN 20
#define       MC_CMD_MDIO_WRITE_IN_BUS_OFST 0
/*               MC_CMD_MDIO_BUS_INTERNAL 0x0 */
/*               MC_CMD_MDIO_BUS_EXTERNAL 0x1 */
#define       MC_CMD_MDIO_WRITE_IN_PRTAD_OFST 4
#define       MC_CMD_MDIO_WRITE_IN_DEVAD_OFST 8
/*               MC_CMD_MDIO_CLAUSE22 0x20 */
#define       MC_CMD_MDIO_WRITE_IN_ADDR_OFST 12
#define       MC_CMD_MDIO_WRITE_IN_VALUE_OFST 16

/* MC_CMD_MDIO_WRITE_OUT msgresponse */
#define    MC_CMD_MDIO_WRITE_OUT_LEN 4
#define       MC_CMD_MDIO_WRITE_OUT_STATUS_OFST 0
/*               MC_CMD_MDIO_STATUS_GOOD 0x8 */


/***********************************/
/* MC_CMD_DBI_WRITE
 * Write DBI register(s).
 */
#define MC_CMD_DBI_WRITE 0x12

/* MC_CMD_DBI_WRITE_IN msgrequest */
#define    MC_CMD_DBI_WRITE_IN_LENMIN 12
#define    MC_CMD_DBI_WRITE_IN_LENMAX 252
#define    MC_CMD_DBI_WRITE_IN_LEN(num) (0+12*(num))
#define       MC_CMD_DBI_WRITE_IN_DBIWROP_OFST 0
#define       MC_CMD_DBI_WRITE_IN_DBIWROP_LEN 12
#define       MC_CMD_DBI_WRITE_IN_DBIWROP_MINNUM 1
#define       MC_CMD_DBI_WRITE_IN_DBIWROP_MAXNUM 21

/* MC_CMD_DBI_WRITE_OUT msgresponse */
#define    MC_CMD_DBI_WRITE_OUT_LEN 0

/* MC_CMD_DBIWROP_TYPEDEF structuredef */
#define    MC_CMD_DBIWROP_TYPEDEF_LEN 12
#define       MC_CMD_DBIWROP_TYPEDEF_ADDRESS_OFST 0
#define       MC_CMD_DBIWROP_TYPEDEF_ADDRESS_LBN 0
#define       MC_CMD_DBIWROP_TYPEDEF_ADDRESS_WIDTH 32
#define       MC_CMD_DBIWROP_TYPEDEF_BYTE_MASK_OFST 4
#define       MC_CMD_DBIWROP_TYPEDEF_BYTE_MASK_LBN 32
#define       MC_CMD_DBIWROP_TYPEDEF_BYTE_MASK_WIDTH 32
#define       MC_CMD_DBIWROP_TYPEDEF_VALUE_OFST 8
#define       MC_CMD_DBIWROP_TYPEDEF_VALUE_LBN 64
#define       MC_CMD_DBIWROP_TYPEDEF_VALUE_WIDTH 32


/***********************************/
/* MC_CMD_PORT_READ32
 * Read a 32-bit register from the indirect port register map.
 */
#define MC_CMD_PORT_READ32 0x14

/* MC_CMD_PORT_READ32_IN msgrequest */
#define    MC_CMD_PORT_READ32_IN_LEN 4
#define       MC_CMD_PORT_READ32_IN_ADDR_OFST 0

/* MC_CMD_PORT_READ32_OUT msgresponse */
#define    MC_CMD_PORT_READ32_OUT_LEN 8
#define       MC_CMD_PORT_READ32_OUT_VALUE_OFST 0
#define       MC_CMD_PORT_READ32_OUT_STATUS_OFST 4


/***********************************/
/* MC_CMD_PORT_WRITE32
 * Write a 32-bit register to the indirect port register map.
 */
#define MC_CMD_PORT_WRITE32 0x15

/* MC_CMD_PORT_WRITE32_IN msgrequest */
#define    MC_CMD_PORT_WRITE32_IN_LEN 8
#define       MC_CMD_PORT_WRITE32_IN_ADDR_OFST 0
#define       MC_CMD_PORT_WRITE32_IN_VALUE_OFST 4

/* MC_CMD_PORT_WRITE32_OUT msgresponse */
#define    MC_CMD_PORT_WRITE32_OUT_LEN 4
#define       MC_CMD_PORT_WRITE32_OUT_STATUS_OFST 0


/***********************************/
/* MC_CMD_PORT_READ128
 * Read a 128-bit register from the indirect port register map.
 */
#define MC_CMD_PORT_READ128 0x16

/* MC_CMD_PORT_READ128_IN msgrequest */
#define    MC_CMD_PORT_READ128_IN_LEN 4
#define       MC_CMD_PORT_READ128_IN_ADDR_OFST 0

/* MC_CMD_PORT_READ128_OUT msgresponse */
#define    MC_CMD_PORT_READ128_OUT_LEN 20
#define       MC_CMD_PORT_READ128_OUT_VALUE_OFST 0
#define       MC_CMD_PORT_READ128_OUT_VALUE_LEN 16
#define       MC_CMD_PORT_READ128_OUT_STATUS_OFST 16


/***********************************/
/* MC_CMD_PORT_WRITE128
 * Write a 128-bit register to the indirect port register map.
 */
#define MC_CMD_PORT_WRITE128 0x17

/* MC_CMD_PORT_WRITE128_IN msgrequest */
#define    MC_CMD_PORT_WRITE128_IN_LEN 20
#define       MC_CMD_PORT_WRITE128_IN_ADDR_OFST 0
#define       MC_CMD_PORT_WRITE128_IN_VALUE_OFST 4
#define       MC_CMD_PORT_WRITE128_IN_VALUE_LEN 16

/* MC_CMD_PORT_WRITE128_OUT msgresponse */
#define    MC_CMD_PORT_WRITE128_OUT_LEN 4
#define       MC_CMD_PORT_WRITE128_OUT_STATUS_OFST 0


/***********************************/
/* MC_CMD_GET_BOARD_CFG
 * Returns the MC firmware configuration structure.
 */
#define MC_CMD_GET_BOARD_CFG 0x18

/* MC_CMD_GET_BOARD_CFG_IN msgrequest */
#define    MC_CMD_GET_BOARD_CFG_IN_LEN 0

/* MC_CMD_GET_BOARD_CFG_OUT msgresponse */
#define    MC_CMD_GET_BOARD_CFG_OUT_LENMIN 96
#define    MC_CMD_GET_BOARD_CFG_OUT_LENMAX 136
#define    MC_CMD_GET_BOARD_CFG_OUT_LEN(num) (72+2*(num))
#define       MC_CMD_GET_BOARD_CFG_OUT_BOARD_TYPE_OFST 0
#define       MC_CMD_GET_BOARD_CFG_OUT_BOARD_NAME_OFST 4
#define       MC_CMD_GET_BOARD_CFG_OUT_BOARD_NAME_LEN 32
#define       MC_CMD_GET_BOARD_CFG_OUT_CAPABILITIES_PORT0_OFST 36
#define          MC_CMD_CAPABILITIES_SMALL_BUF_TBL_LBN 0x0 /* enum */
#define          MC_CMD_CAPABILITIES_SMALL_BUF_TBL_WIDTH 0x1 /* enum */
#define          MC_CMD_CAPABILITIES_TURBO_LBN 0x1 /* enum */
#define          MC_CMD_CAPABILITIES_TURBO_WIDTH 0x1 /* enum */
#define          MC_CMD_CAPABILITIES_TURBO_ACTIVE_LBN 0x2 /* enum */
#define          MC_CMD_CAPABILITIES_TURBO_ACTIVE_WIDTH 0x1 /* enum */
#define          MC_CMD_CAPABILITIES_PTP_LBN 0x3 /* enum */
#define          MC_CMD_CAPABILITIES_PTP_WIDTH 0x1 /* enum */
#define       MC_CMD_GET_BOARD_CFG_OUT_CAPABILITIES_PORT1_OFST 40
/*            Enum values, see field(s): */
/*               CAPABILITIES_PORT0 */
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT0_OFST 44
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT0_LEN 6
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT1_OFST 50
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT1_LEN 6
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_COUNT_PORT0_OFST 56
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_COUNT_PORT1_OFST 60
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_STRIDE_PORT0_OFST 64
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_STRIDE_PORT1_OFST 68
#define       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_OFST 72
#define       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_LEN 2
#define       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_MINNUM 12
#define       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_MAXNUM 32


/***********************************/
/* MC_CMD_DBI_READX
 * Read DBI register(s).
 */
#define MC_CMD_DBI_READX 0x19

/* MC_CMD_DBI_READX_IN msgrequest */
#define    MC_CMD_DBI_READX_IN_LENMIN 8
#define    MC_CMD_DBI_READX_IN_LENMAX 248
#define    MC_CMD_DBI_READX_IN_LEN(num) (0+8*(num))
#define       MC_CMD_DBI_READX_IN_DBIRDOP_OFST 0
#define       MC_CMD_DBI_READX_IN_DBIRDOP_LEN 8
#define       MC_CMD_DBI_READX_IN_DBIRDOP_LO_OFST 0
#define       MC_CMD_DBI_READX_IN_DBIRDOP_HI_OFST 4
#define       MC_CMD_DBI_READX_IN_DBIRDOP_MINNUM 1
#define       MC_CMD_DBI_READX_IN_DBIRDOP_MAXNUM 31

/* MC_CMD_DBI_READX_OUT msgresponse */
#define    MC_CMD_DBI_READX_OUT_LENMIN 4
#define    MC_CMD_DBI_READX_OUT_LENMAX 252
#define    MC_CMD_DBI_READX_OUT_LEN(num) (0+4*(num))
#define       MC_CMD_DBI_READX_OUT_VALUE_OFST 0
#define       MC_CMD_DBI_READX_OUT_VALUE_LEN 4
#define       MC_CMD_DBI_READX_OUT_VALUE_MINNUM 1
#define       MC_CMD_DBI_READX_OUT_VALUE_MAXNUM 63


/***********************************/
/* MC_CMD_SET_RAND_SEED
 * Set the 16byte seed for the MC pseudo-random generator.
 */
#define MC_CMD_SET_RAND_SEED 0x1a

/* MC_CMD_SET_RAND_SEED_IN msgrequest */
#define    MC_CMD_SET_RAND_SEED_IN_LEN 16
#define       MC_CMD_SET_RAND_SEED_IN_SEED_OFST 0
#define       MC_CMD_SET_RAND_SEED_IN_SEED_LEN 16

/* MC_CMD_SET_RAND_SEED_OUT msgresponse */
#define    MC_CMD_SET_RAND_SEED_OUT_LEN 0


/***********************************/
/* MC_CMD_LTSSM_HIST
 * Retrieve the history of the PCIE LTSSM.
 */
#define MC_CMD_LTSSM_HIST 0x1b

/* MC_CMD_LTSSM_HIST_IN msgrequest */
#define    MC_CMD_LTSSM_HIST_IN_LEN 0

/* MC_CMD_LTSSM_HIST_OUT msgresponse */
#define    MC_CMD_LTSSM_HIST_OUT_LENMIN 0
#define    MC_CMD_LTSSM_HIST_OUT_LENMAX 252
#define    MC_CMD_LTSSM_HIST_OUT_LEN(num) (0+4*(num))
#define       MC_CMD_LTSSM_HIST_OUT_DATA_OFST 0
#define       MC_CMD_LTSSM_HIST_OUT_DATA_LEN 4
#define       MC_CMD_LTSSM_HIST_OUT_DATA_MINNUM 0
#define       MC_CMD_LTSSM_HIST_OUT_DATA_MAXNUM 63


/***********************************/
/* MC_CMD_DRV_ATTACH
 * Inform MCPU that this port is managed on the host.
 */
#define MC_CMD_DRV_ATTACH 0x1c

/* MC_CMD_DRV_ATTACH_IN msgrequest */
#define    MC_CMD_DRV_ATTACH_IN_LEN 8
#define       MC_CMD_DRV_ATTACH_IN_NEW_STATE_OFST 0
#define       MC_CMD_DRV_ATTACH_IN_UPDATE_OFST 4

/* MC_CMD_DRV_ATTACH_OUT msgresponse */
#define    MC_CMD_DRV_ATTACH_OUT_LEN 4
#define       MC_CMD_DRV_ATTACH_OUT_OLD_STATE_OFST 0


/***********************************/
/* MC_CMD_NCSI_PROD
 * Trigger an NC-SI event.
 */
#define MC_CMD_NCSI_PROD 0x1d

/* MC_CMD_NCSI_PROD_IN msgrequest */
#define    MC_CMD_NCSI_PROD_IN_LEN 4
#define       MC_CMD_NCSI_PROD_IN_EVENTS_OFST 0
#define          MC_CMD_NCSI_PROD_LINKCHANGE 0x0 /* enum */
#define          MC_CMD_NCSI_PROD_RESET 0x1 /* enum */
#define          MC_CMD_NCSI_PROD_DRVATTACH 0x2 /* enum */
#define        MC_CMD_NCSI_PROD_IN_LINKCHANGE_LBN 0
#define        MC_CMD_NCSI_PROD_IN_LINKCHANGE_WIDTH 1
#define        MC_CMD_NCSI_PROD_IN_RESET_LBN 1
#define        MC_CMD_NCSI_PROD_IN_RESET_WIDTH 1
#define        MC_CMD_NCSI_PROD_IN_DRVATTACH_LBN 2
#define        MC_CMD_NCSI_PROD_IN_DRVATTACH_WIDTH 1

/* MC_CMD_NCSI_PROD_OUT msgresponse */
#define    MC_CMD_NCSI_PROD_OUT_LEN 0


/***********************************/
/* MC_CMD_SHMUART
 * Route UART output to circular buffer in shared memory instead.
 */
#define MC_CMD_SHMUART 0x1f

/* MC_CMD_SHMUART_IN msgrequest */
#define    MC_CMD_SHMUART_IN_LEN 4
#define       MC_CMD_SHMUART_IN_FLAG_OFST 0

/* MC_CMD_SHMUART_OUT msgresponse */
#define    MC_CMD_SHMUART_OUT_LEN 0


/***********************************/
/* MC_CMD_ENTITY_RESET
 * Generic per-port reset.
 */
#define MC_CMD_ENTITY_RESET 0x20

/* MC_CMD_ENTITY_RESET_IN msgrequest */
#define    MC_CMD_ENTITY_RESET_IN_LEN 4
#define       MC_CMD_ENTITY_RESET_IN_FLAG_OFST 0
#define        MC_CMD_ENTITY_RESET_IN_FUNCTION_RESOURCE_RESET_LBN 0
#define        MC_CMD_ENTITY_RESET_IN_FUNCTION_RESOURCE_RESET_WIDTH 1

/* MC_CMD_ENTITY_RESET_OUT msgresponse */
#define    MC_CMD_ENTITY_RESET_OUT_LEN 0


/***********************************/
/* MC_CMD_PCIE_CREDITS
 * Read instantaneous and minimum flow control thresholds.
 */
#define MC_CMD_PCIE_CREDITS 0x21

/* MC_CMD_PCIE_CREDITS_IN msgrequest */
#define    MC_CMD_PCIE_CREDITS_IN_LEN 8
#define       MC_CMD_PCIE_CREDITS_IN_POLL_PERIOD_OFST 0
#define       MC_CMD_PCIE_CREDITS_IN_WIPE_OFST 4

/* MC_CMD_PCIE_CREDITS_OUT msgresponse */
#define    MC_CMD_PCIE_CREDITS_OUT_LEN 16
#define       MC_CMD_PCIE_CREDITS_OUT_CURRENT_P_HDR_OFST 0
#define       MC_CMD_PCIE_CREDITS_OUT_CURRENT_P_HDR_LEN 2
#define       MC_CMD_PCIE_CREDITS_OUT_CURRENT_P_DATA_OFST 2
#define       MC_CMD_PCIE_CREDITS_OUT_CURRENT_P_DATA_LEN 2
#define       MC_CMD_PCIE_CREDITS_OUT_CURRENT_NP_HDR_OFST 4
#define       MC_CMD_PCIE_CREDITS_OUT_CURRENT_NP_HDR_LEN 2
#define       MC_CMD_PCIE_CREDITS_OUT_CURRENT_NP_DATA_OFST 6
#define       MC_CMD_PCIE_CREDITS_OUT_CURRENT_NP_DATA_LEN 2
#define       MC_CMD_PCIE_CREDITS_OUT_MINIMUM_P_HDR_OFST 8
#define       MC_CMD_PCIE_CREDITS_OUT_MINIMUM_P_HDR_LEN 2
#define       MC_CMD_PCIE_CREDITS_OUT_MINIMUM_P_DATA_OFST 10
#define       MC_CMD_PCIE_CREDITS_OUT_MINIMUM_P_DATA_LEN 2
#define       MC_CMD_PCIE_CREDITS_OUT_MINIMUM_NP_HDR_OFST 12
#define       MC_CMD_PCIE_CREDITS_OUT_MINIMUM_NP_HDR_LEN 2
#define       MC_CMD_PCIE_CREDITS_OUT_MINIMUM_NP_DATA_OFST 14
#define       MC_CMD_PCIE_CREDITS_OUT_MINIMUM_NP_DATA_LEN 2


/***********************************/
/* MC_CMD_RXD_MONITOR
 * Get histogram of RX queue fill level.
 */
#define MC_CMD_RXD_MONITOR 0x22

/* MC_CMD_RXD_MONITOR_IN msgrequest */
#define    MC_CMD_RXD_MONITOR_IN_LEN 12
#define       MC_CMD_RXD_MONITOR_IN_QID_OFST 0
#define       MC_CMD_RXD_MONITOR_IN_POLL_PERIOD_OFST 4
#define       MC_CMD_RXD_MONITOR_IN_WIPE_OFST 8

/* MC_CMD_RXD_MONITOR_OUT msgresponse */
#define    MC_CMD_RXD_MONITOR_OUT_LEN 80
#define       MC_CMD_RXD_MONITOR_OUT_QID_OFST 0
#define       MC_CMD_RXD_MONITOR_OUT_RING_FILL_OFST 4
#define       MC_CMD_RXD_MONITOR_OUT_CACHE_FILL_OFST 8
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_1_OFST 12
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_2_OFST 16
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_4_OFST 20
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_8_OFST 24
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_16_OFST 28
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_32_OFST 32
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_64_OFST 36
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_128_OFST 40
#define       MC_CMD_RXD_MONITOR_OUT_RING_LT_256_OFST 44
#define       MC_CMD_RXD_MONITOR_OUT_RING_GE_256_OFST 48
#define       MC_CMD_RXD_MONITOR_OUT_CACHE_LT_1_OFST 52
#define       MC_CMD_RXD_MONITOR_OUT_CACHE_LT_2_OFST 56
#define       MC_CMD_RXD_MONITOR_OUT_CACHE_LT_4_OFST 60
#define       MC_CMD_RXD_MONITOR_OUT_CACHE_LT_8_OFST 64
#define       MC_CMD_RXD_MONITOR_OUT_CACHE_LT_16_OFST 68
#define       MC_CMD_RXD_MONITOR_OUT_CACHE_LT_32_OFST 72
#define       MC_CMD_RXD_MONITOR_OUT_CACHE_GE_32_OFST 76


/***********************************/
/* MC_CMD_PUTS
 * puts(3) implementation over MCDI
 */
#define MC_CMD_PUTS 0x23

/* MC_CMD_PUTS_IN msgrequest */
#define    MC_CMD_PUTS_IN_LENMIN 13
#define    MC_CMD_PUTS_IN_LENMAX 252
#define    MC_CMD_PUTS_IN_LEN(num) (12+1*(num))
#define       MC_CMD_PUTS_IN_DEST_OFST 0
#define        MC_CMD_PUTS_IN_UART_LBN 0
#define        MC_CMD_PUTS_IN_UART_WIDTH 1
#define        MC_CMD_PUTS_IN_PORT_LBN 1
#define        MC_CMD_PUTS_IN_PORT_WIDTH 1
#define       MC_CMD_PUTS_IN_DHOST_OFST 4
#define       MC_CMD_PUTS_IN_DHOST_LEN 6
#define       MC_CMD_PUTS_IN_STRING_OFST 12
#define       MC_CMD_PUTS_IN_STRING_LEN 1
#define       MC_CMD_PUTS_IN_STRING_MINNUM 1
#define       MC_CMD_PUTS_IN_STRING_MAXNUM 240

/* MC_CMD_PUTS_OUT msgresponse */
#define    MC_CMD_PUTS_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_PHY_CFG
 * Report PHY configuration.
 */
#define MC_CMD_GET_PHY_CFG 0x24

/* MC_CMD_GET_PHY_CFG_IN msgrequest */
#define    MC_CMD_GET_PHY_CFG_IN_LEN 0

/* MC_CMD_GET_PHY_CFG_OUT msgresponse */
#define    MC_CMD_GET_PHY_CFG_OUT_LEN 72
#define       MC_CMD_GET_PHY_CFG_OUT_FLAGS_OFST 0
#define        MC_CMD_GET_PHY_CFG_OUT_PRESENT_LBN 0
#define        MC_CMD_GET_PHY_CFG_OUT_PRESENT_WIDTH 1
#define        MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_SHORT_LBN 1
#define        MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_SHORT_WIDTH 1
#define        MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_LONG_LBN 2
#define        MC_CMD_GET_PHY_CFG_OUT_BIST_CABLE_LONG_WIDTH 1
#define        MC_CMD_GET_PHY_CFG_OUT_LOWPOWER_LBN 3
#define        MC_CMD_GET_PHY_CFG_OUT_LOWPOWER_WIDTH 1
#define        MC_CMD_GET_PHY_CFG_OUT_POWEROFF_LBN 4
#define        MC_CMD_GET_PHY_CFG_OUT_POWEROFF_WIDTH 1
#define        MC_CMD_GET_PHY_CFG_OUT_TXDIS_LBN 5
#define        MC_CMD_GET_PHY_CFG_OUT_TXDIS_WIDTH 1
#define        MC_CMD_GET_PHY_CFG_OUT_BIST_LBN 6
#define        MC_CMD_GET_PHY_CFG_OUT_BIST_WIDTH 1
#define       MC_CMD_GET_PHY_CFG_OUT_TYPE_OFST 4
#define       MC_CMD_GET_PHY_CFG_OUT_SUPPORTED_CAP_OFST 8
#define        MC_CMD_PHY_CAP_10HDX_LBN 1
#define        MC_CMD_PHY_CAP_10HDX_WIDTH 1
#define        MC_CMD_PHY_CAP_10FDX_LBN 2
#define        MC_CMD_PHY_CAP_10FDX_WIDTH 1
#define        MC_CMD_PHY_CAP_100HDX_LBN 3
#define        MC_CMD_PHY_CAP_100HDX_WIDTH 1
#define        MC_CMD_PHY_CAP_100FDX_LBN 4
#define        MC_CMD_PHY_CAP_100FDX_WIDTH 1
#define        MC_CMD_PHY_CAP_1000HDX_LBN 5
#define        MC_CMD_PHY_CAP_1000HDX_WIDTH 1
#define        MC_CMD_PHY_CAP_1000FDX_LBN 6
#define        MC_CMD_PHY_CAP_1000FDX_WIDTH 1
#define        MC_CMD_PHY_CAP_10000FDX_LBN 7
#define        MC_CMD_PHY_CAP_10000FDX_WIDTH 1
#define        MC_CMD_PHY_CAP_PAUSE_LBN 8
#define        MC_CMD_PHY_CAP_PAUSE_WIDTH 1
#define        MC_CMD_PHY_CAP_ASYM_LBN 9
#define        MC_CMD_PHY_CAP_ASYM_WIDTH 1
#define        MC_CMD_PHY_CAP_AN_LBN 10
#define        MC_CMD_PHY_CAP_AN_WIDTH 1
#define       MC_CMD_GET_PHY_CFG_OUT_CHANNEL_OFST 12
#define       MC_CMD_GET_PHY_CFG_OUT_PRT_OFST 16
#define       MC_CMD_GET_PHY_CFG_OUT_STATS_MASK_OFST 20
#define       MC_CMD_GET_PHY_CFG_OUT_NAME_OFST 24
#define       MC_CMD_GET_PHY_CFG_OUT_NAME_LEN 20
#define       MC_CMD_GET_PHY_CFG_OUT_MEDIA_TYPE_OFST 44
#define          MC_CMD_MEDIA_XAUI 0x1 /* enum */
#define          MC_CMD_MEDIA_CX4 0x2 /* enum */
#define          MC_CMD_MEDIA_KX4 0x3 /* enum */
#define          MC_CMD_MEDIA_XFP 0x4 /* enum */
#define          MC_CMD_MEDIA_SFP_PLUS 0x5 /* enum */
#define          MC_CMD_MEDIA_BASE_T 0x6 /* enum */
#define       MC_CMD_GET_PHY_CFG_OUT_MMD_MASK_OFST 48
#define          MC_CMD_MMD_CLAUSE22 0x0 /* enum */
#define          MC_CMD_MMD_CLAUSE45_PMAPMD 0x1 /* enum */
#define          MC_CMD_MMD_CLAUSE45_WIS 0x2 /* enum */
#define          MC_CMD_MMD_CLAUSE45_PCS 0x3 /* enum */
#define          MC_CMD_MMD_CLAUSE45_PHYXS 0x4 /* enum */
#define          MC_CMD_MMD_CLAUSE45_DTEXS 0x5 /* enum */
#define          MC_CMD_MMD_CLAUSE45_TC 0x6 /* enum */
#define          MC_CMD_MMD_CLAUSE45_AN 0x7 /* enum */
#define          MC_CMD_MMD_CLAUSE45_C22EXT 0x1d /* enum */
#define          MC_CMD_MMD_CLAUSE45_VEND1 0x1e /* enum */
#define          MC_CMD_MMD_CLAUSE45_VEND2 0x1f /* enum */
#define       MC_CMD_GET_PHY_CFG_OUT_REVISION_OFST 52
#define       MC_CMD_GET_PHY_CFG_OUT_REVISION_LEN 20


/***********************************/
/* MC_CMD_START_BIST
 * Start a BIST test on the PHY.
 */
#define MC_CMD_START_BIST 0x25

/* MC_CMD_START_BIST_IN msgrequest */
#define    MC_CMD_START_BIST_IN_LEN 4
#define       MC_CMD_START_BIST_IN_TYPE_OFST 0
#define          MC_CMD_PHY_BIST_CABLE_SHORT 0x1 /* enum */
#define          MC_CMD_PHY_BIST_CABLE_LONG 0x2 /* enum */
#define          MC_CMD_BPX_SERDES_BIST 0x3 /* enum */
#define          MC_CMD_MC_LOOPBACK_BIST 0x4 /* enum */
#define          MC_CMD_PHY_BIST 0x5 /* enum */

/* MC_CMD_START_BIST_OUT msgresponse */
#define    MC_CMD_START_BIST_OUT_LEN 0


/***********************************/
/* MC_CMD_POLL_BIST
 * Poll for BIST completion.
 */
#define MC_CMD_POLL_BIST 0x26

/* MC_CMD_POLL_BIST_IN msgrequest */
#define    MC_CMD_POLL_BIST_IN_LEN 0

/* MC_CMD_POLL_BIST_OUT msgresponse */
#define    MC_CMD_POLL_BIST_OUT_LEN 8
#define       MC_CMD_POLL_BIST_OUT_RESULT_OFST 0
#define          MC_CMD_POLL_BIST_RUNNING 0x1 /* enum */
#define          MC_CMD_POLL_BIST_PASSED 0x2 /* enum */
#define          MC_CMD_POLL_BIST_FAILED 0x3 /* enum */
#define          MC_CMD_POLL_BIST_TIMEOUT 0x4 /* enum */
#define       MC_CMD_POLL_BIST_OUT_PRIVATE_OFST 4

/* MC_CMD_POLL_BIST_OUT_SFT9001 msgresponse */
#define    MC_CMD_POLL_BIST_OUT_SFT9001_LEN 36
/*            MC_CMD_POLL_BIST_OUT_RESULT_OFST 0 */
/*            Enum values, see field(s): */
/*               MC_CMD_POLL_BIST_OUT/MC_CMD_POLL_BIST_OUT_RESULT */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_A_OFST 4
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_B_OFST 8
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_C_OFST 12
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_D_OFST 16
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_A_OFST 20
#define          MC_CMD_POLL_BIST_SFT9001_PAIR_OK 0x1 /* enum */
#define          MC_CMD_POLL_BIST_SFT9001_PAIR_OPEN 0x2 /* enum */
#define          MC_CMD_POLL_BIST_SFT9001_INTRA_PAIR_SHORT 0x3 /* enum */
#define          MC_CMD_POLL_BIST_SFT9001_INTER_PAIR_SHORT 0x4 /* enum */
#define          MC_CMD_POLL_BIST_SFT9001_PAIR_BUSY 0x9 /* enum */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_B_OFST 24
/*            Enum values, see field(s): */
/*               CABLE_STATUS_A */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_C_OFST 28
/*            Enum values, see field(s): */
/*               CABLE_STATUS_A */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_D_OFST 32
/*            Enum values, see field(s): */
/*               CABLE_STATUS_A */

/* MC_CMD_POLL_BIST_OUT_MRSFP msgresponse */
#define    MC_CMD_POLL_BIST_OUT_MRSFP_LEN 8
/*            MC_CMD_POLL_BIST_OUT_RESULT_OFST 0 */
/*            Enum values, see field(s): */
/*               MC_CMD_POLL_BIST_OUT/MC_CMD_POLL_BIST_OUT_RESULT */
#define       MC_CMD_POLL_BIST_OUT_MRSFP_TEST_OFST 4
#define          MC_CMD_POLL_BIST_MRSFP_TEST_COMPLETE 0x0 /* enum */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_WRITE 0x1 /* enum */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_NO_ACCESS_IO_EXP 0x2 /* enum */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_NO_ACCESS_MODULE 0x3 /* enum */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_IO_EXP_I2C_CONFIGURE 0x4 /* enum */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_I2C_NO_CROSSTALK 0x5 /* enum */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_PRESENCE 0x6 /* enum */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_ID_I2C_ACCESS 0x7 /* enum */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_ID_SANE_VALUE 0x8 /* enum */


/***********************************/
/* MC_CMD_FLUSH_RX_QUEUES
 * Flush receive queue(s).
 */
#define MC_CMD_FLUSH_RX_QUEUES 0x27

/* MC_CMD_FLUSH_RX_QUEUES_IN msgrequest */
#define    MC_CMD_FLUSH_RX_QUEUES_IN_LENMIN 4
#define    MC_CMD_FLUSH_RX_QUEUES_IN_LENMAX 252
#define    MC_CMD_FLUSH_RX_QUEUES_IN_LEN(num) (0+4*(num))
#define       MC_CMD_FLUSH_RX_QUEUES_IN_QID_OFST_OFST 0
#define       MC_CMD_FLUSH_RX_QUEUES_IN_QID_OFST_LEN 4
#define       MC_CMD_FLUSH_RX_QUEUES_IN_QID_OFST_MINNUM 1
#define       MC_CMD_FLUSH_RX_QUEUES_IN_QID_OFST_MAXNUM 63

/* MC_CMD_FLUSH_RX_QUEUES_OUT msgresponse */
#define    MC_CMD_FLUSH_RX_QUEUES_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_LOOPBACK_MODES
 * Get port's loopback modes.
 */
#define MC_CMD_GET_LOOPBACK_MODES 0x28

/* MC_CMD_GET_LOOPBACK_MODES_IN msgrequest */
#define    MC_CMD_GET_LOOPBACK_MODES_IN_LEN 0

/* MC_CMD_GET_LOOPBACK_MODES_OUT msgresponse */
#define    MC_CMD_GET_LOOPBACK_MODES_OUT_LEN 32
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_100M_OFST 0
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_100M_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_100M_LO_OFST 0
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_100M_HI_OFST 4
#define          MC_CMD_LOOPBACK_NONE  0x0 /* enum */
#define          MC_CMD_LOOPBACK_DATA  0x1 /* enum */
#define          MC_CMD_LOOPBACK_GMAC  0x2 /* enum */
#define          MC_CMD_LOOPBACK_XGMII 0x3 /* enum */
#define          MC_CMD_LOOPBACK_XGXS  0x4 /* enum */
#define          MC_CMD_LOOPBACK_XAUI  0x5 /* enum */
#define          MC_CMD_LOOPBACK_GMII  0x6 /* enum */
#define          MC_CMD_LOOPBACK_SGMII  0x7 /* enum */
#define          MC_CMD_LOOPBACK_XGBR  0x8 /* enum */
#define          MC_CMD_LOOPBACK_XFI  0x9 /* enum */
#define          MC_CMD_LOOPBACK_XAUI_FAR  0xa /* enum */
#define          MC_CMD_LOOPBACK_GMII_FAR  0xb /* enum */
#define          MC_CMD_LOOPBACK_SGMII_FAR  0xc /* enum */
#define          MC_CMD_LOOPBACK_XFI_FAR  0xd /* enum */
#define          MC_CMD_LOOPBACK_GPHY  0xe /* enum */
#define          MC_CMD_LOOPBACK_PHYXS  0xf /* enum */
#define          MC_CMD_LOOPBACK_PCS  0x10 /* enum */
#define          MC_CMD_LOOPBACK_PMAPMD  0x11 /* enum */
#define          MC_CMD_LOOPBACK_XPORT  0x12 /* enum */
#define          MC_CMD_LOOPBACK_XGMII_WS  0x13 /* enum */
#define          MC_CMD_LOOPBACK_XAUI_WS  0x14 /* enum */
#define          MC_CMD_LOOPBACK_XAUI_WS_FAR  0x15 /* enum */
#define          MC_CMD_LOOPBACK_XAUI_WS_NEAR  0x16 /* enum */
#define          MC_CMD_LOOPBACK_GMII_WS  0x17 /* enum */
#define          MC_CMD_LOOPBACK_XFI_WS  0x18 /* enum */
#define          MC_CMD_LOOPBACK_XFI_WS_FAR  0x19 /* enum */
#define          MC_CMD_LOOPBACK_PHYXS_WS  0x1a /* enum */
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_1G_OFST 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_1G_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_1G_LO_OFST 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_1G_HI_OFST 12
/*            Enum values, see field(s): */
/*               100M */
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_10G_OFST 16
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_10G_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_10G_LO_OFST 16
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_10G_HI_OFST 20
/*            Enum values, see field(s): */
/*               100M */
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_OFST 24
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_LO_OFST 24
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_HI_OFST 28
/*            Enum values, see field(s): */
/*               100M */


/***********************************/
/* MC_CMD_GET_LINK
 * Read the unified MAC/PHY link state.
 */
#define MC_CMD_GET_LINK 0x29

/* MC_CMD_GET_LINK_IN msgrequest */
#define    MC_CMD_GET_LINK_IN_LEN 0

/* MC_CMD_GET_LINK_OUT msgresponse */
#define    MC_CMD_GET_LINK_OUT_LEN 28
#define       MC_CMD_GET_LINK_OUT_CAP_OFST 0
#define       MC_CMD_GET_LINK_OUT_LP_CAP_OFST 4
#define       MC_CMD_GET_LINK_OUT_LINK_SPEED_OFST 8
#define       MC_CMD_GET_LINK_OUT_LOOPBACK_MODE_OFST 12
/*            Enum values, see field(s): */
/*               MC_CMD_GET_LOOPBACK_MODES/MC_CMD_GET_LOOPBACK_MODES_OUT/100M */
#define       MC_CMD_GET_LINK_OUT_FLAGS_OFST 16
#define        MC_CMD_GET_LINK_OUT_LINK_UP_LBN 0
#define        MC_CMD_GET_LINK_OUT_LINK_UP_WIDTH 1
#define        MC_CMD_GET_LINK_OUT_FULL_DUPLEX_LBN 1
#define        MC_CMD_GET_LINK_OUT_FULL_DUPLEX_WIDTH 1
#define        MC_CMD_GET_LINK_OUT_BPX_LINK_LBN 2
#define        MC_CMD_GET_LINK_OUT_BPX_LINK_WIDTH 1
#define        MC_CMD_GET_LINK_OUT_PHY_LINK_LBN 3
#define        MC_CMD_GET_LINK_OUT_PHY_LINK_WIDTH 1
#define       MC_CMD_GET_LINK_OUT_FCNTL_OFST 20
#define          MC_CMD_FCNTL_OFF 0x0 /* enum */
#define          MC_CMD_FCNTL_RESPOND 0x1 /* enum */
#define          MC_CMD_FCNTL_BIDIR 0x2 /* enum */
#define       MC_CMD_GET_LINK_OUT_MAC_FAULT_OFST 24
#define        MC_CMD_MAC_FAULT_XGMII_LOCAL_LBN 0
#define        MC_CMD_MAC_FAULT_XGMII_LOCAL_WIDTH 1
#define        MC_CMD_MAC_FAULT_XGMII_REMOTE_LBN 1
#define        MC_CMD_MAC_FAULT_XGMII_REMOTE_WIDTH 1
#define        MC_CMD_MAC_FAULT_SGMII_REMOTE_LBN 2
#define        MC_CMD_MAC_FAULT_SGMII_REMOTE_WIDTH 1
#define        MC_CMD_MAC_FAULT_PENDING_RECONFIG_LBN 3
#define        MC_CMD_MAC_FAULT_PENDING_RECONFIG_WIDTH 1


/***********************************/
/* MC_CMD_SET_LINK
 * Write the unified MAC/PHY link configuration.
 */
#define MC_CMD_SET_LINK 0x2a

/* MC_CMD_SET_LINK_IN msgrequest */
#define    MC_CMD_SET_LINK_IN_LEN 16
#define       MC_CMD_SET_LINK_IN_CAP_OFST 0
#define       MC_CMD_SET_LINK_IN_FLAGS_OFST 4
#define        MC_CMD_SET_LINK_IN_LOWPOWER_LBN 0
#define        MC_CMD_SET_LINK_IN_LOWPOWER_WIDTH 1
#define        MC_CMD_SET_LINK_IN_POWEROFF_LBN 1
#define        MC_CMD_SET_LINK_IN_POWEROFF_WIDTH 1
#define        MC_CMD_SET_LINK_IN_TXDIS_LBN 2
#define        MC_CMD_SET_LINK_IN_TXDIS_WIDTH 1
#define       MC_CMD_SET_LINK_IN_LOOPBACK_MODE_OFST 8
/*            Enum values, see field(s): */
/*               MC_CMD_GET_LOOPBACK_MODES/MC_CMD_GET_LOOPBACK_MODES_OUT/100M */
#define       MC_CMD_SET_LINK_IN_LOOPBACK_SPEED_OFST 12

/* MC_CMD_SET_LINK_OUT msgresponse */
#define    MC_CMD_SET_LINK_OUT_LEN 0


/***********************************/
/* MC_CMD_SET_ID_LED
 * Set indentification LED state.
 */
#define MC_CMD_SET_ID_LED 0x2b

/* MC_CMD_SET_ID_LED_IN msgrequest */
#define    MC_CMD_SET_ID_LED_IN_LEN 4
#define       MC_CMD_SET_ID_LED_IN_STATE_OFST 0
#define          MC_CMD_LED_OFF  0x0 /* enum */
#define          MC_CMD_LED_ON  0x1 /* enum */
#define          MC_CMD_LED_DEFAULT  0x2 /* enum */

/* MC_CMD_SET_ID_LED_OUT msgresponse */
#define    MC_CMD_SET_ID_LED_OUT_LEN 0


/***********************************/
/* MC_CMD_SET_MAC
 * Set MAC configuration.
 */
#define MC_CMD_SET_MAC 0x2c

/* MC_CMD_SET_MAC_IN msgrequest */
#define    MC_CMD_SET_MAC_IN_LEN 24
#define       MC_CMD_SET_MAC_IN_MTU_OFST 0
#define       MC_CMD_SET_MAC_IN_DRAIN_OFST 4
#define       MC_CMD_SET_MAC_IN_ADDR_OFST 8
#define       MC_CMD_SET_MAC_IN_ADDR_LEN 8
#define       MC_CMD_SET_MAC_IN_ADDR_LO_OFST 8
#define       MC_CMD_SET_MAC_IN_ADDR_HI_OFST 12
#define       MC_CMD_SET_MAC_IN_REJECT_OFST 16
#define        MC_CMD_SET_MAC_IN_REJECT_UNCST_LBN 0
#define        MC_CMD_SET_MAC_IN_REJECT_UNCST_WIDTH 1
#define        MC_CMD_SET_MAC_IN_REJECT_BRDCST_LBN 1
#define        MC_CMD_SET_MAC_IN_REJECT_BRDCST_WIDTH 1
#define       MC_CMD_SET_MAC_IN_FCNTL_OFST 20
/*               MC_CMD_FCNTL_OFF 0x0 */
/*               MC_CMD_FCNTL_RESPOND 0x1 */
/*               MC_CMD_FCNTL_BIDIR 0x2 */
#define          MC_CMD_FCNTL_AUTO 0x3 /* enum */

/* MC_CMD_SET_MAC_OUT msgresponse */
#define    MC_CMD_SET_MAC_OUT_LEN 0


/***********************************/
/* MC_CMD_PHY_STATS
 * Get generic PHY statistics.
 */
#define MC_CMD_PHY_STATS 0x2d

/* MC_CMD_PHY_STATS_IN msgrequest */
#define    MC_CMD_PHY_STATS_IN_LEN 8
#define       MC_CMD_PHY_STATS_IN_DMA_ADDR_OFST 0
#define       MC_CMD_PHY_STATS_IN_DMA_ADDR_LEN 8
#define       MC_CMD_PHY_STATS_IN_DMA_ADDR_LO_OFST 0
#define       MC_CMD_PHY_STATS_IN_DMA_ADDR_HI_OFST 4

/* MC_CMD_PHY_STATS_OUT_DMA msgresponse */
#define    MC_CMD_PHY_STATS_OUT_DMA_LEN 0

/* MC_CMD_PHY_STATS_OUT_NO_DMA msgresponse */
#define    MC_CMD_PHY_STATS_OUT_NO_DMA_LEN (((MC_CMD_PHY_NSTATS*32))>>3)
#define       MC_CMD_PHY_STATS_OUT_NO_DMA_STATISTICS_OFST 0
#define       MC_CMD_PHY_STATS_OUT_NO_DMA_STATISTICS_LEN 4
#define       MC_CMD_PHY_STATS_OUT_NO_DMA_STATISTICS_NUM MC_CMD_PHY_NSTATS
#define          MC_CMD_OUI  0x0 /* enum */
#define          MC_CMD_PMA_PMD_LINK_UP  0x1 /* enum */
#define          MC_CMD_PMA_PMD_RX_FAULT  0x2 /* enum */
#define          MC_CMD_PMA_PMD_TX_FAULT  0x3 /* enum */
#define          MC_CMD_PMA_PMD_SIGNAL  0x4 /* enum */
#define          MC_CMD_PMA_PMD_SNR_A  0x5 /* enum */
#define          MC_CMD_PMA_PMD_SNR_B  0x6 /* enum */
#define          MC_CMD_PMA_PMD_SNR_C  0x7 /* enum */
#define          MC_CMD_PMA_PMD_SNR_D  0x8 /* enum */
#define          MC_CMD_PCS_LINK_UP  0x9 /* enum */
#define          MC_CMD_PCS_RX_FAULT  0xa /* enum */
#define          MC_CMD_PCS_TX_FAULT  0xb /* enum */
#define          MC_CMD_PCS_BER  0xc /* enum */
#define          MC_CMD_PCS_BLOCK_ERRORS  0xd /* enum */
#define          MC_CMD_PHYXS_LINK_UP  0xe /* enum */
#define          MC_CMD_PHYXS_RX_FAULT  0xf /* enum */
#define          MC_CMD_PHYXS_TX_FAULT  0x10 /* enum */
#define          MC_CMD_PHYXS_ALIGN  0x11 /* enum */
#define          MC_CMD_PHYXS_SYNC  0x12 /* enum */
#define          MC_CMD_AN_LINK_UP  0x13 /* enum */
#define          MC_CMD_AN_COMPLETE  0x14 /* enum */
#define          MC_CMD_AN_10GBT_STATUS  0x15 /* enum */
#define          MC_CMD_CL22_LINK_UP  0x16 /* enum */
#define          MC_CMD_PHY_NSTATS  0x17 /* enum */


/***********************************/
/* MC_CMD_MAC_STATS
 * Get generic MAC statistics.
 */
#define MC_CMD_MAC_STATS 0x2e

/* MC_CMD_MAC_STATS_IN msgrequest */
#define    MC_CMD_MAC_STATS_IN_LEN 16
#define       MC_CMD_MAC_STATS_IN_DMA_ADDR_OFST 0
#define       MC_CMD_MAC_STATS_IN_DMA_ADDR_LEN 8
#define       MC_CMD_MAC_STATS_IN_DMA_ADDR_LO_OFST 0
#define       MC_CMD_MAC_STATS_IN_DMA_ADDR_HI_OFST 4
#define       MC_CMD_MAC_STATS_IN_CMD_OFST 8
#define        MC_CMD_MAC_STATS_IN_DMA_LBN 0
#define        MC_CMD_MAC_STATS_IN_DMA_WIDTH 1
#define        MC_CMD_MAC_STATS_IN_CLEAR_LBN 1
#define        MC_CMD_MAC_STATS_IN_CLEAR_WIDTH 1
#define        MC_CMD_MAC_STATS_IN_PERIODIC_CHANGE_LBN 2
#define        MC_CMD_MAC_STATS_IN_PERIODIC_CHANGE_WIDTH 1
#define        MC_CMD_MAC_STATS_IN_PERIODIC_ENABLE_LBN 3
#define        MC_CMD_MAC_STATS_IN_PERIODIC_ENABLE_WIDTH 1
#define        MC_CMD_MAC_STATS_IN_PERIODIC_CLEAR_LBN 4
#define        MC_CMD_MAC_STATS_IN_PERIODIC_CLEAR_WIDTH 1
#define        MC_CMD_MAC_STATS_IN_PERIODIC_NOEVENT_LBN 5
#define        MC_CMD_MAC_STATS_IN_PERIODIC_NOEVENT_WIDTH 1
#define        MC_CMD_MAC_STATS_IN_PERIOD_MS_LBN 16
#define        MC_CMD_MAC_STATS_IN_PERIOD_MS_WIDTH 16
#define       MC_CMD_MAC_STATS_IN_DMA_LEN_OFST 12

/* MC_CMD_MAC_STATS_OUT_DMA msgresponse */
#define    MC_CMD_MAC_STATS_OUT_DMA_LEN 0

/* MC_CMD_MAC_STATS_OUT_NO_DMA msgresponse */
#define    MC_CMD_MAC_STATS_OUT_NO_DMA_LEN (((MC_CMD_MAC_NSTATS*64))>>3)
#define       MC_CMD_MAC_STATS_OUT_NO_DMA_STATISTICS_OFST 0
#define       MC_CMD_MAC_STATS_OUT_NO_DMA_STATISTICS_LEN 8
#define       MC_CMD_MAC_STATS_OUT_NO_DMA_STATISTICS_LO_OFST 0
#define       MC_CMD_MAC_STATS_OUT_NO_DMA_STATISTICS_HI_OFST 4
#define       MC_CMD_MAC_STATS_OUT_NO_DMA_STATISTICS_NUM MC_CMD_MAC_NSTATS
#define          MC_CMD_MAC_GENERATION_START  0x0 /* enum */
#define          MC_CMD_MAC_TX_PKTS  0x1 /* enum */
#define          MC_CMD_MAC_TX_PAUSE_PKTS  0x2 /* enum */
#define          MC_CMD_MAC_TX_CONTROL_PKTS  0x3 /* enum */
#define          MC_CMD_MAC_TX_UNICAST_PKTS  0x4 /* enum */
#define          MC_CMD_MAC_TX_MULTICAST_PKTS  0x5 /* enum */
#define          MC_CMD_MAC_TX_BROADCAST_PKTS  0x6 /* enum */
#define          MC_CMD_MAC_TX_BYTES  0x7 /* enum */
#define          MC_CMD_MAC_TX_BAD_BYTES  0x8 /* enum */
#define          MC_CMD_MAC_TX_LT64_PKTS  0x9 /* enum */
#define          MC_CMD_MAC_TX_64_PKTS  0xa /* enum */
#define          MC_CMD_MAC_TX_65_TO_127_PKTS  0xb /* enum */
#define          MC_CMD_MAC_TX_128_TO_255_PKTS  0xc /* enum */
#define          MC_CMD_MAC_TX_256_TO_511_PKTS  0xd /* enum */
#define          MC_CMD_MAC_TX_512_TO_1023_PKTS  0xe /* enum */
#define          MC_CMD_MAC_TX_1024_TO_15XX_PKTS  0xf /* enum */
#define          MC_CMD_MAC_TX_15XX_TO_JUMBO_PKTS  0x10 /* enum */
#define          MC_CMD_MAC_TX_GTJUMBO_PKTS  0x11 /* enum */
#define          MC_CMD_MAC_TX_BAD_FCS_PKTS  0x12 /* enum */
#define          MC_CMD_MAC_TX_SINGLE_COLLISION_PKTS  0x13 /* enum */
#define          MC_CMD_MAC_TX_MULTIPLE_COLLISION_PKTS  0x14 /* enum */
#define          MC_CMD_MAC_TX_EXCESSIVE_COLLISION_PKTS  0x15 /* enum */
#define          MC_CMD_MAC_TX_LATE_COLLISION_PKTS  0x16 /* enum */
#define          MC_CMD_MAC_TX_DEFERRED_PKTS  0x17 /* enum */
#define          MC_CMD_MAC_TX_EXCESSIVE_DEFERRED_PKTS  0x18 /* enum */
#define          MC_CMD_MAC_TX_NON_TCPUDP_PKTS  0x19 /* enum */
#define          MC_CMD_MAC_TX_MAC_SRC_ERR_PKTS  0x1a /* enum */
#define          MC_CMD_MAC_TX_IP_SRC_ERR_PKTS  0x1b /* enum */
#define          MC_CMD_MAC_RX_PKTS  0x1c /* enum */
#define          MC_CMD_MAC_RX_PAUSE_PKTS  0x1d /* enum */
#define          MC_CMD_MAC_RX_GOOD_PKTS  0x1e /* enum */
#define          MC_CMD_MAC_RX_CONTROL_PKTS  0x1f /* enum */
#define          MC_CMD_MAC_RX_UNICAST_PKTS  0x20 /* enum */
#define          MC_CMD_MAC_RX_MULTICAST_PKTS  0x21 /* enum */
#define          MC_CMD_MAC_RX_BROADCAST_PKTS  0x22 /* enum */
#define          MC_CMD_MAC_RX_BYTES  0x23 /* enum */
#define          MC_CMD_MAC_RX_BAD_BYTES  0x24 /* enum */
#define          MC_CMD_MAC_RX_64_PKTS  0x25 /* enum */
#define          MC_CMD_MAC_RX_65_TO_127_PKTS  0x26 /* enum */
#define          MC_CMD_MAC_RX_128_TO_255_PKTS  0x27 /* enum */
#define          MC_CMD_MAC_RX_256_TO_511_PKTS  0x28 /* enum */
#define          MC_CMD_MAC_RX_512_TO_1023_PKTS  0x29 /* enum */
#define          MC_CMD_MAC_RX_1024_TO_15XX_PKTS  0x2a /* enum */
#define          MC_CMD_MAC_RX_15XX_TO_JUMBO_PKTS  0x2b /* enum */
#define          MC_CMD_MAC_RX_GTJUMBO_PKTS  0x2c /* enum */
#define          MC_CMD_MAC_RX_UNDERSIZE_PKTS  0x2d /* enum */
#define          MC_CMD_MAC_RX_BAD_FCS_PKTS  0x2e /* enum */
#define          MC_CMD_MAC_RX_OVERFLOW_PKTS  0x2f /* enum */
#define          MC_CMD_MAC_RX_FALSE_CARRIER_PKTS  0x30 /* enum */
#define          MC_CMD_MAC_RX_SYMBOL_ERROR_PKTS  0x31 /* enum */
#define          MC_CMD_MAC_RX_ALIGN_ERROR_PKTS  0x32 /* enum */
#define          MC_CMD_MAC_RX_LENGTH_ERROR_PKTS  0x33 /* enum */
#define          MC_CMD_MAC_RX_INTERNAL_ERROR_PKTS  0x34 /* enum */
#define          MC_CMD_MAC_RX_JABBER_PKTS  0x35 /* enum */
#define          MC_CMD_MAC_RX_NODESC_DROPS  0x36 /* enum */
#define          MC_CMD_MAC_RX_LANES01_CHAR_ERR  0x37 /* enum */
#define          MC_CMD_MAC_RX_LANES23_CHAR_ERR  0x38 /* enum */
#define          MC_CMD_MAC_RX_LANES01_DISP_ERR  0x39 /* enum */
#define          MC_CMD_MAC_RX_LANES23_DISP_ERR  0x3a /* enum */
#define          MC_CMD_MAC_RX_MATCH_FAULT  0x3b /* enum */
#define          MC_CMD_GMAC_DMABUF_START  0x40 /* enum */
#define          MC_CMD_GMAC_DMABUF_END    0x5f /* enum */
#define          MC_CMD_MAC_GENERATION_END 0x60 /* enum */
#define          MC_CMD_MAC_NSTATS  0x61 /* enum */


/***********************************/
/* MC_CMD_SRIOV
 * to be documented
 */
#define MC_CMD_SRIOV 0x30

/* MC_CMD_SRIOV_IN msgrequest */
#define    MC_CMD_SRIOV_IN_LEN 12
#define       MC_CMD_SRIOV_IN_ENABLE_OFST 0
#define       MC_CMD_SRIOV_IN_VI_BASE_OFST 4
#define       MC_CMD_SRIOV_IN_VF_COUNT_OFST 8

/* MC_CMD_SRIOV_OUT msgresponse */
#define    MC_CMD_SRIOV_OUT_LEN 8
#define       MC_CMD_SRIOV_OUT_VI_SCALE_OFST 0
#define       MC_CMD_SRIOV_OUT_VF_TOTAL_OFST 4

/* MC_CMD_MEMCPY_RECORD_TYPEDEF structuredef */
#define    MC_CMD_MEMCPY_RECORD_TYPEDEF_LEN 32
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_NUM_RECORDS_OFST 0
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_NUM_RECORDS_LBN 0
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_NUM_RECORDS_WIDTH 32
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_RID_OFST 4
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_RID_LBN 32
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_RID_WIDTH 32
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_ADDR_OFST 8
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_ADDR_LEN 8
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_ADDR_LO_OFST 8
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_ADDR_HI_OFST 12
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_ADDR_LBN 64
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_TO_ADDR_WIDTH 64
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_RID_OFST 16
#define          MC_CMD_MEMCPY_RECORD_TYPEDEF_RID_INLINE 0x100 /* enum */
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_RID_LBN 128
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_RID_WIDTH 32
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_ADDR_OFST 20
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_ADDR_LEN 8
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_ADDR_LO_OFST 20
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_ADDR_HI_OFST 24
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_ADDR_LBN 160
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_FROM_ADDR_WIDTH 64
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_LENGTH_OFST 28
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_LENGTH_LBN 224
#define       MC_CMD_MEMCPY_RECORD_TYPEDEF_LENGTH_WIDTH 32


/***********************************/
/* MC_CMD_MEMCPY
 * Perform memory copy operation.
 */
#define MC_CMD_MEMCPY 0x31

/* MC_CMD_MEMCPY_IN msgrequest */
#define    MC_CMD_MEMCPY_IN_LENMIN 32
#define    MC_CMD_MEMCPY_IN_LENMAX 224
#define    MC_CMD_MEMCPY_IN_LEN(num) (0+32*(num))
#define       MC_CMD_MEMCPY_IN_RECORD_OFST 0
#define       MC_CMD_MEMCPY_IN_RECORD_LEN 32
#define       MC_CMD_MEMCPY_IN_RECORD_MINNUM 1
#define       MC_CMD_MEMCPY_IN_RECORD_MAXNUM 7

/* MC_CMD_MEMCPY_OUT msgresponse */
#define    MC_CMD_MEMCPY_OUT_LEN 0


/***********************************/
/* MC_CMD_WOL_FILTER_SET
 * Set a WoL filter.
 */
#define MC_CMD_WOL_FILTER_SET 0x32

/* MC_CMD_WOL_FILTER_SET_IN msgrequest */
#define    MC_CMD_WOL_FILTER_SET_IN_LEN 192
#define       MC_CMD_WOL_FILTER_SET_IN_FILTER_MODE_OFST 0
#define          MC_CMD_FILTER_MODE_SIMPLE    0x0 /* enum */
#define          MC_CMD_FILTER_MODE_STRUCTURED 0xffffffff /* enum */
#define       MC_CMD_WOL_FILTER_SET_IN_WOL_TYPE_OFST 4
#define          MC_CMD_WOL_TYPE_MAGIC      0x0 /* enum */
#define          MC_CMD_WOL_TYPE_WIN_MAGIC 0x2 /* enum */
#define          MC_CMD_WOL_TYPE_IPV4_SYN   0x3 /* enum */
#define          MC_CMD_WOL_TYPE_IPV6_SYN   0x4 /* enum */
#define          MC_CMD_WOL_TYPE_BITMAP     0x5 /* enum */
#define          MC_CMD_WOL_TYPE_LINK       0x6 /* enum */
#define          MC_CMD_WOL_TYPE_MAX        0x7 /* enum */
#define       MC_CMD_WOL_FILTER_SET_IN_DATA_OFST 8
#define       MC_CMD_WOL_FILTER_SET_IN_DATA_LEN 4
#define       MC_CMD_WOL_FILTER_SET_IN_DATA_NUM 46

/* MC_CMD_WOL_FILTER_SET_IN_MAGIC msgrequest */
#define    MC_CMD_WOL_FILTER_SET_IN_MAGIC_LEN 16
/*            MC_CMD_WOL_FILTER_SET_IN_FILTER_MODE_OFST 0 */
/*            MC_CMD_WOL_FILTER_SET_IN_WOL_TYPE_OFST 4 */
#define       MC_CMD_WOL_FILTER_SET_IN_MAGIC_MAC_OFST 8
#define       MC_CMD_WOL_FILTER_SET_IN_MAGIC_MAC_LEN 8
#define       MC_CMD_WOL_FILTER_SET_IN_MAGIC_MAC_LO_OFST 8
#define       MC_CMD_WOL_FILTER_SET_IN_MAGIC_MAC_HI_OFST 12

/* MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN msgrequest */
#define    MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_LEN 20
/*            MC_CMD_WOL_FILTER_SET_IN_FILTER_MODE_OFST 0 */
/*            MC_CMD_WOL_FILTER_SET_IN_WOL_TYPE_OFST 4 */
#define       MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_SRC_IP_OFST 8
#define       MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_DST_IP_OFST 12
#define       MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_SRC_PORT_OFST 16
#define       MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_SRC_PORT_LEN 2
#define       MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_DST_PORT_OFST 18
#define       MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_DST_PORT_LEN 2

/* MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN msgrequest */
#define    MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_LEN 44
/*            MC_CMD_WOL_FILTER_SET_IN_FILTER_MODE_OFST 0 */
/*            MC_CMD_WOL_FILTER_SET_IN_WOL_TYPE_OFST 4 */
#define       MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_SRC_IP_OFST 8
#define       MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_SRC_IP_LEN 16
#define       MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_DST_IP_OFST 24
#define       MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_DST_IP_LEN 16
#define       MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_SRC_PORT_OFST 40
#define       MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_SRC_PORT_LEN 2
#define       MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_DST_PORT_OFST 42
#define       MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_DST_PORT_LEN 2

/* MC_CMD_WOL_FILTER_SET_IN_BITMAP msgrequest */
#define    MC_CMD_WOL_FILTER_SET_IN_BITMAP_LEN 187
/*            MC_CMD_WOL_FILTER_SET_IN_FILTER_MODE_OFST 0 */
/*            MC_CMD_WOL_FILTER_SET_IN_WOL_TYPE_OFST 4 */
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_MASK_OFST 8
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_MASK_LEN 48
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_BITMAP_OFST 56
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_BITMAP_LEN 128
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_LEN_OFST 184
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_LEN_LEN 1
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_LAYER3_OFST 185
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_LAYER3_LEN 1
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_LAYER4_OFST 186
#define       MC_CMD_WOL_FILTER_SET_IN_BITMAP_LAYER4_LEN 1

/* MC_CMD_WOL_FILTER_SET_IN_LINK msgrequest */
#define    MC_CMD_WOL_FILTER_SET_IN_LINK_LEN 12
/*            MC_CMD_WOL_FILTER_SET_IN_FILTER_MODE_OFST 0 */
/*            MC_CMD_WOL_FILTER_SET_IN_WOL_TYPE_OFST 4 */
#define       MC_CMD_WOL_FILTER_SET_IN_LINK_MASK_OFST 8
#define        MC_CMD_WOL_FILTER_SET_IN_LINK_UP_LBN 0
#define        MC_CMD_WOL_FILTER_SET_IN_LINK_UP_WIDTH 1
#define        MC_CMD_WOL_FILTER_SET_IN_LINK_DOWN_LBN 1
#define        MC_CMD_WOL_FILTER_SET_IN_LINK_DOWN_WIDTH 1

/* MC_CMD_WOL_FILTER_SET_OUT msgresponse */
#define    MC_CMD_WOL_FILTER_SET_OUT_LEN 4
#define       MC_CMD_WOL_FILTER_SET_OUT_FILTER_ID_OFST 0


/***********************************/
/* MC_CMD_WOL_FILTER_REMOVE
 * Remove a WoL filter.
 */
#define MC_CMD_WOL_FILTER_REMOVE 0x33

/* MC_CMD_WOL_FILTER_REMOVE_IN msgrequest */
#define    MC_CMD_WOL_FILTER_REMOVE_IN_LEN 4
#define       MC_CMD_WOL_FILTER_REMOVE_IN_FILTER_ID_OFST 0

/* MC_CMD_WOL_FILTER_REMOVE_OUT msgresponse */
#define    MC_CMD_WOL_FILTER_REMOVE_OUT_LEN 0


/***********************************/
/* MC_CMD_WOL_FILTER_RESET
 * Reset (i.e. remove all) WoL filters.
 */
#define MC_CMD_WOL_FILTER_RESET 0x34

/* MC_CMD_WOL_FILTER_RESET_IN msgrequest */
#define    MC_CMD_WOL_FILTER_RESET_IN_LEN 4
#define       MC_CMD_WOL_FILTER_RESET_IN_MASK_OFST 0
#define          MC_CMD_WOL_FILTER_RESET_IN_WAKE_FILTERS 0x1 /* enum */
#define          MC_CMD_WOL_FILTER_RESET_IN_LIGHTSOUT_OFFLOADS 0x2 /* enum */

/* MC_CMD_WOL_FILTER_RESET_OUT msgresponse */
#define    MC_CMD_WOL_FILTER_RESET_OUT_LEN 0


/***********************************/
/* MC_CMD_SET_MCAST_HASH
 * Set the MCASH hash value.
 */
#define MC_CMD_SET_MCAST_HASH 0x35

/* MC_CMD_SET_MCAST_HASH_IN msgrequest */
#define    MC_CMD_SET_MCAST_HASH_IN_LEN 32
#define       MC_CMD_SET_MCAST_HASH_IN_HASH0_OFST 0
#define       MC_CMD_SET_MCAST_HASH_IN_HASH0_LEN 16
#define       MC_CMD_SET_MCAST_HASH_IN_HASH1_OFST 16
#define       MC_CMD_SET_MCAST_HASH_IN_HASH1_LEN 16

/* MC_CMD_SET_MCAST_HASH_OUT msgresponse */
#define    MC_CMD_SET_MCAST_HASH_OUT_LEN 0


/***********************************/
/* MC_CMD_NVRAM_TYPES
 * Get virtual NVRAM partitions information.
 */
#define MC_CMD_NVRAM_TYPES 0x36

/* MC_CMD_NVRAM_TYPES_IN msgrequest */
#define    MC_CMD_NVRAM_TYPES_IN_LEN 0

/* MC_CMD_NVRAM_TYPES_OUT msgresponse */
#define    MC_CMD_NVRAM_TYPES_OUT_LEN 4
#define       MC_CMD_NVRAM_TYPES_OUT_TYPES_OFST 0
#define          MC_CMD_NVRAM_TYPE_DISABLED_CALLISTO 0x0 /* enum */
#define          MC_CMD_NVRAM_TYPE_MC_FW 0x1 /* enum */
#define          MC_CMD_NVRAM_TYPE_MC_FW_BACKUP 0x2 /* enum */
#define          MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT0 0x3 /* enum */
#define          MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT1 0x4 /* enum */
#define          MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT0 0x5 /* enum */
#define          MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT1 0x6 /* enum */
#define          MC_CMD_NVRAM_TYPE_EXP_ROM 0x7 /* enum */
#define          MC_CMD_NVRAM_TYPE_EXP_ROM_CFG_PORT0 0x8 /* enum */
#define          MC_CMD_NVRAM_TYPE_EXP_ROM_CFG_PORT1 0x9 /* enum */
#define          MC_CMD_NVRAM_TYPE_PHY_PORT0 0xa /* enum */
#define          MC_CMD_NVRAM_TYPE_PHY_PORT1 0xb /* enum */
#define          MC_CMD_NVRAM_TYPE_LOG 0xc /* enum */
#define          MC_CMD_NVRAM_TYPE_FPGA 0xd /* enum */


/***********************************/
/* MC_CMD_NVRAM_INFO
 * Read info about a virtual NVRAM partition.
 */
#define MC_CMD_NVRAM_INFO 0x37

/* MC_CMD_NVRAM_INFO_IN msgrequest */
#define    MC_CMD_NVRAM_INFO_IN_LEN 4
#define       MC_CMD_NVRAM_INFO_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */

/* MC_CMD_NVRAM_INFO_OUT msgresponse */
#define    MC_CMD_NVRAM_INFO_OUT_LEN 24
#define       MC_CMD_NVRAM_INFO_OUT_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_INFO_OUT_SIZE_OFST 4
#define       MC_CMD_NVRAM_INFO_OUT_ERASESIZE_OFST 8
#define       MC_CMD_NVRAM_INFO_OUT_FLAGS_OFST 12
#define        MC_CMD_NVRAM_INFO_OUT_PROTECTED_LBN 0
#define        MC_CMD_NVRAM_INFO_OUT_PROTECTED_WIDTH 1
#define       MC_CMD_NVRAM_INFO_OUT_PHYSDEV_OFST 16
#define       MC_CMD_NVRAM_INFO_OUT_PHYSADDR_OFST 20


/***********************************/
/* MC_CMD_NVRAM_UPDATE_START
 * Start a group of update operations on a virtual NVRAM partition.
 */
#define MC_CMD_NVRAM_UPDATE_START 0x38

/* MC_CMD_NVRAM_UPDATE_START_IN msgrequest */
#define    MC_CMD_NVRAM_UPDATE_START_IN_LEN 4
#define       MC_CMD_NVRAM_UPDATE_START_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */

/* MC_CMD_NVRAM_UPDATE_START_OUT msgresponse */
#define    MC_CMD_NVRAM_UPDATE_START_OUT_LEN 0


/***********************************/
/* MC_CMD_NVRAM_READ
 * Read data from a virtual NVRAM partition.
 */
#define MC_CMD_NVRAM_READ 0x39

/* MC_CMD_NVRAM_READ_IN msgrequest */
#define    MC_CMD_NVRAM_READ_IN_LEN 12
#define       MC_CMD_NVRAM_READ_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_READ_IN_OFFSET_OFST 4
#define       MC_CMD_NVRAM_READ_IN_LENGTH_OFST 8

/* MC_CMD_NVRAM_READ_OUT msgresponse */
#define    MC_CMD_NVRAM_READ_OUT_LENMIN 1
#define    MC_CMD_NVRAM_READ_OUT_LENMAX 252
#define    MC_CMD_NVRAM_READ_OUT_LEN(num) (0+1*(num))
#define       MC_CMD_NVRAM_READ_OUT_READ_BUFFER_OFST 0
#define       MC_CMD_NVRAM_READ_OUT_READ_BUFFER_LEN 1
#define       MC_CMD_NVRAM_READ_OUT_READ_BUFFER_MINNUM 1
#define       MC_CMD_NVRAM_READ_OUT_READ_BUFFER_MAXNUM 252


/***********************************/
/* MC_CMD_NVRAM_WRITE
 * Write data to a virtual NVRAM partition.
 */
#define MC_CMD_NVRAM_WRITE 0x3a

/* MC_CMD_NVRAM_WRITE_IN msgrequest */
#define    MC_CMD_NVRAM_WRITE_IN_LENMIN 13
#define    MC_CMD_NVRAM_WRITE_IN_LENMAX 252
#define    MC_CMD_NVRAM_WRITE_IN_LEN(num) (12+1*(num))
#define       MC_CMD_NVRAM_WRITE_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_WRITE_IN_OFFSET_OFST 4
#define       MC_CMD_NVRAM_WRITE_IN_LENGTH_OFST 8
#define       MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_OFST 12
#define       MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_LEN 1
#define       MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_MINNUM 1
#define       MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_MAXNUM 240

/* MC_CMD_NVRAM_WRITE_OUT msgresponse */
#define    MC_CMD_NVRAM_WRITE_OUT_LEN 0


/***********************************/
/* MC_CMD_NVRAM_ERASE
 * Erase sector(s) from a virtual NVRAM partition.
 */
#define MC_CMD_NVRAM_ERASE 0x3b

/* MC_CMD_NVRAM_ERASE_IN msgrequest */
#define    MC_CMD_NVRAM_ERASE_IN_LEN 12
#define       MC_CMD_NVRAM_ERASE_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_ERASE_IN_OFFSET_OFST 4
#define       MC_CMD_NVRAM_ERASE_IN_LENGTH_OFST 8

/* MC_CMD_NVRAM_ERASE_OUT msgresponse */
#define    MC_CMD_NVRAM_ERASE_OUT_LEN 0


/***********************************/
/* MC_CMD_NVRAM_UPDATE_FINISH
 * Finish a group of update operations on a virtual NVRAM partition.
 */
#define MC_CMD_NVRAM_UPDATE_FINISH 0x3c

/* MC_CMD_NVRAM_UPDATE_FINISH_IN msgrequest */
#define    MC_CMD_NVRAM_UPDATE_FINISH_IN_LEN 8
#define       MC_CMD_NVRAM_UPDATE_FINISH_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_UPDATE_FINISH_IN_REBOOT_OFST 4

/* MC_CMD_NVRAM_UPDATE_FINISH_OUT msgresponse */
#define    MC_CMD_NVRAM_UPDATE_FINISH_OUT_LEN 0


/***********************************/
/* MC_CMD_REBOOT
 * Reboot the MC.
 */
#define MC_CMD_REBOOT 0x3d

/* MC_CMD_REBOOT_IN msgrequest */
#define    MC_CMD_REBOOT_IN_LEN 4
#define       MC_CMD_REBOOT_IN_FLAGS_OFST 0
#define          MC_CMD_REBOOT_FLAGS_AFTER_ASSERTION 0x1 /* enum */

/* MC_CMD_REBOOT_OUT msgresponse */
#define    MC_CMD_REBOOT_OUT_LEN 0


/***********************************/
/* MC_CMD_SCHEDINFO
 * Request scheduler info.
 */
#define MC_CMD_SCHEDINFO 0x3e

/* MC_CMD_SCHEDINFO_IN msgrequest */
#define    MC_CMD_SCHEDINFO_IN_LEN 0

/* MC_CMD_SCHEDINFO_OUT msgresponse */
#define    MC_CMD_SCHEDINFO_OUT_LENMIN 4
#define    MC_CMD_SCHEDINFO_OUT_LENMAX 252
#define    MC_CMD_SCHEDINFO_OUT_LEN(num) (0+4*(num))
#define       MC_CMD_SCHEDINFO_OUT_DATA_OFST 0
#define       MC_CMD_SCHEDINFO_OUT_DATA_LEN 4
#define       MC_CMD_SCHEDINFO_OUT_DATA_MINNUM 1
#define       MC_CMD_SCHEDINFO_OUT_DATA_MAXNUM 63


/***********************************/
/* MC_CMD_REBOOT_MODE
 */
#define MC_CMD_REBOOT_MODE 0x3f

/* MC_CMD_REBOOT_MODE_IN msgrequest */
#define    MC_CMD_REBOOT_MODE_IN_LEN 4
#define       MC_CMD_REBOOT_MODE_IN_VALUE_OFST 0
#define          MC_CMD_REBOOT_MODE_NORMAL 0x0 /* enum */
#define          MC_CMD_REBOOT_MODE_SNAPPER 0x3 /* enum */

/* MC_CMD_REBOOT_MODE_OUT msgresponse */
#define    MC_CMD_REBOOT_MODE_OUT_LEN 4
#define       MC_CMD_REBOOT_MODE_OUT_VALUE_OFST 0


/***********************************/
/* MC_CMD_SENSOR_INFO
 * Returns information about every available sensor.
 */
#define MC_CMD_SENSOR_INFO 0x41

/* MC_CMD_SENSOR_INFO_IN msgrequest */
#define    MC_CMD_SENSOR_INFO_IN_LEN 0

/* MC_CMD_SENSOR_INFO_OUT msgresponse */
#define    MC_CMD_SENSOR_INFO_OUT_LENMIN 12
#define    MC_CMD_SENSOR_INFO_OUT_LENMAX 252
#define    MC_CMD_SENSOR_INFO_OUT_LEN(num) (4+8*(num))
#define       MC_CMD_SENSOR_INFO_OUT_MASK_OFST 0
#define          MC_CMD_SENSOR_CONTROLLER_TEMP  0x0 /* enum */
#define          MC_CMD_SENSOR_PHY_COMMON_TEMP  0x1 /* enum */
#define          MC_CMD_SENSOR_CONTROLLER_COOLING  0x2 /* enum */
#define          MC_CMD_SENSOR_PHY0_TEMP  0x3 /* enum */
#define          MC_CMD_SENSOR_PHY0_COOLING  0x4 /* enum */
#define          MC_CMD_SENSOR_PHY1_TEMP  0x5 /* enum */
#define          MC_CMD_SENSOR_PHY1_COOLING  0x6 /* enum */
#define          MC_CMD_SENSOR_IN_1V0  0x7 /* enum */
#define          MC_CMD_SENSOR_IN_1V2  0x8 /* enum */
#define          MC_CMD_SENSOR_IN_1V8  0x9 /* enum */
#define          MC_CMD_SENSOR_IN_2V5  0xa /* enum */
#define          MC_CMD_SENSOR_IN_3V3  0xb /* enum */
#define          MC_CMD_SENSOR_IN_12V0  0xc /* enum */
#define          MC_CMD_SENSOR_IN_1V2A  0xd /* enum */
#define          MC_CMD_SENSOR_IN_VREF  0xe /* enum */
#define       MC_CMD_SENSOR_ENTRY_OFST 4
#define       MC_CMD_SENSOR_ENTRY_LEN 8
#define       MC_CMD_SENSOR_ENTRY_LO_OFST 4
#define       MC_CMD_SENSOR_ENTRY_HI_OFST 8
#define       MC_CMD_SENSOR_ENTRY_MINNUM 1
#define       MC_CMD_SENSOR_ENTRY_MAXNUM 31

/* MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF structuredef */
#define    MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_LEN 8
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN1_OFST 0
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN1_LEN 2
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN1_LBN 0
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN1_WIDTH 16
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX1_OFST 2
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX1_LEN 2
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX1_LBN 16
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX1_WIDTH 16
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN2_OFST 4
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN2_LEN 2
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN2_LBN 32
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MIN2_WIDTH 16
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX2_OFST 6
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX2_LEN 2
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX2_LBN 48
#define       MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF_MAX2_WIDTH 16


/***********************************/
/* MC_CMD_READ_SENSORS
 * Returns the current reading from each sensor.
 */
#define MC_CMD_READ_SENSORS 0x42

/* MC_CMD_READ_SENSORS_IN msgrequest */
#define    MC_CMD_READ_SENSORS_IN_LEN 8
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_OFST 0
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_LEN 8
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_LO_OFST 0
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_HI_OFST 4

/* MC_CMD_READ_SENSORS_OUT msgresponse */
#define    MC_CMD_READ_SENSORS_OUT_LEN 0

/* MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF structuredef */
#define    MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_LEN 3
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE_OFST 0
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE_LEN 2
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE_LBN 0
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE_WIDTH 16
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE_OFST 2
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE_LEN 1
#define          MC_CMD_SENSOR_STATE_OK  0x0 /* enum */
#define          MC_CMD_SENSOR_STATE_WARNING  0x1 /* enum */
#define          MC_CMD_SENSOR_STATE_FATAL  0x2 /* enum */
#define          MC_CMD_SENSOR_STATE_BROKEN  0x3 /* enum */
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE_LBN 16
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE_WIDTH 8


/***********************************/
/* MC_CMD_GET_PHY_STATE
 * Report current state of PHY.
 */
#define MC_CMD_GET_PHY_STATE 0x43

/* MC_CMD_GET_PHY_STATE_IN msgrequest */
#define    MC_CMD_GET_PHY_STATE_IN_LEN 0

/* MC_CMD_GET_PHY_STATE_OUT msgresponse */
#define    MC_CMD_GET_PHY_STATE_OUT_LEN 4
#define       MC_CMD_GET_PHY_STATE_OUT_STATE_OFST 0
#define          MC_CMD_PHY_STATE_OK 0x1 /* enum */
#define          MC_CMD_PHY_STATE_ZOMBIE 0x2 /* enum */


/***********************************/
/* MC_CMD_SETUP_8021QBB
 * 802.1Qbb control.
 */
#define MC_CMD_SETUP_8021QBB 0x44

/* MC_CMD_SETUP_8021QBB_IN msgrequest */
#define    MC_CMD_SETUP_8021QBB_IN_LEN 32
#define       MC_CMD_SETUP_8021QBB_IN_TXQS_OFST 0
#define       MC_CMD_SETUP_8021QBB_IN_TXQS_LEN 32

/* MC_CMD_SETUP_8021QBB_OUT msgresponse */
#define    MC_CMD_SETUP_8021QBB_OUT_LEN 0


/***********************************/
/* MC_CMD_WOL_FILTER_GET
 * Retrieve ID of any WoL filters.
 */
#define MC_CMD_WOL_FILTER_GET 0x45

/* MC_CMD_WOL_FILTER_GET_IN msgrequest */
#define    MC_CMD_WOL_FILTER_GET_IN_LEN 0

/* MC_CMD_WOL_FILTER_GET_OUT msgresponse */
#define    MC_CMD_WOL_FILTER_GET_OUT_LEN 4
#define       MC_CMD_WOL_FILTER_GET_OUT_FILTER_ID_OFST 0


/***********************************/
/* MC_CMD_ADD_LIGHTSOUT_OFFLOAD
 * Add a protocol offload to NIC for lights-out state.
 */
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD 0x46

/* MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN msgrequest */
#define    MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_LENMIN 8
#define    MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_LENMAX 252
#define    MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_LEN(num) (4+4*(num))
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_PROTOCOL_OFST 0
#define          MC_CMD_LIGHTSOUT_OFFLOAD_PROTOCOL_ARP 0x1 /* enum */
#define          MC_CMD_LIGHTSOUT_OFFLOAD_PROTOCOL_NS  0x2 /* enum */
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_DATA_OFST 4
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_DATA_LEN 4
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_DATA_MINNUM 1
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_DATA_MAXNUM 62

/* MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_ARP msgrequest */
#define    MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_ARP_LEN 14
/*            MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_PROTOCOL_OFST 0 */
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_ARP_MAC_OFST 4
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_ARP_MAC_LEN 6
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_ARP_IP_OFST 10

/* MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NS msgrequest */
#define    MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NS_LEN 42
/*            MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_PROTOCOL_OFST 0 */
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NS_MAC_OFST 4
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NS_MAC_LEN 6
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NS_SNIPV6_OFST 10
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NS_SNIPV6_LEN 16
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NS_IPV6_OFST 26
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NS_IPV6_LEN 16

/* MC_CMD_ADD_LIGHTSOUT_OFFLOAD_OUT msgresponse */
#define    MC_CMD_ADD_LIGHTSOUT_OFFLOAD_OUT_LEN 4
#define       MC_CMD_ADD_LIGHTSOUT_OFFLOAD_OUT_FILTER_ID_OFST 0


/***********************************/
/* MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD
 * Remove a protocol offload from NIC for lights-out state.
 */
#define MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD 0x47

/* MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_IN msgrequest */
#define    MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_IN_LEN 8
#define       MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_IN_PROTOCOL_OFST 0
#define       MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_IN_FILTER_ID_OFST 4

/* MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_OUT msgresponse */
#define    MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_OUT_LEN 0


/***********************************/
/* MC_CMD_MAC_RESET_RESTORE
 * Restore MAC after block reset.
 */
#define MC_CMD_MAC_RESET_RESTORE 0x48

/* MC_CMD_MAC_RESET_RESTORE_IN msgrequest */
#define    MC_CMD_MAC_RESET_RESTORE_IN_LEN 0

/* MC_CMD_MAC_RESET_RESTORE_OUT msgresponse */
#define    MC_CMD_MAC_RESET_RESTORE_OUT_LEN 0


/***********************************/
/* MC_CMD_TESTASSERT
 */
#define MC_CMD_TESTASSERT 0x49

/* MC_CMD_TESTASSERT_IN msgrequest */
#define    MC_CMD_TESTASSERT_IN_LEN 0

/* MC_CMD_TESTASSERT_OUT msgresponse */
#define    MC_CMD_TESTASSERT_OUT_LEN 0


/***********************************/
/* MC_CMD_WORKAROUND
 * Enable/Disable a given workaround.
 */
#define MC_CMD_WORKAROUND 0x4a

/* MC_CMD_WORKAROUND_IN msgrequest */
#define    MC_CMD_WORKAROUND_IN_LEN 8
#define       MC_CMD_WORKAROUND_IN_TYPE_OFST 0
#define          MC_CMD_WORKAROUND_BUG17230 0x1 /* enum */
#define       MC_CMD_WORKAROUND_IN_ENABLED_OFST 4

/* MC_CMD_WORKAROUND_OUT msgresponse */
#define    MC_CMD_WORKAROUND_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_PHY_MEDIA_INFO
 * Read media-specific data from PHY.
 */
#define MC_CMD_GET_PHY_MEDIA_INFO 0x4b

/* MC_CMD_GET_PHY_MEDIA_INFO_IN msgrequest */
#define    MC_CMD_GET_PHY_MEDIA_INFO_IN_LEN 4
#define       MC_CMD_GET_PHY_MEDIA_INFO_IN_PAGE_OFST 0

/* MC_CMD_GET_PHY_MEDIA_INFO_OUT msgresponse */
#define    MC_CMD_GET_PHY_MEDIA_INFO_OUT_LENMIN 5
#define    MC_CMD_GET_PHY_MEDIA_INFO_OUT_LENMAX 252
#define    MC_CMD_GET_PHY_MEDIA_INFO_OUT_LEN(num) (4+1*(num))
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATALEN_OFST 0
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_OFST 4
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_LEN 1
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_MINNUM 1
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_MAXNUM 248


/***********************************/
/* MC_CMD_NVRAM_TEST
 * Test a particular NVRAM partition.
 */
#define MC_CMD_NVRAM_TEST 0x4c

/* MC_CMD_NVRAM_TEST_IN msgrequest */
#define    MC_CMD_NVRAM_TEST_IN_LEN 4
#define       MC_CMD_NVRAM_TEST_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */

/* MC_CMD_NVRAM_TEST_OUT msgresponse */
#define    MC_CMD_NVRAM_TEST_OUT_LEN 4
#define       MC_CMD_NVRAM_TEST_OUT_RESULT_OFST 0
#define          MC_CMD_NVRAM_TEST_PASS 0x0 /* enum */
#define          MC_CMD_NVRAM_TEST_FAIL 0x1 /* enum */
#define          MC_CMD_NVRAM_TEST_NOTSUPP 0x2 /* enum */


/***********************************/
/* MC_CMD_MRSFP_TWEAK
 * Read status and/or set parameters for the 'mrsfp' driver.
 */
#define MC_CMD_MRSFP_TWEAK 0x4d

/* MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG msgrequest */
#define    MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_LEN 16
#define       MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_TXEQ_LEVEL_OFST 0
#define       MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_TXEQ_DT_CFG_OFST 4
#define       MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_RXEQ_BOOST_OFST 8
#define       MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_RXEQ_DT_CFG_OFST 12

/* MC_CMD_MRSFP_TWEAK_IN_READ_ONLY msgrequest */
#define    MC_CMD_MRSFP_TWEAK_IN_READ_ONLY_LEN 0

/* MC_CMD_MRSFP_TWEAK_OUT msgresponse */
#define    MC_CMD_MRSFP_TWEAK_OUT_LEN 12
#define       MC_CMD_MRSFP_TWEAK_OUT_IOEXP_INPUTS_OFST 0
#define       MC_CMD_MRSFP_TWEAK_OUT_IOEXP_OUTPUTS_OFST 4
#define       MC_CMD_MRSFP_TWEAK_OUT_IOEXP_DIRECTION_OFST 8
#define          MC_CMD_MRSFP_TWEAK_OUT_IOEXP_DIRECTION_OUT 0x0 /* enum */
#define          MC_CMD_MRSFP_TWEAK_OUT_IOEXP_DIRECTION_IN 0x1 /* enum */


/***********************************/
/* MC_CMD_SENSOR_SET_LIMS
 * Adjusts the sensor limits.
 */
#define MC_CMD_SENSOR_SET_LIMS 0x4e

/* MC_CMD_SENSOR_SET_LIMS_IN msgrequest */
#define    MC_CMD_SENSOR_SET_LIMS_IN_LEN 20
#define       MC_CMD_SENSOR_SET_LIMS_IN_SENSOR_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_SENSOR_INFO/MC_CMD_SENSOR_INFO_OUT/MASK */
#define       MC_CMD_SENSOR_SET_LIMS_IN_LOW0_OFST 4
#define       MC_CMD_SENSOR_SET_LIMS_IN_HI0_OFST 8
#define       MC_CMD_SENSOR_SET_LIMS_IN_LOW1_OFST 12
#define       MC_CMD_SENSOR_SET_LIMS_IN_HI1_OFST 16

/* MC_CMD_SENSOR_SET_LIMS_OUT msgresponse */
#define    MC_CMD_SENSOR_SET_LIMS_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_RESOURCE_LIMITS
 */
#define MC_CMD_GET_RESOURCE_LIMITS 0x4f

/* MC_CMD_GET_RESOURCE_LIMITS_IN msgrequest */
#define    MC_CMD_GET_RESOURCE_LIMITS_IN_LEN 0

/* MC_CMD_GET_RESOURCE_LIMITS_OUT msgresponse */
#define    MC_CMD_GET_RESOURCE_LIMITS_OUT_LEN 16
#define       MC_CMD_GET_RESOURCE_LIMITS_OUT_BUFTBL_OFST 0
#define       MC_CMD_GET_RESOURCE_LIMITS_OUT_EVQ_OFST 4
#define       MC_CMD_GET_RESOURCE_LIMITS_OUT_RXQ_OFST 8
#define       MC_CMD_GET_RESOURCE_LIMITS_OUT_TXQ_OFST 12

/* MC_CMD_RESOURCE_SPECIFIER enum */
#define          MC_CMD_RESOURCE_INSTANCE_ANY 0xffffffff /* enum */
#define          MC_CMD_RESOURCE_INSTANCE_NONE 0xfffffffe /* enum */


#endif /* MCDI_PCOL_H */
