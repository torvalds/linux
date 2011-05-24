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

/**
 * MCDI version 1
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

#define MCDI_EVENT_DATA_LBN 0
#define MCDI_EVENT_DATA_WIDTH 32
#define MCDI_EVENT_CONT_LBN 32
#define MCDI_EVENT_CONT_WIDTH 1
#define MCDI_EVENT_LEVEL_LBN 33
#define MCDI_EVENT_LEVEL_WIDTH 3
#define MCDI_EVENT_LEVEL_INFO (0)
#define MCDI_EVENT_LEVEL_WARN (1)
#define MCDI_EVENT_LEVEL_ERR (2)
#define MCDI_EVENT_LEVEL_FATAL (3)
#define MCDI_EVENT_SRC_LBN 36
#define MCDI_EVENT_SRC_WIDTH 8
#define MCDI_EVENT_CODE_LBN 44
#define MCDI_EVENT_CODE_WIDTH 8
#define MCDI_EVENT_CODE_BADSSERT (1)
#define MCDI_EVENT_CODE_PMNOTICE (2)
#define MCDI_EVENT_CODE_CMDDONE (3)
#define  MCDI_EVENT_CMDDONE_SEQ_LBN 0
#define  MCDI_EVENT_CMDDONE_SEQ_WIDTH 8
#define  MCDI_EVENT_CMDDONE_DATALEN_LBN 8
#define  MCDI_EVENT_CMDDONE_DATALEN_WIDTH 8
#define  MCDI_EVENT_CMDDONE_ERRNO_LBN 16
#define  MCDI_EVENT_CMDDONE_ERRNO_WIDTH 8
#define MCDI_EVENT_CODE_LINKCHANGE (4)
#define  MCDI_EVENT_LINKCHANGE_LP_CAP_LBN 0
#define  MCDI_EVENT_LINKCHANGE_LP_CAP_WIDTH 16
#define  MCDI_EVENT_LINKCHANGE_SPEED_LBN 16
#define  MCDI_EVENT_LINKCHANGE_SPEED_WIDTH 4
#define  MCDI_EVENT_LINKCHANGE_SPEED_100M 1
#define  MCDI_EVENT_LINKCHANGE_SPEED_1G 2
#define  MCDI_EVENT_LINKCHANGE_SPEED_10G 3
#define  MCDI_EVENT_LINKCHANGE_FCNTL_LBN 20
#define  MCDI_EVENT_LINKCHANGE_FCNTL_WIDTH 4
#define  MCDI_EVENT_LINKCHANGE_LINK_FLAGS_LBN 24
#define  MCDI_EVENT_LINKCHANGE_LINK_FLAGS_WIDTH 8
#define MCDI_EVENT_CODE_SENSOREVT (5)
#define  MCDI_EVENT_SENSOREVT_MONITOR_LBN 0
#define  MCDI_EVENT_SENSOREVT_MONITOR_WIDTH 8
#define  MCDI_EVENT_SENSOREVT_STATE_LBN 8
#define  MCDI_EVENT_SENSOREVT_STATE_WIDTH 8
#define  MCDI_EVENT_SENSOREVT_VALUE_LBN 16
#define  MCDI_EVENT_SENSOREVT_VALUE_WIDTH 16
#define MCDI_EVENT_CODE_SCHEDERR (6)
#define MCDI_EVENT_CODE_REBOOT (7)
#define MCDI_EVENT_CODE_MAC_STATS_DMA (8)
#define  MCDI_EVENT_MAC_STATS_DMA_GENERATION_LBN 0
#define  MCDI_EVENT_MAC_STATS_DMA_GENERATION_WIDTH 32

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


/* MC_CMD_READ32: (debug, variadic out)
 * Read multiple 32byte words from MC memory
 */
#define MC_CMD_READ32 0x01
#define MC_CMD_READ32_IN_LEN 8
#define MC_CMD_READ32_IN_ADDR_OFST 0
#define MC_CMD_READ32_IN_NUMWORDS_OFST 4
#define MC_CMD_READ32_OUT_LEN(_numwords) \
	(4 * (_numwords))
#define MC_CMD_READ32_OUT_BUFFER_OFST 0

/* MC_CMD_WRITE32: (debug, variadic in)
 * Write multiple 32byte words to MC memory
 */
#define MC_CMD_WRITE32 0x02
#define MC_CMD_WRITE32_IN_LEN(_numwords) (((_numwords) * 4) + 4)
#define MC_CMD_WRITE32_IN_ADDR_OFST 0
#define MC_CMD_WRITE32_IN_BUFFER_OFST 4
#define MC_CMD_WRITE32_OUT_LEN 0

/* MC_CMD_COPYCODE: (debug)
 * Copy MC code between two locations and jump
 */
#define MC_CMD_COPYCODE 0x03
#define MC_CMD_COPYCODE_IN_LEN 16
#define MC_CMD_COPYCODE_IN_SRC_ADDR_OFST 0
#define MC_CMD_COPYCODE_IN_DEST_ADDR_OFST 4
#define MC_CMD_COPYCODE_IN_NUMWORDS_OFST 8
#define MC_CMD_COPYCODE_IN_JUMP_OFST 12
/* Control should return to the caller rather than jumping */
#define MC_CMD_COPYCODE_JUMP_NONE 1
#define MC_CMD_COPYCODE_OUT_LEN 0

/* MC_CMD_SET_FUNC: (debug)
 * Select function for function-specific commands.
 */
#define MC_CMD_SET_FUNC 0x04
#define MC_CMD_SET_FUNC_IN_LEN 4
#define MC_CMD_SET_FUNC_IN_FUNC_OFST 0
#define MC_CMD_SET_FUNC_OUT_LEN 0

/* MC_CMD_GET_BOOT_STATUS:
 * Get the instruction address from which the MC booted.
 */
#define MC_CMD_GET_BOOT_STATUS 0x05
#define MC_CMD_GET_BOOT_STATUS_IN_LEN 0
#define MC_CMD_GET_BOOT_STATUS_OUT_LEN 8
#define MC_CMD_GET_BOOT_STATUS_OUT_BOOT_OFFSET_OFST 0
#define MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_OFST 4
/* Reboot caused by watchdog */
#define MC_CMD_GET_BOOT_STATUS_FLAGS_WATCHDOG_LBN   (0)
#define MC_CMD_GET_BOOT_STATUS_FLAGS_WATCHDOG_WIDTH (1)
/* MC booted from primary flash partition */
#define MC_CMD_GET_BOOT_STATUS_FLAGS_PRIMARY_LBN    (1)
#define MC_CMD_GET_BOOT_STATUS_FLAGS_PRIMARY_WIDTH  (1)
/* MC booted from backup flash partition */
#define MC_CMD_GET_BOOT_STATUS_FLAGS_BACKUP_LBN     (2)
#define MC_CMD_GET_BOOT_STATUS_FLAGS_BACKUP_WIDTH   (1)

/* MC_CMD_GET_ASSERTS: (debug, variadic out)
 * Get (and optionally clear) the current assertion status.
 *
 * Only OUT.GLOBAL_FLAGS is guaranteed to exist in the completion
 * payload. The other fields will only be present if
 * OUT.GLOBAL_FLAGS != NO_FAILS
 */
#define MC_CMD_GET_ASSERTS 0x06
#define MC_CMD_GET_ASSERTS_IN_LEN 4
#define MC_CMD_GET_ASSERTS_IN_CLEAR_OFST 0
#define MC_CMD_GET_ASSERTS_OUT_LEN 140
/* Assertion status flag */
#define MC_CMD_GET_ASSERTS_OUT_GLOBAL_FLAGS_OFST 0
/*! No assertions have failed. */
#define MC_CMD_GET_ASSERTS_FLAGS_NO_FAILS 1
/*! A system-level assertion has failed. */
#define MC_CMD_GET_ASSERTS_FLAGS_SYS_FAIL 2
/*! A thread-level assertion has failed. */
#define MC_CMD_GET_ASSERTS_FLAGS_THR_FAIL 3
/*! The system was reset by the watchdog. */
#define MC_CMD_GET_ASSERTS_FLAGS_WDOG_FIRED 4
/* Failing PC value */
#define MC_CMD_GET_ASSERTS_OUT_SAVED_PC_OFFS_OFST 4
/* Saved GP regs */
#define MC_CMD_GET_ASSERTS_OUT_GP_REGS_OFFS_OFST 8
#define MC_CMD_GET_ASSERTS_OUT_GP_REGS_LEN 124
/* Failing thread address */
#define MC_CMD_GET_ASSERTS_OUT_THREAD_OFFS_OFST 132

/* MC_CMD_LOG_CTRL:
 * Determine the output stream for various events and messages
 */
#define MC_CMD_LOG_CTRL 0x07
#define MC_CMD_LOG_CTRL_IN_LEN 8
#define MC_CMD_LOG_CTRL_IN_LOG_DEST_OFST 0
#define MC_CMD_LOG_CTRL_IN_LOG_DEST_UART (1)
#define MC_CMD_LOG_CTRL_IN_LOG_DEST_EVQ (2)
#define MC_CMD_LOG_CTRL_IN_LOG_DEST_EVQ_OFST 4
#define MC_CMD_LOG_CTRL_OUT_LEN 0

/* MC_CMD_GET_VERSION:
 * Get version information about the MC firmware
 */
#define MC_CMD_GET_VERSION 0x08
#define MC_CMD_GET_VERSION_IN_LEN 0
#define MC_CMD_GET_VERSION_V0_OUT_LEN 4
#define MC_CMD_GET_VERSION_V1_OUT_LEN 32
#define MC_CMD_GET_VERSION_OUT_FIRMWARE_OFST 0
/* Reserved version number to indicate "any" version. */
#define MC_CMD_GET_VERSION_OUT_FIRMWARE_ANY 0xffffffff
/* The version response of a boot ROM awaiting rescue */
#define MC_CMD_GET_VERSION_OUT_FIRMWARE_BOOTROM 0xb0070000
#define MC_CMD_GET_VERSION_V1_OUT_PCOL_OFST 4
/* 128bit mask of functions supported by the current firmware */
#define MC_CMD_GET_VERSION_V1_OUT_SUPPORTED_FUNCS_OFST 8
/* The command set exported by the boot ROM (MCDI v0) */
#define MC_CMD_GET_VERSION_V0_SUPPORTED_FUNCS {		\
	(1 << MC_CMD_READ32)	|			\
	(1 << MC_CMD_WRITE32)	|			\
	(1 << MC_CMD_COPYCODE)	|			\
	(1 << MC_CMD_GET_VERSION),			\
	0, 0, 0 }
#define MC_CMD_GET_VERSION_OUT_VERSION_OFST 24

/* Vectors in the boot ROM */
/* Point to the copycode entry point. */
#define MC_BOOTROM_COPYCODE_VEC (0x7f4)
/* Points to the recovery mode entry point. */
#define MC_BOOTROM_NOFLASH_VEC (0x7f8)

/* Test execution limits */
#define MC_TESTEXEC_VARIANT_COUNT 16
#define MC_TESTEXEC_RESULT_COUNT 7

/* MC_CMD_SET_TESTVARS: (debug, variadic in)
 * Write variant words for test.
 *
 * The user supplies a bitmap of the variants they wish to set.
 * They must ensure that IN.LEN >= 4 + 4 * ffs(BITMAP)
 */
#define MC_CMD_SET_TESTVARS 0x09
#define MC_CMD_SET_TESTVARS_IN_LEN(_numwords)	\
  (4 + 4*(_numwords))
#define MC_CMD_SET_TESTVARS_IN_ARGS_BITMAP_OFST 0
/* Up to MC_TESTEXEC_VARIANT_COUNT of 32byte words start here */
#define MC_CMD_SET_TESTVARS_IN_ARGS_BUFFER_OFST 4
#define MC_CMD_SET_TESTVARS_OUT_LEN 0

/* MC_CMD_GET_TESTRCS: (debug, variadic out)
 * Return result words from test.
 */
#define MC_CMD_GET_TESTRCS 0x0a
#define MC_CMD_GET_TESTRCS_IN_LEN 4
#define MC_CMD_GET_TESTRCS_IN_NUMWORDS_OFST 0
#define MC_CMD_GET_TESTRCS_OUT_LEN(_numwords) \
	(4 * (_numwords))
#define MC_CMD_GET_TESTRCS_OUT_BUFFER_OFST 0

/* MC_CMD_RUN_TEST: (debug)
 * Run the test exported by this firmware image
 */
#define MC_CMD_RUN_TEST 0x0b
#define MC_CMD_RUN_TEST_IN_LEN 0
#define MC_CMD_RUN_TEST_OUT_LEN 0

/* MC_CMD_CSR_READ32: (debug, variadic out)
 * Read 32bit words from the indirect memory map
 */
#define MC_CMD_CSR_READ32 0x0c
#define MC_CMD_CSR_READ32_IN_LEN 12
#define MC_CMD_CSR_READ32_IN_ADDR_OFST 0
#define MC_CMD_CSR_READ32_IN_STEP_OFST 4
#define MC_CMD_CSR_READ32_IN_NUMWORDS_OFST 8
#define MC_CMD_CSR_READ32_OUT_LEN(_numwords)	\
	(((_numwords) * 4) + 4)
/* IN.NUMWORDS of 32bit words start here */
#define MC_CMD_CSR_READ32_OUT_BUFFER_OFST 0
#define MC_CMD_CSR_READ32_OUT_IREG_STATUS_OFST(_numwords)	\
	((_numwords) * 4)

/* MC_CMD_CSR_WRITE32: (debug, variadic in)
 * Write 32bit dwords to the indirect memory map
 */
#define MC_CMD_CSR_WRITE32 0x0d
#define MC_CMD_CSR_WRITE32_IN_LEN(_numwords)	\
	(((_numwords) * 4) + 8)
#define MC_CMD_CSR_WRITE32_IN_ADDR_OFST 0
#define MC_CMD_CSR_WRITE32_IN_STEP_OFST 4
/* Multiple 32bit words of data to write start here */
#define MC_CMD_CSR_WRITE32_IN_BUFFER_OFST 8
#define MC_CMD_CSR_WRITE32_OUT_LEN 4
#define MC_CMD_CSR_WRITE32_OUT_STATUS_OFST 0

/* MC_CMD_JTAG_WORK: (debug, fpga only)
 * Process JTAG work buffer for RBF acceleration.
 *
 *  Host: bit count, (up to) 32 words of data to clock out to JTAG
 *   (bits 1,0=TMS,TDO for first bit; bits 3,2=TMS,TDO for second bit, etc.)
 *  MC: bit count, (up to) 32 words of data clocked in from JTAG
 *   (bit 0=TDI for first bit, bit 1=TDI for second bit, etc.; [31:16] unused)
 */
#define MC_CMD_JTAG_WORK 0x0e

/* MC_CMD_STACKINFO: (debug, variadic out)
 * Get stack information
 *
 * Host: nothing
 * MC: (thread ptr, stack size, free space) for each thread in system
 */
#define MC_CMD_STACKINFO 0x0f

/* MC_CMD_MDIO_READ:
 * MDIO register read
 */
#define MC_CMD_MDIO_READ 0x10
#define MC_CMD_MDIO_READ_IN_LEN 16
#define MC_CMD_MDIO_READ_IN_BUS_OFST 0
#define MC_CMD_MDIO_READ_IN_PRTAD_OFST 4
#define MC_CMD_MDIO_READ_IN_DEVAD_OFST 8
#define MC_CMD_MDIO_READ_IN_ADDR_OFST 12
#define MC_CMD_MDIO_READ_OUT_LEN 8
#define MC_CMD_MDIO_READ_OUT_VALUE_OFST 0
#define MC_CMD_MDIO_READ_OUT_STATUS_OFST 4

/* MC_CMD_MDIO_WRITE:
 * MDIO register write
 */
#define MC_CMD_MDIO_WRITE 0x11
#define MC_CMD_MDIO_WRITE_IN_LEN 20
#define MC_CMD_MDIO_WRITE_IN_BUS_OFST 0
#define MC_CMD_MDIO_WRITE_IN_PRTAD_OFST 4
#define MC_CMD_MDIO_WRITE_IN_DEVAD_OFST 8
#define MC_CMD_MDIO_WRITE_IN_ADDR_OFST 12
#define MC_CMD_MDIO_WRITE_IN_VALUE_OFST 16
#define MC_CMD_MDIO_WRITE_OUT_LEN 4
#define MC_CMD_MDIO_WRITE_OUT_STATUS_OFST 0

/* By default all the MCDI MDIO operations perform clause45 mode.
 * If you want to use clause22 then set DEVAD = MC_CMD_MDIO_CLAUSE22.
 */
#define MC_CMD_MDIO_CLAUSE22 32

/* There are two MDIO buses: one for the internal PHY, and one for external
 * devices.
 */
#define MC_CMD_MDIO_BUS_INTERNAL 0
#define MC_CMD_MDIO_BUS_EXTERNAL 1

/* The MDIO commands return the raw status bits from the MDIO block.  A "good"
 * transaction should have the DONE bit set and all other bits clear.
 */
#define MC_CMD_MDIO_STATUS_GOOD 0x08


/* MC_CMD_DBI_WRITE: (debug)
 * Write DBI register(s)
 *
 * Host: address, byte-enables (and VF selection, and cs2 flag),
 *       value [,address ...]
 * MC: nothing
 */
#define MC_CMD_DBI_WRITE 0x12
#define MC_CMD_DBI_WRITE_IN_LEN(_numwords)		\
	(12 * (_numwords))
#define MC_CMD_DBI_WRITE_IN_ADDRESS_OFST(_word)		\
	(((_word) * 12) + 0)
#define MC_CMD_DBI_WRITE_IN_BYTE_MASK_OFST(_word)	\
	(((_word) * 12) + 4)
#define MC_CMD_DBI_WRITE_IN_VALUE_OFST(_word)		\
	(((_word) * 12) + 8)
#define MC_CMD_DBI_WRITE_OUT_LEN 0

/* MC_CMD_DBI_READ: (debug)
 * Read DBI register(s)
 *
 * Host: address, [,address ...]
 * MC: value [,value ...]
 * (note: this does not support reading from VFs, but is retained for backwards
 * compatibility; see MC_CMD_DBI_READX below)
 */
#define MC_CMD_DBI_READ 0x13
#define MC_CMD_DBI_READ_IN_LEN(_numwords)		\
	(4 * (_numwords))
#define MC_CMD_DBI_READ_OUT_LEN(_numwords)		\
	(4 * (_numwords))

/* MC_CMD_PORT_READ32: (debug)
 * Read a 32-bit register from the indirect port register map.
 *
 * The port to access is implied by the Shared memory channel used.
 */
#define MC_CMD_PORT_READ32 0x14
#define MC_CMD_PORT_READ32_IN_LEN 4
#define MC_CMD_PORT_READ32_IN_ADDR_OFST 0
#define MC_CMD_PORT_READ32_OUT_LEN 8
#define MC_CMD_PORT_READ32_OUT_VALUE_OFST 0
#define MC_CMD_PORT_READ32_OUT_STATUS_OFST 4

/* MC_CMD_PORT_WRITE32: (debug)
 * Write a 32-bit register to the indirect port register map.
 *
 * The port to access is implied by the Shared memory channel used.
 */
#define MC_CMD_PORT_WRITE32 0x15
#define MC_CMD_PORT_WRITE32_IN_LEN 8
#define MC_CMD_PORT_WRITE32_IN_ADDR_OFST 0
#define MC_CMD_PORT_WRITE32_IN_VALUE_OFST 4
#define MC_CMD_PORT_WRITE32_OUT_LEN 4
#define MC_CMD_PORT_WRITE32_OUT_STATUS_OFST 0

/* MC_CMD_PORT_READ128: (debug)
 * Read a 128-bit register from indirect port register map
 *
 * The port to access is implied by the Shared memory channel used.
 */
#define MC_CMD_PORT_READ128 0x16
#define MC_CMD_PORT_READ128_IN_LEN 4
#define MC_CMD_PORT_READ128_IN_ADDR_OFST 0
#define MC_CMD_PORT_READ128_OUT_LEN 20
#define MC_CMD_PORT_READ128_OUT_VALUE_OFST 0
#define MC_CMD_PORT_READ128_OUT_STATUS_OFST 16

/* MC_CMD_PORT_WRITE128: (debug)
 * Write a 128-bit register to indirect port register map.
 *
 * The port to access is implied by the Shared memory channel used.
 */
#define MC_CMD_PORT_WRITE128 0x17
#define MC_CMD_PORT_WRITE128_IN_LEN 20
#define MC_CMD_PORT_WRITE128_IN_ADDR_OFST 0
#define MC_CMD_PORT_WRITE128_IN_VALUE_OFST 4
#define MC_CMD_PORT_WRITE128_OUT_LEN 4
#define MC_CMD_PORT_WRITE128_OUT_STATUS_OFST 0

/* MC_CMD_GET_BOARD_CFG:
 * Returns the MC firmware configuration structure
 *
 * The FW_SUBTYPE_LIST contains a 16-bit value for each of the 12 types of
 * NVRAM area.  The values are defined in the firmware/mc/platform/<xxx>.c file
 * for a specific board type, but otherwise have no meaning to the MC; they
 * are used by the driver to manage selection of appropriate firmware updates.
 */
#define MC_CMD_GET_BOARD_CFG 0x18
#define MC_CMD_GET_BOARD_CFG_IN_LEN 0
#define MC_CMD_GET_BOARD_CFG_OUT_LEN 96
#define MC_CMD_GET_BOARD_CFG_OUT_BOARD_TYPE_OFST 0
#define MC_CMD_GET_BOARD_CFG_OUT_BOARD_NAME_OFST 4
#define MC_CMD_GET_BOARD_CFG_OUT_BOARD_NAME_LEN 32
#define MC_CMD_GET_BOARD_CFG_OUT_CAPABILITIES_PORT0_OFST 36
#define MC_CMD_GET_BOARD_CFG_OUT_CAPABILITIES_PORT1_OFST 40
#define MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT0_OFST 44
#define MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT0_LEN 6
#define MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT1_OFST 50
#define MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT1_LEN 6
#define MC_CMD_GET_BOARD_CFG_OUT_MAC_COUNT_PORT0_OFST 56
#define MC_CMD_GET_BOARD_CFG_OUT_MAC_COUNT_PORT1_OFST 60
#define MC_CMD_GET_BOARD_CFG_OUT_MAC_STRIDE_PORT0_OFST 64
#define MC_CMD_GET_BOARD_CFG_OUT_MAC_STRIDE_PORT1_OFST 68
#define MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_OFST 72
#define MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_LEN 24

/* MC_CMD_DBI_READX: (debug)
 * Read DBI register(s) -- extended functionality
 *
 * Host: vf selection, address, [,vf selection ...]
 * MC: value [,value ...]
 */
#define MC_CMD_DBI_READX 0x19
#define MC_CMD_DBI_READX_IN_LEN(_numwords)	\
  (8*(_numwords))
#define MC_CMD_DBI_READX_OUT_LEN(_numwords)	\
  (4*(_numwords))

/* MC_CMD_SET_RAND_SEED:
 * Set the 16byte seed for the MC pseudo-random generator
 */
#define MC_CMD_SET_RAND_SEED 0x1a
#define MC_CMD_SET_RAND_SEED_IN_LEN 16
#define MC_CMD_SET_RAND_SEED_IN_SEED_OFST 0
#define MC_CMD_SET_RAND_SEED_OUT_LEN 0

/* MC_CMD_LTSSM_HIST: (debug)
 * Retrieve the history of the LTSSM, if the build supports it.
 *
 * Host: nothing
 * MC: variable number of LTSSM values, as bytes
 * The history is read-to-clear.
 */
#define MC_CMD_LTSSM_HIST 0x1b

/* MC_CMD_DRV_ATTACH:
 * Inform MCPU that this port is managed on the host (i.e. driver active)
 */
#define MC_CMD_DRV_ATTACH 0x1c
#define MC_CMD_DRV_ATTACH_IN_LEN 8
#define MC_CMD_DRV_ATTACH_IN_NEW_STATE_OFST 0
#define MC_CMD_DRV_ATTACH_IN_UPDATE_OFST 4
#define MC_CMD_DRV_ATTACH_OUT_LEN 4
#define MC_CMD_DRV_ATTACH_OUT_OLD_STATE_OFST 0

/* MC_CMD_NCSI_PROD: (debug)
 * Trigger an NC-SI event (and possibly an AEN in response)
 */
#define MC_CMD_NCSI_PROD 0x1d
#define MC_CMD_NCSI_PROD_IN_LEN 4
#define MC_CMD_NCSI_PROD_IN_EVENTS_OFST 0
#define MC_CMD_NCSI_PROD_LINKCHANGE_LBN 0
#define MC_CMD_NCSI_PROD_LINKCHANGE_WIDTH 1
#define MC_CMD_NCSI_PROD_RESET_LBN 1
#define MC_CMD_NCSI_PROD_RESET_WIDTH 1
#define MC_CMD_NCSI_PROD_DRVATTACH_LBN 2
#define MC_CMD_NCSI_PROD_DRVATTACH_WIDTH 1
#define MC_CMD_NCSI_PROD_OUT_LEN 0

/* Enumeration */
#define MC_CMD_NCSI_PROD_LINKCHANGE 0
#define MC_CMD_NCSI_PROD_RESET 1
#define MC_CMD_NCSI_PROD_DRVATTACH 2

/* MC_CMD_DEVEL: (debug)
 * Reserved for development
 */
#define MC_CMD_DEVEL 0x1e

/* MC_CMD_SHMUART: (debug)
 * Route UART output to circular buffer in shared memory instead.
 */
#define MC_CMD_SHMUART 0x1f
#define MC_CMD_SHMUART_IN_FLAG_OFST 0
#define MC_CMD_SHMUART_IN_LEN 4
#define MC_CMD_SHMUART_OUT_LEN 0

/* MC_CMD_PORT_RESET:
 * Generic per-port reset. There is no equivalent for per-board reset.
 *
 * Locks required: None
 * Return code: 0, ETIME
 */
#define MC_CMD_PORT_RESET 0x20
#define MC_CMD_PORT_RESET_IN_LEN 0
#define MC_CMD_PORT_RESET_OUT_LEN 0

/* MC_CMD_RESOURCE_LOCK:
 * Generic resource lock/unlock interface.
 *
 * Locks required: None
 * Return code: 0,
 *              EBUSY (if trylock is contended by other port),
 *              EDEADLK (if trylock is already acquired by this port)
 *              EINVAL (if unlock doesn't own the lock)
 */
#define MC_CMD_RESOURCE_LOCK 0x21
#define MC_CMD_RESOURCE_LOCK_IN_LEN 8
#define MC_CMD_RESOURCE_LOCK_IN_ACTION_OFST 0
#define MC_CMD_RESOURCE_LOCK_ACTION_TRYLOCK 1
#define MC_CMD_RESOURCE_LOCK_ACTION_UNLOCK 0
#define MC_CMD_RESOURCE_LOCK_IN_RESOURCE_OFST 4
#define MC_CMD_RESOURCE_LOCK_I2C 2
#define MC_CMD_RESOURCE_LOCK_PHY 3
#define MC_CMD_RESOURCE_LOCK_OUT_LEN 0

/* MC_CMD_SPI_COMMAND: (variadic in, variadic out)
 * Read/Write to/from the SPI device.
 *
 * Locks required: SPI_LOCK
 * Return code: 0, ETIME, EINVAL, EACCES (if SPI_LOCK is not held)
 */
#define MC_CMD_SPI_COMMAND 0x22
#define MC_CMD_SPI_COMMAND_IN_LEN(_write_bytes)	(12 + (_write_bytes))
#define MC_CMD_SPI_COMMAND_IN_ARGS_OFST 0
#define MC_CMD_SPI_COMMAND_IN_ARGS_ADDRESS_OFST 0
#define MC_CMD_SPI_COMMAND_IN_ARGS_READ_BYTES_OFST 4
#define MC_CMD_SPI_COMMAND_IN_ARGS_CHIP_SELECT_OFST 8
/* Data to write here */
#define MC_CMD_SPI_COMMAND_IN_WRITE_BUFFER_OFST 12
#define MC_CMD_SPI_COMMAND_OUT_LEN(_read_bytes) (_read_bytes)
/* Data read here */
#define MC_CMD_SPI_COMMAND_OUT_READ_BUFFER_OFST 0

/* MC_CMD_I2C_READ_WRITE: (variadic in, variadic out)
 * Read/Write to/from the I2C bus.
 *
 * Locks required: I2C_LOCK
 * Return code: 0, ETIME, EINVAL, EACCES (if I2C_LOCK is not held)
 */
#define MC_CMD_I2C_RW 0x23
#define MC_CMD_I2C_RW_IN_LEN(_write_bytes) (8 + (_write_bytes))
#define MC_CMD_I2C_RW_IN_ARGS_OFST 0
#define MC_CMD_I2C_RW_IN_ARGS_ADDR_OFST 0
#define MC_CMD_I2C_RW_IN_ARGS_READ_BYTES_OFST 4
/* Data to write here */
#define MC_CMD_I2C_RW_IN_WRITE_BUFFER_OFSET 8
#define MC_CMD_I2C_RW_OUT_LEN(_read_bytes) (_read_bytes)
/* Data read here */
#define MC_CMD_I2C_RW_OUT_READ_BUFFER_OFST 0

/* Generic phy capability bitmask */
#define MC_CMD_PHY_CAP_10HDX_LBN 1
#define MC_CMD_PHY_CAP_10HDX_WIDTH 1
#define MC_CMD_PHY_CAP_10FDX_LBN 2
#define MC_CMD_PHY_CAP_10FDX_WIDTH 1
#define MC_CMD_PHY_CAP_100HDX_LBN 3
#define MC_CMD_PHY_CAP_100HDX_WIDTH 1
#define MC_CMD_PHY_CAP_100FDX_LBN 4
#define MC_CMD_PHY_CAP_100FDX_WIDTH 1
#define MC_CMD_PHY_CAP_1000HDX_LBN 5
#define MC_CMD_PHY_CAP_1000HDX_WIDTH 1
#define MC_CMD_PHY_CAP_1000FDX_LBN 6
#define MC_CMD_PHY_CAP_1000FDX_WIDTH 1
#define MC_CMD_PHY_CAP_10000FDX_LBN 7
#define MC_CMD_PHY_CAP_10000FDX_WIDTH 1
#define MC_CMD_PHY_CAP_PAUSE_LBN 8
#define MC_CMD_PHY_CAP_PAUSE_WIDTH 1
#define MC_CMD_PHY_CAP_ASYM_LBN 9
#define MC_CMD_PHY_CAP_ASYM_WIDTH 1
#define MC_CMD_PHY_CAP_AN_LBN 10
#define MC_CMD_PHY_CAP_AN_WIDTH 1

/* Generic loopback enumeration */
#define MC_CMD_LOOPBACK_NONE 0
#define MC_CMD_LOOPBACK_DATA 1
#define MC_CMD_LOOPBACK_GMAC 2
#define MC_CMD_LOOPBACK_XGMII 3
#define MC_CMD_LOOPBACK_XGXS 4
#define MC_CMD_LOOPBACK_XAUI 5
#define MC_CMD_LOOPBACK_GMII 6
#define MC_CMD_LOOPBACK_SGMII 7
#define MC_CMD_LOOPBACK_XGBR 8
#define MC_CMD_LOOPBACK_XFI 9
#define MC_CMD_LOOPBACK_XAUI_FAR 10
#define MC_CMD_LOOPBACK_GMII_FAR 11
#define MC_CMD_LOOPBACK_SGMII_FAR 12
#define MC_CMD_LOOPBACK_XFI_FAR 13
#define MC_CMD_LOOPBACK_GPHY 14
#define MC_CMD_LOOPBACK_PHYXS 15
#define MC_CMD_LOOPBACK_PCS 16
#define MC_CMD_LOOPBACK_PMAPMD 17
#define MC_CMD_LOOPBACK_XPORT 18
#define MC_CMD_LOOPBACK_XGMII_WS 19
#define MC_CMD_LOOPBACK_XAUI_WS 20
#define MC_CMD_LOOPBACK_XAUI_WS_FAR 21
#define MC_CMD_LOOPBACK_XAUI_WS_NEAR 22
#define MC_CMD_LOOPBACK_GMII_WS 23
#define MC_CMD_LOOPBACK_XFI_WS 24
#define MC_CMD_LOOPBACK_XFI_WS_FAR 25
#define MC_CMD_LOOPBACK_PHYXS_WS 26

/* Generic PHY statistics enumeration */
#define MC_CMD_OUI 0
#define MC_CMD_PMA_PMD_LINK_UP 1
#define MC_CMD_PMA_PMD_RX_FAULT 2
#define MC_CMD_PMA_PMD_TX_FAULT 3
#define MC_CMD_PMA_PMD_SIGNAL 4
#define MC_CMD_PMA_PMD_SNR_A 5
#define MC_CMD_PMA_PMD_SNR_B 6
#define MC_CMD_PMA_PMD_SNR_C 7
#define MC_CMD_PMA_PMD_SNR_D 8
#define MC_CMD_PCS_LINK_UP 9
#define MC_CMD_PCS_RX_FAULT 10
#define MC_CMD_PCS_TX_FAULT 11
#define MC_CMD_PCS_BER 12
#define MC_CMD_PCS_BLOCK_ERRORS 13
#define MC_CMD_PHYXS_LINK_UP 14
#define MC_CMD_PHYXS_RX_FAULT 15
#define MC_CMD_PHYXS_TX_FAULT 16
#define MC_CMD_PHYXS_ALIGN 17
#define MC_CMD_PHYXS_SYNC 18
#define MC_CMD_AN_LINK_UP 19
#define MC_CMD_AN_COMPLETE 20
#define MC_CMD_AN_10GBT_STATUS 21
#define MC_CMD_CL22_LINK_UP 22
#define MC_CMD_PHY_NSTATS 23

/* MC_CMD_GET_PHY_CFG:
 * Report PHY configuration.  This guarantees to succeed even if the PHY is in
 * a "zombie" state.
 *
 * Locks required: None
 * Return code: 0
 */
#define MC_CMD_GET_PHY_CFG 0x24

#define MC_CMD_GET_PHY_CFG_IN_LEN 0
#define MC_CMD_GET_PHY_CFG_OUT_LEN 72

#define MC_CMD_GET_PHY_CFG_OUT_FLAGS_OFST 0
#define MC_CMD_GET_PHY_CFG_PRESENT_LBN 0
#define MC_CMD_GET_PHY_CFG_PRESENT_WIDTH 1
#define MC_CMD_GET_PHY_CFG_BIST_CABLE_SHORT_LBN 1
#define MC_CMD_GET_PHY_CFG_BIST_CABLE_SHORT_WIDTH 1
#define MC_CMD_GET_PHY_CFG_BIST_CABLE_LONG_LBN 2
#define MC_CMD_GET_PHY_CFG_BIST_CABLE_LONG_WIDTH 1
#define MC_CMD_GET_PHY_CFG_LOWPOWER_LBN 3
#define MC_CMD_GET_PHY_CFG_LOWPOWER_WIDTH 1
#define MC_CMD_GET_PHY_CFG_POWEROFF_LBN 4
#define MC_CMD_GET_PHY_CFG_POWEROFF_WIDTH 1
#define MC_CMD_GET_PHY_CFG_TXDIS_LBN 5
#define MC_CMD_GET_PHY_CFG_TXDIS_WIDTH 1
#define MC_CMD_GET_PHY_CFG_BIST_LBN 6
#define MC_CMD_GET_PHY_CFG_BIST_WIDTH 1
#define MC_CMD_GET_PHY_CFG_OUT_TYPE_OFST 4
/* Bitmask of supported capabilities */
#define MC_CMD_GET_PHY_CFG_OUT_SUPPORTED_CAP_OFST 8
#define MC_CMD_GET_PHY_CFG_OUT_CHANNEL_OFST 12
#define MC_CMD_GET_PHY_CFG_OUT_PRT_OFST 16
/* PHY statistics bitmap */
#define MC_CMD_GET_PHY_CFG_OUT_STATS_MASK_OFST 20
/* PHY type/name string */
#define MC_CMD_GET_PHY_CFG_OUT_NAME_OFST 24
#define MC_CMD_GET_PHY_CFG_OUT_NAME_LEN 20
#define MC_CMD_GET_PHY_CFG_OUT_MEDIA_TYPE_OFST 44
#define MC_CMD_MEDIA_XAUI 1
#define MC_CMD_MEDIA_CX4 2
#define MC_CMD_MEDIA_KX4 3
#define MC_CMD_MEDIA_XFP 4
#define MC_CMD_MEDIA_SFP_PLUS 5
#define MC_CMD_MEDIA_BASE_T 6
/* MDIO "MMDS" supported */
#define MC_CMD_GET_PHY_CFG_OUT_MMD_MASK_OFST 48
/* Native clause 22 */
#define MC_CMD_MMD_CLAUSE22  0
#define MC_CMD_MMD_CLAUSE45_PMAPMD 1
#define MC_CMD_MMD_CLAUSE45_WIS 2
#define MC_CMD_MMD_CLAUSE45_PCS 3
#define MC_CMD_MMD_CLAUSE45_PHYXS 4
#define MC_CMD_MMD_CLAUSE45_DTEXS 5
#define MC_CMD_MMD_CLAUSE45_TC 6
#define MC_CMD_MMD_CLAUSE45_AN 7
/* Clause22 proxied over clause45 by PHY */
#define MC_CMD_MMD_CLAUSE45_C22EXT 29
#define MC_CMD_MMD_CLAUSE45_VEND1 30
#define MC_CMD_MMD_CLAUSE45_VEND2 31
/* PHY stepping version */
#define MC_CMD_GET_PHY_CFG_OUT_REVISION_OFST 52
#define MC_CMD_GET_PHY_CFG_OUT_REVISION_LEN 20

/* MC_CMD_START_BIST:
 * Start a BIST test on the PHY.
 *
 * Locks required: PHY_LOCK if doing a  PHY BIST
 * Return code: 0, EINVAL, EACCES (if PHY_LOCK is not held)
 */
#define MC_CMD_START_BIST 0x25
#define MC_CMD_START_BIST_IN_LEN 4
#define MC_CMD_START_BIST_IN_TYPE_OFST 0
#define MC_CMD_START_BIST_OUT_LEN 0

/* Run the PHY's short cable BIST */
#define MC_CMD_PHY_BIST_CABLE_SHORT  1
/* Run the PHY's long cable BIST */
#define MC_CMD_PHY_BIST_CABLE_LONG   2
/* Run BIST on the currently selected BPX Serdes (XAUI or XFI) */
#define MC_CMD_BPX_SERDES_BIST 3
/* Run the MC loopback tests */
#define MC_CMD_MC_LOOPBACK_BIST 4
/* Run the PHY's standard BIST */
#define MC_CMD_PHY_BIST 5

/* MC_CMD_POLL_PHY_BIST: (variadic output)
 * Poll for BIST completion
 *
 * Returns a single status code, and optionally some PHY specific
 * bist output. The driver should only consume the BIST output
 * after validating OUTLEN and PHY_CFG.PHY_TYPE.
 *
 * If a driver can't successfully parse the BIST output, it should
 * still respect the pass/Fail in OUT.RESULT
 *
 * Locks required: PHY_LOCK if doing a  PHY BIST
 * Return code: 0, EACCES (if PHY_LOCK is not held)
 */
#define MC_CMD_POLL_BIST 0x26
#define MC_CMD_POLL_BIST_IN_LEN 0
#define MC_CMD_POLL_BIST_OUT_LEN UNKNOWN
#define MC_CMD_POLL_BIST_OUT_SFT9001_LEN 36
#define MC_CMD_POLL_BIST_OUT_MRSFP_LEN 8
#define MC_CMD_POLL_BIST_OUT_RESULT_OFST 0
#define MC_CMD_POLL_BIST_RUNNING 1
#define MC_CMD_POLL_BIST_PASSED 2
#define MC_CMD_POLL_BIST_FAILED 3
#define MC_CMD_POLL_BIST_TIMEOUT 4
/* Generic: */
#define MC_CMD_POLL_BIST_OUT_PRIVATE_OFST 4
/* SFT9001-specific: */
#define MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_A_OFST 4
#define MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_B_OFST 8
#define MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_C_OFST 12
#define MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_D_OFST 16
#define MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_A_OFST 20
#define MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_B_OFST 24
#define MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_C_OFST 28
#define MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_D_OFST 32
#define MC_CMD_POLL_BIST_SFT9001_PAIR_OK 1
#define MC_CMD_POLL_BIST_SFT9001_PAIR_OPEN 2
#define MC_CMD_POLL_BIST_SFT9001_INTRA_PAIR_SHORT 3
#define MC_CMD_POLL_BIST_SFT9001_INTER_PAIR_SHORT 4
#define MC_CMD_POLL_BIST_SFT9001_PAIR_BUSY 9
/* mrsfp "PHY" driver: */
#define MC_CMD_POLL_BIST_OUT_MRSFP_TEST_OFST 4
#define MC_CMD_POLL_BIST_MRSFP_TEST_COMPLETE 0
#define MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_WRITE 1
#define MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_NO_ACCESS_IO_EXP 2
#define MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_NO_ACCESS_MODULE 3
#define MC_CMD_POLL_BIST_MRSFP_TEST_IO_EXP_I2C_CONFIGURE 4
#define MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_I2C_NO_CROSSTALK 5
#define MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_PRESENCE 6
#define MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_ID_I2C_ACCESS 7
#define MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_ID_SANE_VALUE 8

/* MC_CMD_PHY_SPI: (variadic in, variadic out)
 * Read/Write/Erase the PHY SPI device
 *
 * Locks required: PHY_LOCK
 * Return code: 0, ETIME, EINVAL, EACCES (if PHY_LOCK is not held)
 */
#define MC_CMD_PHY_SPI 0x27
#define MC_CMD_PHY_SPI_IN_LEN(_write_bytes) (12 + (_write_bytes))
#define MC_CMD_PHY_SPI_IN_ARGS_OFST 0
#define MC_CMD_PHY_SPI_IN_ARGS_ADDR_OFST 0
#define MC_CMD_PHY_SPI_IN_ARGS_READ_BYTES_OFST 4
#define MC_CMD_PHY_SPI_IN_ARGS_ERASE_ALL_OFST 8
/* Data to write here */
#define MC_CMD_PHY_SPI_IN_WRITE_BUFFER_OFSET 12
#define MC_CMD_PHY_SPI_OUT_LEN(_read_bytes) (_read_bytes)
/* Data read here */
#define MC_CMD_PHY_SPI_OUT_READ_BUFFER_OFST 0


/* MC_CMD_GET_LOOPBACK_MODES:
 * Returns a bitmask of loopback modes evailable at each speed.
 *
 * Locks required: None
 * Return code: 0
 */
#define MC_CMD_GET_LOOPBACK_MODES 0x28
#define MC_CMD_GET_LOOPBACK_MODES_IN_LEN 0
#define MC_CMD_GET_LOOPBACK_MODES_OUT_LEN 32
#define MC_CMD_GET_LOOPBACK_MODES_100M_OFST 0
#define MC_CMD_GET_LOOPBACK_MODES_1G_OFST 8
#define MC_CMD_GET_LOOPBACK_MODES_10G_OFST 16
#define MC_CMD_GET_LOOPBACK_MODES_SUGGESTED_OFST 24

/* Flow control enumeration */
#define MC_CMD_FCNTL_OFF 0
#define MC_CMD_FCNTL_RESPOND 1
#define MC_CMD_FCNTL_BIDIR 2
/* Auto - Use what the link has autonegotiated
 *      - The driver should modify the advertised capabilities via SET_LINK.CAP
 *        to control the negotiated flow control mode.
 *      - Can only be set if the PHY supports PAUSE+ASYM capabilities
 *      - Never returned by GET_LINK as the value programmed into the MAC
 */
#define MC_CMD_FCNTL_AUTO 3

/* Generic mac fault bitmask */
#define MC_CMD_MAC_FAULT_XGMII_LOCAL_LBN 0
#define MC_CMD_MAC_FAULT_XGMII_LOCAL_WIDTH 1
#define MC_CMD_MAC_FAULT_XGMII_REMOTE_LBN 1
#define MC_CMD_MAC_FAULT_XGMII_REMOTE_WIDTH 1
#define MC_CMD_MAC_FAULT_SGMII_REMOTE_LBN 2
#define MC_CMD_MAC_FAULT_SGMII_REMOTE_WIDTH 1

/* MC_CMD_GET_LINK:
 * Read the unified MAC/PHY link state
 *
 * Locks required: None
 * Return code: 0, ETIME
 */
#define MC_CMD_GET_LINK 0x29
#define MC_CMD_GET_LINK_IN_LEN 0
#define MC_CMD_GET_LINK_OUT_LEN 28
/* near-side and link-partner advertised capabilities */
#define MC_CMD_GET_LINK_OUT_CAP_OFST 0
#define MC_CMD_GET_LINK_OUT_LP_CAP_OFST 4
/* Autonegotiated speed in mbit/s. The link may still be down
 * even if this reads non-zero */
#define MC_CMD_GET_LINK_OUT_LINK_SPEED_OFST 8
#define MC_CMD_GET_LINK_OUT_LOOPBACK_MODE_OFST 12
#define MC_CMD_GET_LINK_OUT_FLAGS_OFST 16
/* Whether we have overall link up */
#define MC_CMD_GET_LINK_LINK_UP_LBN 0
#define MC_CMD_GET_LINK_LINK_UP_WIDTH 1
#define MC_CMD_GET_LINK_FULL_DUPLEX_LBN 1
#define MC_CMD_GET_LINK_FULL_DUPLEX_WIDTH 1
/* Whether we have link at the layers provided by the BPX */
#define MC_CMD_GET_LINK_BPX_LINK_LBN 2
#define MC_CMD_GET_LINK_BPX_LINK_WIDTH 1
/* Whether the PHY has external link */
#define MC_CMD_GET_LINK_PHY_LINK_LBN 3
#define MC_CMD_GET_LINK_PHY_LINK_WIDTH 1
#define MC_CMD_GET_LINK_OUT_FCNTL_OFST 20
#define MC_CMD_GET_LINK_OUT_MAC_FAULT_OFST 24

/* MC_CMD_SET_LINK:
 * Write the unified MAC/PHY link configuration
 *
 * A loopback speed of "0" is supported, and means
 * (choose any available speed)
 *
 * Locks required: None
 * Return code: 0, EINVAL, ETIME
 */
#define MC_CMD_SET_LINK 0x2a
#define MC_CMD_SET_LINK_IN_LEN 16
#define MC_CMD_SET_LINK_IN_CAP_OFST 0
#define MC_CMD_SET_LINK_IN_FLAGS_OFST 4
#define MC_CMD_SET_LINK_LOWPOWER_LBN 0
#define MC_CMD_SET_LINK_LOWPOWER_WIDTH 1
#define MC_CMD_SET_LINK_POWEROFF_LBN 1
#define MC_CMD_SET_LINK_POWEROFF_WIDTH 1
#define MC_CMD_SET_LINK_TXDIS_LBN 2
#define MC_CMD_SET_LINK_TXDIS_WIDTH 1
#define MC_CMD_SET_LINK_IN_LOOPBACK_MODE_OFST 8
#define MC_CMD_SET_LINK_IN_LOOPBACK_SPEED_OFST 12
#define MC_CMD_SET_LINK_OUT_LEN 0

/* MC_CMD_SET_ID_LED:
 * Set indentification LED state
 *
 * Locks required: None
 * Return code: 0, EINVAL
 */
#define MC_CMD_SET_ID_LED 0x2b
#define MC_CMD_SET_ID_LED_IN_LEN 4
#define MC_CMD_SET_ID_LED_IN_STATE_OFST 0
#define  MC_CMD_LED_OFF 0
#define  MC_CMD_LED_ON 1
#define  MC_CMD_LED_DEFAULT 2
#define MC_CMD_SET_ID_LED_OUT_LEN 0

/* MC_CMD_SET_MAC:
 * Set MAC configuration
 *
 * The MTU is the MTU programmed directly into the XMAC/GMAC
 * (inclusive of EtherII, VLAN, bug16011 padding)
 *
 * Locks required: None
 * Return code: 0, EINVAL
 */
#define MC_CMD_SET_MAC 0x2c
#define MC_CMD_SET_MAC_IN_LEN 24
#define MC_CMD_SET_MAC_IN_MTU_OFST 0
#define MC_CMD_SET_MAC_IN_DRAIN_OFST 4
#define MC_CMD_SET_MAC_IN_ADDR_OFST 8
#define MC_CMD_SET_MAC_IN_REJECT_OFST 16
#define MC_CMD_SET_MAC_IN_REJECT_UNCST_LBN 0
#define MC_CMD_SET_MAC_IN_REJECT_UNCST_WIDTH 1
#define MC_CMD_SET_MAC_IN_REJECT_BRDCST_LBN 1
#define MC_CMD_SET_MAC_IN_REJECT_BRDCST_WIDTH 1
#define MC_CMD_SET_MAC_IN_FCNTL_OFST 20
#define MC_CMD_SET_MAC_OUT_LEN 0

/* MC_CMD_PHY_STATS:
 * Get generic PHY statistics
 *
 * This call returns the statistics for a generic PHY in a sparse
 * array (indexed by the enumerate). Each value is represented by
 * a 32bit number.
 *
 * If the DMA_ADDR is 0, then no DMA is performed, and the statistics
 * may be read directly out of shared memory. If DMA_ADDR != 0, then
 * the statistics are dmad to that (page-aligned location)
 *
 * Locks required: None
 * Returns: 0, ETIME
 * Response methods: shared memory, event
 */
#define MC_CMD_PHY_STATS 0x2d
#define MC_CMD_PHY_STATS_IN_LEN 8
#define MC_CMD_PHY_STATS_IN_DMA_ADDR_LO_OFST 0
#define MC_CMD_PHY_STATS_IN_DMA_ADDR_HI_OFST 4
#define MC_CMD_PHY_STATS_OUT_DMA_LEN 0
#define MC_CMD_PHY_STATS_OUT_NO_DMA_LEN (MC_CMD_PHY_NSTATS * 4)

/* Unified MAC statistics enumeration */
#define MC_CMD_MAC_GENERATION_START 0
#define MC_CMD_MAC_TX_PKTS 1
#define MC_CMD_MAC_TX_PAUSE_PKTS 2
#define MC_CMD_MAC_TX_CONTROL_PKTS 3
#define MC_CMD_MAC_TX_UNICAST_PKTS 4
#define MC_CMD_MAC_TX_MULTICAST_PKTS 5
#define MC_CMD_MAC_TX_BROADCAST_PKTS 6
#define MC_CMD_MAC_TX_BYTES 7
#define MC_CMD_MAC_TX_BAD_BYTES 8
#define MC_CMD_MAC_TX_LT64_PKTS 9
#define MC_CMD_MAC_TX_64_PKTS 10
#define MC_CMD_MAC_TX_65_TO_127_PKTS 11
#define MC_CMD_MAC_TX_128_TO_255_PKTS 12
#define MC_CMD_MAC_TX_256_TO_511_PKTS 13
#define MC_CMD_MAC_TX_512_TO_1023_PKTS 14
#define MC_CMD_MAC_TX_1024_TO_15XX_PKTS 15
#define MC_CMD_MAC_TX_15XX_TO_JUMBO_PKTS 16
#define MC_CMD_MAC_TX_GTJUMBO_PKTS 17
#define MC_CMD_MAC_TX_BAD_FCS_PKTS 18
#define MC_CMD_MAC_TX_SINGLE_COLLISION_PKTS 19
#define MC_CMD_MAC_TX_MULTIPLE_COLLISION_PKTS 20
#define MC_CMD_MAC_TX_EXCESSIVE_COLLISION_PKTS 21
#define MC_CMD_MAC_TX_LATE_COLLISION_PKTS 22
#define MC_CMD_MAC_TX_DEFERRED_PKTS 23
#define MC_CMD_MAC_TX_EXCESSIVE_DEFERRED_PKTS 24
#define MC_CMD_MAC_TX_NON_TCPUDP_PKTS 25
#define MC_CMD_MAC_TX_MAC_SRC_ERR_PKTS 26
#define MC_CMD_MAC_TX_IP_SRC_ERR_PKTS 27
#define MC_CMD_MAC_RX_PKTS 28
#define MC_CMD_MAC_RX_PAUSE_PKTS 29
#define MC_CMD_MAC_RX_GOOD_PKTS 30
#define MC_CMD_MAC_RX_CONTROL_PKTS 31
#define MC_CMD_MAC_RX_UNICAST_PKTS 32
#define MC_CMD_MAC_RX_MULTICAST_PKTS 33
#define MC_CMD_MAC_RX_BROADCAST_PKTS 34
#define MC_CMD_MAC_RX_BYTES 35
#define MC_CMD_MAC_RX_BAD_BYTES 36
#define MC_CMD_MAC_RX_64_PKTS 37
#define MC_CMD_MAC_RX_65_TO_127_PKTS 38
#define MC_CMD_MAC_RX_128_TO_255_PKTS 39
#define MC_CMD_MAC_RX_256_TO_511_PKTS 40
#define MC_CMD_MAC_RX_512_TO_1023_PKTS 41
#define MC_CMD_MAC_RX_1024_TO_15XX_PKTS 42
#define MC_CMD_MAC_RX_15XX_TO_JUMBO_PKTS 43
#define MC_CMD_MAC_RX_GTJUMBO_PKTS 44
#define MC_CMD_MAC_RX_UNDERSIZE_PKTS 45
#define MC_CMD_MAC_RX_BAD_FCS_PKTS 46
#define MC_CMD_MAC_RX_OVERFLOW_PKTS 47
#define MC_CMD_MAC_RX_FALSE_CARRIER_PKTS 48
#define MC_CMD_MAC_RX_SYMBOL_ERROR_PKTS 49
#define MC_CMD_MAC_RX_ALIGN_ERROR_PKTS 50
#define MC_CMD_MAC_RX_LENGTH_ERROR_PKTS 51
#define MC_CMD_MAC_RX_INTERNAL_ERROR_PKTS 52
#define MC_CMD_MAC_RX_JABBER_PKTS 53
#define MC_CMD_MAC_RX_NODESC_DROPS 54
#define MC_CMD_MAC_RX_LANES01_CHAR_ERR 55
#define MC_CMD_MAC_RX_LANES23_CHAR_ERR 56
#define MC_CMD_MAC_RX_LANES01_DISP_ERR 57
#define MC_CMD_MAC_RX_LANES23_DISP_ERR 58
#define MC_CMD_MAC_RX_MATCH_FAULT 59
#define MC_CMD_GMAC_DMABUF_START 64
#define MC_CMD_GMAC_DMABUF_END   95
/* Insert new members here. */
#define MC_CMD_MAC_GENERATION_END 96
#define MC_CMD_MAC_NSTATS (MC_CMD_MAC_GENERATION_END+1)

/* MC_CMD_MAC_STATS:
 * Get unified GMAC/XMAC statistics
 *
 * This call returns unified statistics maintained by the MC as it
 * switches between the GMAC and XMAC. The MC will write out all
 * supported stats.  The driver should zero initialise the buffer to
 * guarantee consistent results.
 *
 * Locks required: None
 * Returns: 0
 * Response methods: shared memory, event
 */
#define MC_CMD_MAC_STATS 0x2e
#define MC_CMD_MAC_STATS_IN_LEN 16
#define MC_CMD_MAC_STATS_IN_DMA_ADDR_LO_OFST 0
#define MC_CMD_MAC_STATS_IN_DMA_ADDR_HI_OFST 4
#define MC_CMD_MAC_STATS_IN_CMD_OFST 8
#define MC_CMD_MAC_STATS_CMD_DMA_LBN 0
#define MC_CMD_MAC_STATS_CMD_DMA_WIDTH 1
#define MC_CMD_MAC_STATS_CMD_CLEAR_LBN 1
#define MC_CMD_MAC_STATS_CMD_CLEAR_WIDTH 1
#define MC_CMD_MAC_STATS_CMD_PERIODIC_CHANGE_LBN 2
#define MC_CMD_MAC_STATS_CMD_PERIODIC_CHANGE_WIDTH 1
/* Remaining PERIOD* fields only relevant when PERIODIC_CHANGE is set */
#define MC_CMD_MAC_STATS_CMD_PERIODIC_ENABLE_LBN 3
#define MC_CMD_MAC_STATS_CMD_PERIODIC_ENABLE_WIDTH 1
#define MC_CMD_MAC_STATS_CMD_PERIODIC_CLEAR_LBN 4
#define MC_CMD_MAC_STATS_CMD_PERIODIC_CLEAR_WIDTH 1
#define MC_CMD_MAC_STATS_CMD_PERIODIC_NOEVENT_LBN 5
#define MC_CMD_MAC_STATS_CMD_PERIODIC_NOEVENT_WIDTH 1
#define MC_CMD_MAC_STATS_CMD_PERIOD_MS_LBN 16
#define MC_CMD_MAC_STATS_CMD_PERIOD_MS_WIDTH 16
#define MC_CMD_MAC_STATS_IN_DMA_LEN_OFST 12

#define MC_CMD_MAC_STATS_OUT_LEN 0

/* Callisto flags */
#define MC_CMD_SFT9001_ROBUST_LBN 0
#define MC_CMD_SFT9001_ROBUST_WIDTH 1
#define MC_CMD_SFT9001_SHORT_REACH_LBN 1
#define MC_CMD_SFT9001_SHORT_REACH_WIDTH 1

/* MC_CMD_SFT9001_GET:
 * Read current callisto specific setting
 *
 * Locks required: None
 * Returns: 0, ETIME
 */
#define MC_CMD_SFT9001_GET 0x30
#define MC_CMD_SFT9001_GET_IN_LEN 0
#define MC_CMD_SFT9001_GET_OUT_LEN 4
#define MC_CMD_SFT9001_GET_OUT_FLAGS_OFST 0

/* MC_CMD_SFT9001_SET:
 * Write current callisto specific setting
 *
 * Locks required: None
 * Returns: 0, ETIME, EINVAL
 */
#define MC_CMD_SFT9001_SET 0x31
#define MC_CMD_SFT9001_SET_IN_LEN 4
#define MC_CMD_SFT9001_SET_IN_FLAGS_OFST 0
#define MC_CMD_SFT9001_SET_OUT_LEN 0


/* MC_CMD_WOL_FILTER_SET:
 * Set a WoL filter
 *
 * Locks required: None
 * Returns: 0, EBUSY, EINVAL, ENOSYS
 */
#define MC_CMD_WOL_FILTER_SET 0x32
#define MC_CMD_WOL_FILTER_SET_IN_LEN 192 /* 190 rounded up to a word */
#define MC_CMD_WOL_FILTER_SET_IN_FILTER_MODE_OFST 0
#define MC_CMD_WOL_FILTER_SET_IN_WOL_TYPE_OFST 4

/* There is a union at offset 8, following defines overlap due to
 * this */
#define MC_CMD_WOL_FILTER_SET_IN_DATA_OFST 8

#define MC_CMD_WOL_FILTER_SET_IN_MAGIC_MAC_OFST		\
	MC_CMD_WOL_FILTER_SET_IN_DATA_OFST

#define MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_SRC_IP_OFST   \
	MC_CMD_WOL_FILTER_SET_IN_DATA_OFST
#define MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_DST_IP_OFST   \
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 4)
#define MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_SRC_PORT_OFST \
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 8)
#define MC_CMD_WOL_FILTER_SET_IN_IPV4_SYN_DST_PORT_OFST \
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 10)

#define MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_SRC_IP_OFST   \
	MC_CMD_WOL_FILTER_SET_IN_DATA_OFST
#define MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_DST_IP_OFST   \
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 16)
#define MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_SRC_PORT_OFST \
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 32)
#define MC_CMD_WOL_FILTER_SET_IN_IPV6_SYN_DST_PORT_OFST \
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 34)

#define MC_CMD_WOL_FILTER_SET_IN_BITMAP_MASK_OFST	\
	MC_CMD_WOL_FILTER_SET_IN_DATA_OFST
#define MC_CMD_WOL_FILTER_SET_IN_BITMAP_OFST		\
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 48)
#define MC_CMD_WOL_FILTER_SET_IN_BITMAP_LEN_OFST	\
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 176)
#define MC_CMD_WOL_FILTER_SET_IN_BITMAP_LAYER3_OFST	\
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 177)
#define MC_CMD_WOL_FILTER_SET_IN_BITMAP_LAYER4_OFST	\
	(MC_CMD_WOL_FILTER_SET_IN_DATA_OFST + 178)

#define MC_CMD_WOL_FILTER_SET_IN_LINK_MASK_OFST	\
	MC_CMD_WOL_FILTER_SET_IN_DATA_OFST
#define MC_CMD_WOL_FILTER_SET_IN_LINK_UP_LBN	0
#define MC_CMD_WOL_FILTER_SET_IN_LINK_UP_WIDTH	1
#define MC_CMD_WOL_FILTER_SET_IN_LINK_DOWN_LBN	1
#define MC_CMD_WOL_FILTER_SET_IN_LINK_DOWN_WIDTH 1

#define MC_CMD_WOL_FILTER_SET_OUT_LEN 4
#define MC_CMD_WOL_FILTER_SET_OUT_FILTER_ID_OFST 0

/* WOL Filter types enumeration */
#define MC_CMD_WOL_TYPE_MAGIC      0x0
			 /* unused 0x1 */
#define MC_CMD_WOL_TYPE_WIN_MAGIC  0x2
#define MC_CMD_WOL_TYPE_IPV4_SYN   0x3
#define MC_CMD_WOL_TYPE_IPV6_SYN   0x4
#define MC_CMD_WOL_TYPE_BITMAP     0x5
#define MC_CMD_WOL_TYPE_LINK       0x6
#define MC_CMD_WOL_TYPE_MAX        0x7

#define MC_CMD_FILTER_MODE_SIMPLE     0x0
#define MC_CMD_FILTER_MODE_STRUCTURED 0xffffffff

/* MC_CMD_WOL_FILTER_REMOVE:
 * Remove a WoL filter
 *
 * Locks required: None
 * Returns: 0, EINVAL, ENOSYS
 */
#define MC_CMD_WOL_FILTER_REMOVE 0x33
#define MC_CMD_WOL_FILTER_REMOVE_IN_LEN 4
#define MC_CMD_WOL_FILTER_REMOVE_IN_FILTER_ID_OFST 0
#define MC_CMD_WOL_FILTER_REMOVE_OUT_LEN 0


/* MC_CMD_WOL_FILTER_RESET:
 * Reset (i.e. remove all) WoL filters
 *
 * Locks required: None
 * Returns: 0, ENOSYS
 */
#define MC_CMD_WOL_FILTER_RESET 0x34
#define MC_CMD_WOL_FILTER_RESET_IN_LEN 0
#define MC_CMD_WOL_FILTER_RESET_OUT_LEN 0

/* MC_CMD_SET_MCAST_HASH:
 * Set the MCASH hash value without otherwise
 * reconfiguring the MAC
 */
#define MC_CMD_SET_MCAST_HASH 0x35
#define MC_CMD_SET_MCAST_HASH_IN_LEN 32
#define MC_CMD_SET_MCAST_HASH_IN_HASH0_OFST 0
#define MC_CMD_SET_MCAST_HASH_IN_HASH1_OFST 16
#define MC_CMD_SET_MCAST_HASH_OUT_LEN 0

/* MC_CMD_NVRAM_TYPES:
 * Return bitfield indicating available types of virtual NVRAM partitions
 *
 * Locks required: none
 * Returns: 0
 */
#define MC_CMD_NVRAM_TYPES 0x36
#define MC_CMD_NVRAM_TYPES_IN_LEN 0
#define MC_CMD_NVRAM_TYPES_OUT_LEN 4
#define MC_CMD_NVRAM_TYPES_OUT_TYPES_OFST 0

/* Supported NVRAM types */
#define MC_CMD_NVRAM_TYPE_DISABLED_CALLISTO 0
#define MC_CMD_NVRAM_TYPE_MC_FW 1
#define MC_CMD_NVRAM_TYPE_MC_FW_BACKUP 2
#define MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT0 3
#define MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT1 4
#define MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT0 5
#define MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT1 6
#define MC_CMD_NVRAM_TYPE_EXP_ROM 7
#define MC_CMD_NVRAM_TYPE_EXP_ROM_CFG_PORT0 8
#define MC_CMD_NVRAM_TYPE_EXP_ROM_CFG_PORT1 9
#define MC_CMD_NVRAM_TYPE_PHY_PORT0 10
#define MC_CMD_NVRAM_TYPE_PHY_PORT1 11
#define MC_CMD_NVRAM_TYPE_LOG 12

/* MC_CMD_NVRAM_INFO:
 * Read info about a virtual NVRAM partition
 *
 * Locks required: none
 * Returns: 0, EINVAL (bad type)
 */
#define MC_CMD_NVRAM_INFO 0x37
#define MC_CMD_NVRAM_INFO_IN_LEN 4
#define MC_CMD_NVRAM_INFO_IN_TYPE_OFST 0
#define MC_CMD_NVRAM_INFO_OUT_LEN 24
#define MC_CMD_NVRAM_INFO_OUT_TYPE_OFST 0
#define MC_CMD_NVRAM_INFO_OUT_SIZE_OFST 4
#define MC_CMD_NVRAM_INFO_OUT_ERASESIZE_OFST 8
#define MC_CMD_NVRAM_INFO_OUT_FLAGS_OFST 12
#define   MC_CMD_NVRAM_PROTECTED_LBN 0
#define   MC_CMD_NVRAM_PROTECTED_WIDTH 1
#define MC_CMD_NVRAM_INFO_OUT_PHYSDEV_OFST 16
#define MC_CMD_NVRAM_INFO_OUT_PHYSADDR_OFST 20

/* MC_CMD_NVRAM_UPDATE_START:
 * Start a group of update operations on a virtual NVRAM partition
 *
 * Locks required: PHY_LOCK if type==*PHY*
 * Returns: 0, EINVAL (bad type), EACCES (if PHY_LOCK required and not held)
 */
#define MC_CMD_NVRAM_UPDATE_START 0x38
#define MC_CMD_NVRAM_UPDATE_START_IN_LEN 4
#define MC_CMD_NVRAM_UPDATE_START_IN_TYPE_OFST 0
#define MC_CMD_NVRAM_UPDATE_START_OUT_LEN 0

/* MC_CMD_NVRAM_READ:
 * Read data from a virtual NVRAM partition
 *
 * Locks required: PHY_LOCK if type==*PHY*
 * Returns: 0, EINVAL (bad type/offset/length), EACCES (if PHY_LOCK required and not held)
 */
#define MC_CMD_NVRAM_READ 0x39
#define MC_CMD_NVRAM_READ_IN_LEN 12
#define MC_CMD_NVRAM_READ_IN_TYPE_OFST 0
#define MC_CMD_NVRAM_READ_IN_OFFSET_OFST 4
#define MC_CMD_NVRAM_READ_IN_LENGTH_OFST 8
#define MC_CMD_NVRAM_READ_OUT_LEN(_read_bytes) (_read_bytes)
#define MC_CMD_NVRAM_READ_OUT_READ_BUFFER_OFST 0

/* MC_CMD_NVRAM_WRITE:
 * Write data to a virtual NVRAM partition
 *
 * Locks required: PHY_LOCK if type==*PHY*
 * Returns: 0, EINVAL (bad type/offset/length), EACCES (if PHY_LOCK required and not held)
 */
#define MC_CMD_NVRAM_WRITE 0x3a
#define MC_CMD_NVRAM_WRITE_IN_TYPE_OFST 0
#define MC_CMD_NVRAM_WRITE_IN_OFFSET_OFST 4
#define MC_CMD_NVRAM_WRITE_IN_LENGTH_OFST 8
#define MC_CMD_NVRAM_WRITE_IN_WRITE_BUFFER_OFST 12
#define MC_CMD_NVRAM_WRITE_IN_LEN(_write_bytes) (12 + _write_bytes)
#define MC_CMD_NVRAM_WRITE_OUT_LEN 0

/* MC_CMD_NVRAM_ERASE:
 * Erase sector(s) from a virtual NVRAM partition
 *
 * Locks required: PHY_LOCK if type==*PHY*
 * Returns: 0, EINVAL (bad type/offset/length), EACCES (if PHY_LOCK required and not held)
 */
#define MC_CMD_NVRAM_ERASE 0x3b
#define MC_CMD_NVRAM_ERASE_IN_LEN 12
#define MC_CMD_NVRAM_ERASE_IN_TYPE_OFST 0
#define MC_CMD_NVRAM_ERASE_IN_OFFSET_OFST 4
#define MC_CMD_NVRAM_ERASE_IN_LENGTH_OFST 8
#define MC_CMD_NVRAM_ERASE_OUT_LEN 0

/* MC_CMD_NVRAM_UPDATE_FINISH:
 * Finish a group of update operations on a virtual NVRAM partition
 *
 * Locks required: PHY_LOCK if type==*PHY*
 * Returns: 0, EINVAL (bad type/offset/length), EACCES (if PHY_LOCK required and not held)
 */
#define MC_CMD_NVRAM_UPDATE_FINISH 0x3c
#define MC_CMD_NVRAM_UPDATE_FINISH_IN_LEN 8
#define MC_CMD_NVRAM_UPDATE_FINISH_IN_TYPE_OFST 0
#define MC_CMD_NVRAM_UPDATE_FINISH_IN_REBOOT_OFST 4
#define MC_CMD_NVRAM_UPDATE_FINISH_OUT_LEN 0

/* MC_CMD_REBOOT:
 * Reboot the MC.
 *
 * The AFTER_ASSERTION flag is intended to be used when the driver notices
 * an assertion failure (at which point it is expected to perform a complete
 * tear down and reinitialise), to allow both ports to reset the MC once
 * in an atomic fashion.
 *
 * Production mc firmwares are generally compiled with REBOOT_ON_ASSERT=1,
 * which means that they will automatically reboot out of the assertion
 * handler, so this is in practise an optional operation. It is still
 * recommended that drivers execute this to support custom firmwares
 * with REBOOT_ON_ASSERT=0.
 *
 * Locks required: NONE
 * Returns: Nothing. You get back a response with ERR=1, DATALEN=0
 */
#define MC_CMD_REBOOT 0x3d
#define MC_CMD_REBOOT_IN_LEN 4
#define MC_CMD_REBOOT_IN_FLAGS_OFST 0
#define MC_CMD_REBOOT_FLAGS_AFTER_ASSERTION 1
#define MC_CMD_REBOOT_OUT_LEN 0

/* MC_CMD_SCHEDINFO:
 * Request scheduler info. from the MC.
 *
 * Locks required: NONE
 * Returns: An array of (timeslice,maximum overrun), one for each thread,
 * in ascending order of thread address.s
 */
#define MC_CMD_SCHEDINFO 0x3e
#define MC_CMD_SCHEDINFO_IN_LEN 0


/* MC_CMD_SET_REBOOT_MODE: (debug)
 * Set the mode for the next MC reboot.
 *
 * Locks required: NONE
 *
 * Sets the reboot mode to the specified value.  Returns the old mode.
 */
#define MC_CMD_REBOOT_MODE 0x3f
#define MC_CMD_REBOOT_MODE_IN_LEN 4
#define MC_CMD_REBOOT_MODE_IN_VALUE_OFST 0
#define MC_CMD_REBOOT_MODE_OUT_LEN 4
#define MC_CMD_REBOOT_MODE_OUT_VALUE_OFST 0
#define   MC_CMD_REBOOT_MODE_NORMAL 0
#define   MC_CMD_REBOOT_MODE_SNAPPER 3

/* MC_CMD_DEBUG_LOG:
 * Null request/response command (debug)
 * - sequence number is always zero
 * - only supported on the UART interface
 * (the same set of bytes is delivered as an
 * event over PCI)
 */
#define MC_CMD_DEBUG_LOG 0x40
#define MC_CMD_DEBUG_LOG_IN_LEN 0
#define MC_CMD_DEBUG_LOG_OUT_LEN 0

/* Generic sensor enumeration. Note that a dual port NIC
 * will EITHER expose PHY_COMMON_TEMP OR PHY0_TEMP and
 * PHY1_TEMP depending on whether there is a single sensor
 * in the vicinity of the two port, or one per port.
 */
#define MC_CMD_SENSOR_CONTROLLER_TEMP 0		/* degC */
#define MC_CMD_SENSOR_PHY_COMMON_TEMP 1		/* degC */
#define MC_CMD_SENSOR_CONTROLLER_COOLING 2	/* bool */
#define MC_CMD_SENSOR_PHY0_TEMP 3		/* degC */
#define MC_CMD_SENSOR_PHY0_COOLING 4		/* bool */
#define MC_CMD_SENSOR_PHY1_TEMP 5		/* degC */
#define MC_CMD_SENSOR_PHY1_COOLING 6		/* bool */
#define MC_CMD_SENSOR_IN_1V0 7			/* mV */
#define MC_CMD_SENSOR_IN_1V2 8			/* mV */
#define MC_CMD_SENSOR_IN_1V8 9			/* mV */
#define MC_CMD_SENSOR_IN_2V5 10			/* mV */
#define MC_CMD_SENSOR_IN_3V3 11			/* mV */
#define MC_CMD_SENSOR_IN_12V0 12		/* mV */


/* Sensor state */
#define MC_CMD_SENSOR_STATE_OK 0
#define MC_CMD_SENSOR_STATE_WARNING 1
#define MC_CMD_SENSOR_STATE_FATAL 2
#define MC_CMD_SENSOR_STATE_BROKEN 3

/* MC_CMD_SENSOR_INFO:
 * Returns information about every available sensor.
 *
 * Each sensor has a single (16bit) value, and a corresponding state.
 * The mapping between value and sensor is nominally determined by the
 * MC, but in practise is implemented as zero (BROKEN), one (TEMPERATURE),
 * or two (VOLTAGE) ranges per sensor per state.
 *
 * This call returns a mask (32bit) of the sensors that are supported
 * by this platform, then an array (indexed by MC_CMD_SENSOR) of byte
 * offsets to the per-sensor arrays. Each sensor array has four 16bit
 * numbers, min1, max1, min2, max2.
 *
 * Locks required: None
 * Returns: 0
 */
#define MC_CMD_SENSOR_INFO 0x41
#define MC_CMD_SENSOR_INFO_IN_LEN 0
#define MC_CMD_SENSOR_INFO_OUT_MASK_OFST 0
#define MC_CMD_SENSOR_INFO_OUT_OFFSET_OFST(_x) \
	(4 + (_x))
#define MC_CMD_SENSOR_INFO_OUT_MIN1_OFST(_ofst) \
	((_ofst) + 0)
#define MC_CMD_SENSOR_INFO_OUT_MAX1_OFST(_ofst) \
	((_ofst) + 2)
#define MC_CMD_SENSOR_INFO_OUT_MIN2_OFST(_ofst) \
	((_ofst) + 4)
#define MC_CMD_SENSOR_INFO_OUT_MAX2_OFST(_ofst) \
	((_ofst) + 6)

/* MC_CMD_READ_SENSORS
 * Returns the current reading from each sensor
 *
 * Returns a sparse array of sensor readings (indexed by the sensor
 * type) into host memory.  Each array element is a dword.
 *
 * The MC will send a SENSOREVT event every time any sensor changes state. The
 * driver is responsible for ensuring that it doesn't miss any events. The board
 * will function normally if all sensors are in STATE_OK or state_WARNING.
 * Otherwise the board should not be expected to function.
 */
#define MC_CMD_READ_SENSORS 0x42
#define MC_CMD_READ_SENSORS_IN_LEN 8
#define MC_CMD_READ_SENSORS_IN_DMA_ADDR_LO_OFST 0
#define MC_CMD_READ_SENSORS_IN_DMA_ADDR_HI_OFST 4
#define MC_CMD_READ_SENSORS_OUT_LEN 0

/* Sensor reading fields */
#define MC_CMD_READ_SENSOR_VALUE_LBN 0
#define MC_CMD_READ_SENSOR_VALUE_WIDTH 16
#define MC_CMD_READ_SENSOR_STATE_LBN 16
#define MC_CMD_READ_SENSOR_STATE_WIDTH 8


/* MC_CMD_GET_PHY_STATE:
 * Report current state of PHY.  A "zombie" PHY is a PHY that has failed to
 * boot (e.g. due to missing or corrupted firmware).
 *
 * Locks required: None
 * Return code: 0
 */
#define MC_CMD_GET_PHY_STATE 0x43

#define MC_CMD_GET_PHY_STATE_IN_LEN 0
#define MC_CMD_GET_PHY_STATE_OUT_LEN 4
#define MC_CMD_GET_PHY_STATE_STATE_OFST 0
/* PHY state enumeration: */
#define MC_CMD_PHY_STATE_OK 1
#define MC_CMD_PHY_STATE_ZOMBIE 2


/* 802.1Qbb control. 8 Tx queues that map to priorities 0 - 7. Use all 1s to
 * disable 802.Qbb for a given priority. */
#define MC_CMD_SETUP_8021QBB 0x44
#define MC_CMD_SETUP_8021QBB_IN_LEN 32
#define MC_CMD_SETUP_8021QBB_OUT_LEN 0
#define MC_CMD_SETUP_8021QBB_IN_TXQS_OFFST 0


/* MC_CMD_WOL_FILTER_GET:
 * Retrieve ID of any WoL filters
 *
 * Locks required: None
 * Returns: 0, ENOSYS
 */
#define MC_CMD_WOL_FILTER_GET 0x45
#define MC_CMD_WOL_FILTER_GET_IN_LEN 0
#define MC_CMD_WOL_FILTER_GET_OUT_LEN 4
#define MC_CMD_WOL_FILTER_GET_OUT_FILTER_ID_OFST 0


/* MC_CMD_ADD_LIGHTSOUT_OFFLOAD:
 * Offload a protocol to NIC for lights-out state
 *
 * Locks required: None
 * Returns: 0, ENOSYS
 */
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD 0x46

#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_LEN 16
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_PROTOCOL_OFST 0

/* There is a union at offset 4, following defines overlap due to
 * this */
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_DATA_OFST 4
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_ARPMAC_OFST 4
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_ARPIP_OFST 10
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NSMAC_OFST 4
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NSSNIPV6_OFST 10
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_IN_NSIPV6_OFST 26

#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_OUT_LEN 4
#define MC_CMD_ADD_LIGHTSOUT_OFFLOAD_OUT_FILTER_ID_OFST 0


/* MC_CMD_REMOVE_LIGHTSOUT_PROTOCOL_OFFLOAD:
 * Offload a protocol to NIC for lights-out state
 *
 * Locks required: None
 * Returns: 0, ENOSYS
 */
#define MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD 0x47
#define MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_IN_LEN 8
#define MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_OUT_LEN 0

#define MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_IN_PROTOCOL_OFST 0
#define MC_CMD_REMOVE_LIGHTSOUT_OFFLOAD_IN_FILTER_ID_OFST 4

/* Lights-out offload protocols enumeration */
#define MC_CMD_LIGHTSOUT_OFFLOAD_PROTOCOL_ARP 0x1
#define MC_CMD_LIGHTSOUT_OFFLOAD_PROTOCOL_NS  0x2


/* MC_CMD_MAC_RESET_RESTORE:
 * Restore MAC after block reset
 *
 * Locks required: None
 * Returns: 0
 */

#define MC_CMD_MAC_RESET_RESTORE 0x48
#define MC_CMD_MAC_RESET_RESTORE_IN_LEN 0
#define MC_CMD_MAC_RESET_RESTORE_OUT_LEN 0


/* MC_CMD_TEST_ASSERT:
 * Deliberately trigger an assert-detonation in the firmware for testing
 * purposes (i.e. to allow tests that the driver copes gracefully).
 *
 * Locks required: None
 * Returns: 0
 */

#define MC_CMD_TESTASSERT 0x49
#define MC_CMD_TESTASSERT_IN_LEN 0
#define MC_CMD_TESTASSERT_OUT_LEN 0

/* MC_CMD_WORKAROUND 0x4a
 *
 * Enable/Disable a given workaround. The mcfw will return EINVAL if it
 * doesn't understand the given workaround number - which should not
 * be treated as a hard error by client code.
 *
 * This op does not imply any semantics about each workaround, that's between
 * the driver and the mcfw on a per-workaround basis.
 *
 * Locks required: None
 * Returns: 0, EINVAL
 */
#define MC_CMD_WORKAROUND 0x4a
#define MC_CMD_WORKAROUND_IN_LEN 8
#define MC_CMD_WORKAROUND_IN_TYPE_OFST 0
#define MC_CMD_WORKAROUND_BUG17230 1
#define MC_CMD_WORKAROUND_IN_ENABLED_OFST 4
#define MC_CMD_WORKAROUND_OUT_LEN 0

/* MC_CMD_GET_PHY_MEDIA_INFO:
 * Read media-specific data from PHY (e.g. SFP/SFP+ module ID information for
 * SFP+ PHYs).
 *
 * The "media type" can be found via GET_PHY_CFG (GET_PHY_CFG_OUT_MEDIA_TYPE);
 * the valid "page number" input values, and the output data, are interpreted
 * on a per-type basis.
 *
 * For SFP+: PAGE=0 or 1 returns a 128-byte block read from module I2C address
 *           0xA0 offset 0 or 0x80.
 * Anything else: currently undefined.
 *
 * Locks required: None
 * Return code: 0
 */
#define MC_CMD_GET_PHY_MEDIA_INFO 0x4b
#define MC_CMD_GET_PHY_MEDIA_INFO_IN_LEN 4
#define MC_CMD_GET_PHY_MEDIA_INFO_IN_PAGE_OFST 0
#define MC_CMD_GET_PHY_MEDIA_INFO_OUT_LEN(_num_bytes) (4 + (_num_bytes))
#define MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATALEN_OFST 0
#define MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_OFST 4

/* MC_CMD_NVRAM_TEST:
 * Test a particular NVRAM partition for valid contents (where "valid"
 * depends on the type of partition).
 *
 * Locks required: None
 * Return code: 0
 */
#define MC_CMD_NVRAM_TEST 0x4c
#define MC_CMD_NVRAM_TEST_IN_LEN 4
#define MC_CMD_NVRAM_TEST_IN_TYPE_OFST 0
#define MC_CMD_NVRAM_TEST_OUT_LEN 4
#define MC_CMD_NVRAM_TEST_OUT_RESULT_OFST 0
#define MC_CMD_NVRAM_TEST_PASS 0
#define MC_CMD_NVRAM_TEST_FAIL 1
#define MC_CMD_NVRAM_TEST_NOTSUPP 2

/* MC_CMD_MRSFP_TWEAK: (debug)
 * Read status and/or set parameters for the "mrsfp" driver in mr_rusty builds.
 * I2C I/O expander bits are always read; if equaliser parameters are supplied,
 * they are configured first.
 *
 * Locks required: None
 * Return code: 0, EINVAL
 */
#define MC_CMD_MRSFP_TWEAK 0x4d
#define MC_CMD_MRSFP_TWEAK_IN_LEN_READ_ONLY 0
#define MC_CMD_MRSFP_TWEAK_IN_LEN_EQ_CONFIG 16
#define MC_CMD_MRSFP_TWEAK_IN_TXEQ_LEVEL_OFST 0    /* 0-6 low->high de-emph. */
#define MC_CMD_MRSFP_TWEAK_IN_TXEQ_DT_CFG_OFST 4   /* 0-8 low->high ref.V */
#define MC_CMD_MRSFP_TWEAK_IN_RXEQ_BOOST_OFST 8    /* 0-8 low->high boost */
#define MC_CMD_MRSFP_TWEAK_IN_RXEQ_DT_CFG_OFST 12  /* 0-8 low->high ref.V */
#define MC_CMD_MRSFP_TWEAK_OUT_LEN 12
#define MC_CMD_MRSFP_TWEAK_OUT_IOEXP_INPUTS_OFST 0     /* input bits */
#define MC_CMD_MRSFP_TWEAK_OUT_IOEXP_OUTPUTS_OFST 4    /* output bits */
#define MC_CMD_MRSFP_TWEAK_OUT_IOEXP_DIRECTION_OFST 8  /* dirs: 0=out, 1=in */

/* MC_CMD_TEST_HACK: (debug (unsurprisingly))
  * Change bits of network port state for test purposes in ways that would never be
  * useful in normal operation and so need a special command to change. */
#define MC_CMD_TEST_HACK 0x2f
#define MC_CMD_TEST_HACK_IN_LEN 8
#define MC_CMD_TEST_HACK_IN_TXPAD_OFST 0
#define   MC_CMD_TEST_HACK_IN_TXPAD_AUTO  0 /* Let the MC manage things */
#define   MC_CMD_TEST_HACK_IN_TXPAD_ON    1 /* Force on */
#define   MC_CMD_TEST_HACK_IN_TXPAD_OFF   2 /* Force on */
#define MC_CMD_TEST_HACK_IN_IPG_OFST   4 /* Takes a value in bits */
#define   MC_CMD_TEST_HACK_IN_IPG_AUTO    0 /* The MC picks the value */
#define MC_CMD_TEST_HACK_OUT_LEN 0

/* MC_CMD_SENSOR_SET_LIMS: (debug) (mostly) adjust the sensor limits. This
 * is a warranty-voiding operation.
  *
 * IN: sensor identifier (one of the enumeration starting with MC_CMD_SENSOR_CONTROLLER_TEMP
 * followed by 4 32-bit values: min(warning) max(warning), min(fatal), max(fatal). Which
 * of these limits are meaningful and what their interpretation is is sensor-specific.
 *
 * OUT: nothing
 *
 * Returns: ENOENT if the sensor specified does not exist, EINVAL if the limits are
  * out of range.
 */
#define MC_CMD_SENSOR_SET_LIMS 0x4e
#define MC_CMD_SENSOR_SET_LIMS_IN_LEN 20
#define MC_CMD_SENSOR_SET_LIMS_IN_SENSOR_OFST 0
#define MC_CMD_SENSOR_SET_LIMS_IN_LOW0_OFST 4
#define MC_CMD_SENSOR_SET_LIMS_IN_HI0_OFST  8
#define MC_CMD_SENSOR_SET_LIMS_IN_LOW1_OFST 12
#define MC_CMD_SENSOR_SET_LIMS_IN_HI1_OFST  16

/* Do NOT add new commands beyond 0x4f as part of 3.0 : 0x50 - 0x7f will be
 * used for post-3.0 extensions. If you run out of space, look for gaps or
 * commands that are unused in the existing range. */

#endif /* MCDI_PCOL_H */
