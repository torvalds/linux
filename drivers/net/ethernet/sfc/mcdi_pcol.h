/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2009-2013 Solarflare Communications Inc.
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
/* If this is set in MC_RESET_STATE_REG then it should be
 * possible to jump into IMEM without loading code from flash.
 * Unlike a warm boot, assume DMEM has been reloaded, so that
 * the MC persistent data must be reinitialised. */
#define MC_FW_TEPID_BOOT_OK (16)
/* BIST state has been initialized */
#define MC_FW_BIST_INIT_OK (128)

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

/* Check whether an mcfw version (in host order) belongs to a bootloader */
#define MC_FW_VERSION_IS_BOOTLOADER(_v) (((_v) >> 16) == 0xb007)

/* The current version of the MCDI protocol.
 *
 * Note that the ROM burnt into the card only talks V0, so at the very
 * least every driver must support version 0 and MCDI_PCOL_VERSION
 */
#define MCDI_PCOL_VERSION 2

/* Unused commands: 0x23, 0x27, 0x30, 0x31 */

/* MCDI version 1
 *
 * Each MCDI request starts with an MCDI_HEADER, which is a 32bit
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
#define MCDI_HEADER_SEQ_WIDTH 4
#define MCDI_HEADER_RSVD_LBN 20
#define MCDI_HEADER_RSVD_WIDTH 1
#define MCDI_HEADER_NOT_EPOCH_LBN 21
#define MCDI_HEADER_NOT_EPOCH_WIDTH 1
#define MCDI_HEADER_ERROR_LBN 22
#define MCDI_HEADER_ERROR_WIDTH 1
#define MCDI_HEADER_RESPONSE_LBN 23
#define MCDI_HEADER_RESPONSE_WIDTH 1
#define MCDI_HEADER_XFLAGS_LBN 24
#define MCDI_HEADER_XFLAGS_WIDTH 8
/* Request response using event */
#define MCDI_HEADER_XFLAGS_EVREQ 0x01

/* Maximum number of payload bytes */
#define MCDI_CTL_SDU_LEN_MAX_V1 0xfc
#define MCDI_CTL_SDU_LEN_MAX_V2 0x400

#define MCDI_CTL_SDU_LEN_MAX MCDI_CTL_SDU_LEN_MAX_V2


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


/* Operation not permitted. */
#define MC_CMD_ERR_EPERM 1
/* Non-existent command target */
#define MC_CMD_ERR_ENOENT 2
/* assert() has killed the MC */
#define MC_CMD_ERR_EINTR 4
/* I/O failure */
#define MC_CMD_ERR_EIO 5
/* Try again */
#define MC_CMD_ERR_EAGAIN 11
/* Out of memory */
#define MC_CMD_ERR_ENOMEM 12
/* Caller does not hold required locks */
#define MC_CMD_ERR_EACCES 13
/* Resource is currently unavailable (e.g. lock contention) */
#define MC_CMD_ERR_EBUSY 16
/* No such device */
#define MC_CMD_ERR_ENODEV 19
/* Invalid argument to target */
#define MC_CMD_ERR_EINVAL 22
/* Out of range */
#define MC_CMD_ERR_ERANGE 34
/* Non-recursive resource is already acquired */
#define MC_CMD_ERR_EDEADLK 35
/* Operation not implemented */
#define MC_CMD_ERR_ENOSYS 38
/* Operation timed out */
#define MC_CMD_ERR_ETIME 62
/* Link has been severed */
#define MC_CMD_ERR_ENOLINK 67
/* Protocol error */
#define MC_CMD_ERR_EPROTO 71
/* Operation not supported */
#define MC_CMD_ERR_ENOTSUP 95
/* Address not available */
#define MC_CMD_ERR_EADDRNOTAVAIL 99
/* Not connected */
#define MC_CMD_ERR_ENOTCONN 107
/* Operation already in progress */
#define MC_CMD_ERR_EALREADY 114

/* Resource allocation failed. */
#define MC_CMD_ERR_ALLOC_FAIL  0x1000
/* V-adaptor not found. */
#define MC_CMD_ERR_NO_VADAPTOR 0x1001
/* EVB port not found. */
#define MC_CMD_ERR_NO_EVB_PORT 0x1002
/* V-switch not found. */
#define MC_CMD_ERR_NO_VSWITCH  0x1003
/* Too many VLAN tags. */
#define MC_CMD_ERR_VLAN_LIMIT  0x1004
/* Bad PCI function number. */
#define MC_CMD_ERR_BAD_PCI_FUNC 0x1005
/* Invalid VLAN mode. */
#define MC_CMD_ERR_BAD_VLAN_MODE 0x1006
/* Invalid v-switch type. */
#define MC_CMD_ERR_BAD_VSWITCH_TYPE 0x1007
/* Invalid v-port type. */
#define MC_CMD_ERR_BAD_VPORT_TYPE 0x1008
/* MAC address exists. */
#define MC_CMD_ERR_MAC_EXIST 0x1009
/* Slave core not present */
#define MC_CMD_ERR_SLAVE_NOT_PRESENT 0x100a

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
#define SIENA_MC_BOOTROM_COPYCODE_VEC (0x800 - 3 * 0x4)
#define HUNT_MC_BOOTROM_COPYCODE_VEC (0x8000 - 3 * 0x4)
/* Points to the recovery mode entry point. */
#define SIENA_MC_BOOTROM_NOFLASH_VEC (0x800 - 2 * 0x4)
#define HUNT_MC_BOOTROM_NOFLASH_VEC (0x8000 - 2 * 0x4)

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


/* Version 2 adds an optional argument to error returns: the errno value
 * may be followed by the (0-based) number of the first argument that
 * could not be processed.
 */
#define MC_CMD_ERR_ARG_OFST 4

/* No space */
#define MC_CMD_ERR_ENOSPC 28

/* MCDI_EVENT structuredef */
#define    MCDI_EVENT_LEN 8
#define       MCDI_EVENT_CONT_LBN 32
#define       MCDI_EVENT_CONT_WIDTH 1
#define       MCDI_EVENT_LEVEL_LBN 33
#define       MCDI_EVENT_LEVEL_WIDTH 3
/* enum: Info. */
#define          MCDI_EVENT_LEVEL_INFO  0x0
/* enum: Warning. */
#define          MCDI_EVENT_LEVEL_WARN 0x1
/* enum: Error. */
#define          MCDI_EVENT_LEVEL_ERR 0x2
/* enum: Fatal. */
#define          MCDI_EVENT_LEVEL_FATAL 0x3
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
/* enum: 100Mbs */
#define          MCDI_EVENT_LINKCHANGE_SPEED_100M  0x1
/* enum: 1Gbs */
#define          MCDI_EVENT_LINKCHANGE_SPEED_1G  0x2
/* enum: 10Gbs */
#define          MCDI_EVENT_LINKCHANGE_SPEED_10G  0x3
/* enum: 40Gbs */
#define          MCDI_EVENT_LINKCHANGE_SPEED_40G  0x4
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
/* enum: SRAM Access. */
#define          MCDI_EVENT_FWALERT_REASON_SRAM_ACCESS 0x1
#define        MCDI_EVENT_FLR_VF_LBN 0
#define        MCDI_EVENT_FLR_VF_WIDTH 8
#define        MCDI_EVENT_TX_ERR_TXQ_LBN 0
#define        MCDI_EVENT_TX_ERR_TXQ_WIDTH 12
#define        MCDI_EVENT_TX_ERR_TYPE_LBN 12
#define        MCDI_EVENT_TX_ERR_TYPE_WIDTH 4
/* enum: Descriptor loader reported failure */
#define          MCDI_EVENT_TX_ERR_DL_FAIL 0x1
/* enum: Descriptor ring empty and no EOP seen for packet */
#define          MCDI_EVENT_TX_ERR_NO_EOP 0x2
/* enum: Overlength packet */
#define          MCDI_EVENT_TX_ERR_2BIG 0x3
/* enum: Malformed option descriptor */
#define          MCDI_EVENT_TX_BAD_OPTDESC 0x5
/* enum: Option descriptor part way through a packet */
#define          MCDI_EVENT_TX_OPT_IN_PKT 0x8
/* enum: DMA or PIO data access error */
#define          MCDI_EVENT_TX_ERR_BAD_DMA_OR_PIO 0x9
#define        MCDI_EVENT_TX_ERR_INFO_LBN 16
#define        MCDI_EVENT_TX_ERR_INFO_WIDTH 16
#define        MCDI_EVENT_TX_FLUSH_TO_DRIVER_LBN 12
#define        MCDI_EVENT_TX_FLUSH_TO_DRIVER_WIDTH 1
#define        MCDI_EVENT_TX_FLUSH_TXQ_LBN 0
#define        MCDI_EVENT_TX_FLUSH_TXQ_WIDTH 12
#define        MCDI_EVENT_PTP_ERR_TYPE_LBN 0
#define        MCDI_EVENT_PTP_ERR_TYPE_WIDTH 8
/* enum: PLL lost lock */
#define          MCDI_EVENT_PTP_ERR_PLL_LOST 0x1
/* enum: Filter overflow (PDMA) */
#define          MCDI_EVENT_PTP_ERR_FILTER 0x2
/* enum: FIFO overflow (FPGA) */
#define          MCDI_EVENT_PTP_ERR_FIFO 0x3
/* enum: Merge queue overflow */
#define          MCDI_EVENT_PTP_ERR_QUEUE 0x4
#define        MCDI_EVENT_AOE_ERR_TYPE_LBN 0
#define        MCDI_EVENT_AOE_ERR_TYPE_WIDTH 8
/* enum: AOE failed to load - no valid image? */
#define          MCDI_EVENT_AOE_NO_LOAD 0x1
/* enum: AOE FC reported an exception */
#define          MCDI_EVENT_AOE_FC_ASSERT 0x2
/* enum: AOE FC watchdogged */
#define          MCDI_EVENT_AOE_FC_WATCHDOG 0x3
/* enum: AOE FC failed to start */
#define          MCDI_EVENT_AOE_FC_NO_START 0x4
/* enum: Generic AOE fault - likely to have been reported via other means too
 * but intended for use by aoex driver.
 */
#define          MCDI_EVENT_AOE_FAULT 0x5
/* enum: Results of reprogramming the CPLD (status in AOE_ERR_DATA) */
#define          MCDI_EVENT_AOE_CPLD_REPROGRAMMED 0x6
/* enum: AOE loaded successfully */
#define          MCDI_EVENT_AOE_LOAD 0x7
/* enum: AOE DMA operation completed (LSB of HOST_HANDLE in AOE_ERR_DATA) */
#define          MCDI_EVENT_AOE_DMA 0x8
/* enum: AOE byteblaster connected/disconnected (Connection status in
 * AOE_ERR_DATA)
 */
#define          MCDI_EVENT_AOE_BYTEBLASTER 0x9
#define        MCDI_EVENT_AOE_ERR_DATA_LBN 8
#define        MCDI_EVENT_AOE_ERR_DATA_WIDTH 8
#define        MCDI_EVENT_RX_ERR_RXQ_LBN 0
#define        MCDI_EVENT_RX_ERR_RXQ_WIDTH 12
#define        MCDI_EVENT_RX_ERR_TYPE_LBN 12
#define        MCDI_EVENT_RX_ERR_TYPE_WIDTH 4
#define        MCDI_EVENT_RX_ERR_INFO_LBN 16
#define        MCDI_EVENT_RX_ERR_INFO_WIDTH 16
#define        MCDI_EVENT_RX_FLUSH_TO_DRIVER_LBN 12
#define        MCDI_EVENT_RX_FLUSH_TO_DRIVER_WIDTH 1
#define        MCDI_EVENT_RX_FLUSH_RXQ_LBN 0
#define        MCDI_EVENT_RX_FLUSH_RXQ_WIDTH 12
#define        MCDI_EVENT_MC_REBOOT_COUNT_LBN 0
#define        MCDI_EVENT_MC_REBOOT_COUNT_WIDTH 16
#define       MCDI_EVENT_DATA_LBN 0
#define       MCDI_EVENT_DATA_WIDTH 32
#define       MCDI_EVENT_SRC_LBN 36
#define       MCDI_EVENT_SRC_WIDTH 8
#define       MCDI_EVENT_EV_CODE_LBN 60
#define       MCDI_EVENT_EV_CODE_WIDTH 4
#define       MCDI_EVENT_CODE_LBN 44
#define       MCDI_EVENT_CODE_WIDTH 8
/* enum: Bad assert. */
#define          MCDI_EVENT_CODE_BADSSERT 0x1
/* enum: PM Notice. */
#define          MCDI_EVENT_CODE_PMNOTICE 0x2
/* enum: Command done. */
#define          MCDI_EVENT_CODE_CMDDONE 0x3
/* enum: Link change. */
#define          MCDI_EVENT_CODE_LINKCHANGE 0x4
/* enum: Sensor Event. */
#define          MCDI_EVENT_CODE_SENSOREVT 0x5
/* enum: Schedule error. */
#define          MCDI_EVENT_CODE_SCHEDERR 0x6
/* enum: Reboot. */
#define          MCDI_EVENT_CODE_REBOOT 0x7
/* enum: Mac stats DMA. */
#define          MCDI_EVENT_CODE_MAC_STATS_DMA 0x8
/* enum: Firmware alert. */
#define          MCDI_EVENT_CODE_FWALERT 0x9
/* enum: Function level reset. */
#define          MCDI_EVENT_CODE_FLR 0xa
/* enum: Transmit error */
#define          MCDI_EVENT_CODE_TX_ERR 0xb
/* enum: Tx flush has completed */
#define          MCDI_EVENT_CODE_TX_FLUSH  0xc
/* enum: PTP packet received timestamp */
#define          MCDI_EVENT_CODE_PTP_RX  0xd
/* enum: PTP NIC failure */
#define          MCDI_EVENT_CODE_PTP_FAULT  0xe
/* enum: PTP PPS event */
#define          MCDI_EVENT_CODE_PTP_PPS  0xf
/* enum: Rx flush has completed */
#define          MCDI_EVENT_CODE_RX_FLUSH  0x10
/* enum: Receive error */
#define          MCDI_EVENT_CODE_RX_ERR 0x11
/* enum: AOE fault */
#define          MCDI_EVENT_CODE_AOE  0x12
/* enum: Network port calibration failed (VCAL). */
#define          MCDI_EVENT_CODE_VCAL_FAIL  0x13
/* enum: HW PPS event */
#define          MCDI_EVENT_CODE_HW_PPS  0x14
/* enum: The MC has rebooted (huntington and later, siena uses CODE_REBOOT and
 * a different format)
 */
#define          MCDI_EVENT_CODE_MC_REBOOT 0x15
/* enum: the MC has detected a parity error */
#define          MCDI_EVENT_CODE_PAR_ERR 0x16
/* enum: the MC has detected a correctable error */
#define          MCDI_EVENT_CODE_ECC_CORR_ERR 0x17
/* enum: the MC has detected an uncorrectable error */
#define          MCDI_EVENT_CODE_ECC_FATAL_ERR 0x18
/* enum: Artificial event generated by host and posted via MC for test
 * purposes.
 */
#define          MCDI_EVENT_CODE_TESTGEN  0xfa
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
/* Seconds field of timestamp */
#define       MCDI_EVENT_PTP_SECONDS_OFST 0
#define       MCDI_EVENT_PTP_SECONDS_LBN 0
#define       MCDI_EVENT_PTP_SECONDS_WIDTH 32
/* Nanoseconds field of timestamp */
#define       MCDI_EVENT_PTP_NANOSECONDS_OFST 0
#define       MCDI_EVENT_PTP_NANOSECONDS_LBN 0
#define       MCDI_EVENT_PTP_NANOSECONDS_WIDTH 32
/* Lowest four bytes of sourceUUID from PTP packet */
#define       MCDI_EVENT_PTP_UUID_OFST 0
#define       MCDI_EVENT_PTP_UUID_LBN 0
#define       MCDI_EVENT_PTP_UUID_WIDTH 32
#define       MCDI_EVENT_RX_ERR_DATA_OFST 0
#define       MCDI_EVENT_RX_ERR_DATA_LBN 0
#define       MCDI_EVENT_RX_ERR_DATA_WIDTH 32
#define       MCDI_EVENT_PAR_ERR_DATA_OFST 0
#define       MCDI_EVENT_PAR_ERR_DATA_LBN 0
#define       MCDI_EVENT_PAR_ERR_DATA_WIDTH 32
#define       MCDI_EVENT_ECC_CORR_ERR_DATA_OFST 0
#define       MCDI_EVENT_ECC_CORR_ERR_DATA_LBN 0
#define       MCDI_EVENT_ECC_CORR_ERR_DATA_WIDTH 32
#define       MCDI_EVENT_ECC_FATAL_ERR_DATA_OFST 0
#define       MCDI_EVENT_ECC_FATAL_ERR_DATA_LBN 0
#define       MCDI_EVENT_ECC_FATAL_ERR_DATA_WIDTH 32

/* FCDI_EVENT structuredef */
#define    FCDI_EVENT_LEN 8
#define       FCDI_EVENT_CONT_LBN 32
#define       FCDI_EVENT_CONT_WIDTH 1
#define       FCDI_EVENT_LEVEL_LBN 33
#define       FCDI_EVENT_LEVEL_WIDTH 3
/* enum: Info. */
#define          FCDI_EVENT_LEVEL_INFO  0x0
/* enum: Warning. */
#define          FCDI_EVENT_LEVEL_WARN 0x1
/* enum: Error. */
#define          FCDI_EVENT_LEVEL_ERR 0x2
/* enum: Fatal. */
#define          FCDI_EVENT_LEVEL_FATAL 0x3
#define       FCDI_EVENT_DATA_OFST 0
#define        FCDI_EVENT_LINK_STATE_STATUS_LBN 0
#define        FCDI_EVENT_LINK_STATE_STATUS_WIDTH 1
#define          FCDI_EVENT_LINK_DOWN 0x0 /* enum */
#define          FCDI_EVENT_LINK_UP 0x1 /* enum */
#define       FCDI_EVENT_DATA_LBN 0
#define       FCDI_EVENT_DATA_WIDTH 32
#define       FCDI_EVENT_SRC_LBN 36
#define       FCDI_EVENT_SRC_WIDTH 8
#define       FCDI_EVENT_EV_CODE_LBN 60
#define       FCDI_EVENT_EV_CODE_WIDTH 4
#define       FCDI_EVENT_CODE_LBN 44
#define       FCDI_EVENT_CODE_WIDTH 8
/* enum: The FC was rebooted. */
#define          FCDI_EVENT_CODE_REBOOT 0x1
/* enum: Bad assert. */
#define          FCDI_EVENT_CODE_ASSERT 0x2
/* enum: DDR3 test result. */
#define          FCDI_EVENT_CODE_DDR_TEST_RESULT 0x3
/* enum: Link status. */
#define          FCDI_EVENT_CODE_LINK_STATE 0x4
/* enum: A timed read is ready to be serviced. */
#define          FCDI_EVENT_CODE_TIMED_READ 0x5
/* enum: One or more PPS IN events */
#define          FCDI_EVENT_CODE_PPS_IN 0x6
/* enum: One or more PPS OUT events */
#define          FCDI_EVENT_CODE_PPS_OUT 0x7
#define       FCDI_EVENT_ASSERT_INSTR_ADDRESS_OFST 0
#define       FCDI_EVENT_ASSERT_INSTR_ADDRESS_LBN 0
#define       FCDI_EVENT_ASSERT_INSTR_ADDRESS_WIDTH 32
#define       FCDI_EVENT_ASSERT_TYPE_LBN 36
#define       FCDI_EVENT_ASSERT_TYPE_WIDTH 8
#define       FCDI_EVENT_DDR_TEST_RESULT_STATUS_CODE_LBN 36
#define       FCDI_EVENT_DDR_TEST_RESULT_STATUS_CODE_WIDTH 8
#define       FCDI_EVENT_DDR_TEST_RESULT_RESULT_OFST 0
#define       FCDI_EVENT_DDR_TEST_RESULT_RESULT_LBN 0
#define       FCDI_EVENT_DDR_TEST_RESULT_RESULT_WIDTH 32
#define       FCDI_EVENT_LINK_STATE_DATA_OFST 0
#define       FCDI_EVENT_LINK_STATE_DATA_LBN 0
#define       FCDI_EVENT_LINK_STATE_DATA_WIDTH 32
#define       FCDI_EVENT_PPS_COUNT_OFST 0
#define       FCDI_EVENT_PPS_COUNT_LBN 0
#define       FCDI_EVENT_PPS_COUNT_WIDTH 32

/* FCDI_EXTENDED_EVENT structuredef */
#define    FCDI_EXTENDED_EVENT_LENMIN 16
#define    FCDI_EXTENDED_EVENT_LENMAX 248
#define    FCDI_EXTENDED_EVENT_LEN(num) (8+8*(num))
/* Number of timestamps following */
#define       FCDI_EXTENDED_EVENT_PPS_COUNT_OFST 0
#define       FCDI_EXTENDED_EVENT_PPS_COUNT_LBN 0
#define       FCDI_EXTENDED_EVENT_PPS_COUNT_WIDTH 32
/* Seconds field of a timestamp record */
#define       FCDI_EXTENDED_EVENT_PPS_SECONDS_OFST 8
#define       FCDI_EXTENDED_EVENT_PPS_SECONDS_LBN 64
#define       FCDI_EXTENDED_EVENT_PPS_SECONDS_WIDTH 32
/* Nanoseconds field of a timestamp record */
#define       FCDI_EXTENDED_EVENT_PPS_NANOSECONDS_OFST 12
#define       FCDI_EXTENDED_EVENT_PPS_NANOSECONDS_LBN 96
#define       FCDI_EXTENDED_EVENT_PPS_NANOSECONDS_WIDTH 32
/* Timestamp records comprising the event */
#define       FCDI_EXTENDED_EVENT_PPS_TIME_OFST 8
#define       FCDI_EXTENDED_EVENT_PPS_TIME_LEN 8
#define       FCDI_EXTENDED_EVENT_PPS_TIME_LO_OFST 8
#define       FCDI_EXTENDED_EVENT_PPS_TIME_HI_OFST 12
#define       FCDI_EXTENDED_EVENT_PPS_TIME_MINNUM 1
#define       FCDI_EXTENDED_EVENT_PPS_TIME_MAXNUM 30
#define       FCDI_EXTENDED_EVENT_PPS_TIME_LBN 64
#define       FCDI_EXTENDED_EVENT_PPS_TIME_WIDTH 64


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
/* Source address */
#define       MC_CMD_COPYCODE_IN_SRC_ADDR_OFST 0
/* enum: Entering the main image via a copy of a single word from and to this
 * address indicates that it should not attempt to start the datapath CPUs.
 * This is useful for certain soft rebooting scenarios. (Huntington only)
 */
#define          MC_CMD_COPYCODE_HUNT_NO_DATAPATH_MAGIC_ADDR 0x1d0d0
/* enum: Entering the main image via a copy of a single word from and to this
 * address indicates that it should not attempt to parse any configuration from
 * flash. (In addition, the datapath CPUs will not be started, as for
 * MC_CMD_COPYCODE_HUNT_NO_DATAPATH_MAGIC_ADDR above.) This is useful for
 * certain soft rebooting scenarios. (Huntington only)
 */
#define          MC_CMD_COPYCODE_HUNT_IGNORE_CONFIG_MAGIC_ADDR 0x1badc
/* Destination address */
#define       MC_CMD_COPYCODE_IN_DEST_ADDR_OFST 4
#define       MC_CMD_COPYCODE_IN_NUMWORDS_OFST 8
/* Address of where to jump after copy. */
#define       MC_CMD_COPYCODE_IN_JUMP_OFST 12
/* enum: Control should return to the caller rather than jumping */
#define          MC_CMD_COPYCODE_JUMP_NONE 0x1

/* MC_CMD_COPYCODE_OUT msgresponse */
#define    MC_CMD_COPYCODE_OUT_LEN 0


/***********************************/
/* MC_CMD_SET_FUNC
 * Select function for function-specific commands.
 */
#define MC_CMD_SET_FUNC 0x4

/* MC_CMD_SET_FUNC_IN msgrequest */
#define    MC_CMD_SET_FUNC_IN_LEN 4
/* Set function */
#define       MC_CMD_SET_FUNC_IN_FUNC_OFST 0

/* MC_CMD_SET_FUNC_OUT msgresponse */
#define    MC_CMD_SET_FUNC_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_BOOT_STATUS
 * Get the instruction address from which the MC booted.
 */
#define MC_CMD_GET_BOOT_STATUS 0x5

/* MC_CMD_GET_BOOT_STATUS_IN msgrequest */
#define    MC_CMD_GET_BOOT_STATUS_IN_LEN 0

/* MC_CMD_GET_BOOT_STATUS_OUT msgresponse */
#define    MC_CMD_GET_BOOT_STATUS_OUT_LEN 8
/* ?? */
#define       MC_CMD_GET_BOOT_STATUS_OUT_BOOT_OFFSET_OFST 0
/* enum: indicates that the MC wasn't flash booted */
#define          MC_CMD_GET_BOOT_STATUS_OUT_BOOT_OFFSET_NULL  0xdeadbeef
#define       MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_OFST 4
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_WATCHDOG_LBN 0
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_WATCHDOG_WIDTH 1
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_PRIMARY_LBN 1
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_PRIMARY_WIDTH 1
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_BACKUP_LBN 2
#define        MC_CMD_GET_BOOT_STATUS_OUT_FLAGS_BACKUP_WIDTH 1


/***********************************/
/* MC_CMD_GET_ASSERTS
 * Get (and optionally clear) the current assertion status. Only
 * OUT.GLOBAL_FLAGS is guaranteed to exist in the completion payload. The other
 * fields will only be present if OUT.GLOBAL_FLAGS != NO_FAILS
 */
#define MC_CMD_GET_ASSERTS 0x6

/* MC_CMD_GET_ASSERTS_IN msgrequest */
#define    MC_CMD_GET_ASSERTS_IN_LEN 4
/* Set to clear assertion */
#define       MC_CMD_GET_ASSERTS_IN_CLEAR_OFST 0

/* MC_CMD_GET_ASSERTS_OUT msgresponse */
#define    MC_CMD_GET_ASSERTS_OUT_LEN 140
/* Assertion status flag. */
#define       MC_CMD_GET_ASSERTS_OUT_GLOBAL_FLAGS_OFST 0
/* enum: No assertions have failed. */
#define          MC_CMD_GET_ASSERTS_FLAGS_NO_FAILS 0x1
/* enum: A system-level assertion has failed. */
#define          MC_CMD_GET_ASSERTS_FLAGS_SYS_FAIL 0x2
/* enum: A thread-level assertion has failed. */
#define          MC_CMD_GET_ASSERTS_FLAGS_THR_FAIL 0x3
/* enum: The system was reset by the watchdog. */
#define          MC_CMD_GET_ASSERTS_FLAGS_WDOG_FIRED 0x4
/* enum: An illegal address trap stopped the system (huntington and later) */
#define          MC_CMD_GET_ASSERTS_FLAGS_ADDR_TRAP 0x5
/* Failing PC value */
#define       MC_CMD_GET_ASSERTS_OUT_SAVED_PC_OFFS_OFST 4
/* Saved GP regs */
#define       MC_CMD_GET_ASSERTS_OUT_GP_REGS_OFFS_OFST 8
#define       MC_CMD_GET_ASSERTS_OUT_GP_REGS_OFFS_LEN 4
#define       MC_CMD_GET_ASSERTS_OUT_GP_REGS_OFFS_NUM 31
/* Failing thread address */
#define       MC_CMD_GET_ASSERTS_OUT_THREAD_OFFS_OFST 132
#define       MC_CMD_GET_ASSERTS_OUT_RESERVED_OFST 136


/***********************************/
/* MC_CMD_LOG_CTRL
 * Configure the output stream for various events and messages.
 */
#define MC_CMD_LOG_CTRL 0x7

/* MC_CMD_LOG_CTRL_IN msgrequest */
#define    MC_CMD_LOG_CTRL_IN_LEN 8
/* Log destination */
#define       MC_CMD_LOG_CTRL_IN_LOG_DEST_OFST 0
/* enum: UART. */
#define          MC_CMD_LOG_CTRL_IN_LOG_DEST_UART 0x1
/* enum: Event queue. */
#define          MC_CMD_LOG_CTRL_IN_LOG_DEST_EVQ 0x2
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

/* MC_CMD_GET_VERSION_EXT_IN msgrequest: Asks for the extended version */
#define    MC_CMD_GET_VERSION_EXT_IN_LEN 4
/* placeholder, set to 0 */
#define       MC_CMD_GET_VERSION_EXT_IN_EXT_FLAGS_OFST 0

/* MC_CMD_GET_VERSION_V0_OUT msgresponse: deprecated version format */
#define    MC_CMD_GET_VERSION_V0_OUT_LEN 4
#define       MC_CMD_GET_VERSION_OUT_FIRMWARE_OFST 0
/* enum: Reserved version number to indicate "any" version. */
#define          MC_CMD_GET_VERSION_OUT_FIRMWARE_ANY 0xffffffff
/* enum: Bootrom version value for Siena. */
#define          MC_CMD_GET_VERSION_OUT_FIRMWARE_SIENA_BOOTROM 0xb0070000
/* enum: Bootrom version value for Huntington. */
#define          MC_CMD_GET_VERSION_OUT_FIRMWARE_HUNT_BOOTROM 0xb0070001

/* MC_CMD_GET_VERSION_OUT msgresponse */
#define    MC_CMD_GET_VERSION_OUT_LEN 32
/*            MC_CMD_GET_VERSION_OUT_FIRMWARE_OFST 0 */
/*            Enum values, see field(s): */
/*               MC_CMD_GET_VERSION_V0_OUT/MC_CMD_GET_VERSION_OUT_FIRMWARE */
#define       MC_CMD_GET_VERSION_OUT_PCOL_OFST 4
/* 128bit mask of functions supported by the current firmware */
#define       MC_CMD_GET_VERSION_OUT_SUPPORTED_FUNCS_OFST 8
#define       MC_CMD_GET_VERSION_OUT_SUPPORTED_FUNCS_LEN 16
#define       MC_CMD_GET_VERSION_OUT_VERSION_OFST 24
#define       MC_CMD_GET_VERSION_OUT_VERSION_LEN 8
#define       MC_CMD_GET_VERSION_OUT_VERSION_LO_OFST 24
#define       MC_CMD_GET_VERSION_OUT_VERSION_HI_OFST 28

/* MC_CMD_GET_VERSION_EXT_OUT msgresponse */
#define    MC_CMD_GET_VERSION_EXT_OUT_LEN 48
/*            MC_CMD_GET_VERSION_OUT_FIRMWARE_OFST 0 */
/*            Enum values, see field(s): */
/*               MC_CMD_GET_VERSION_V0_OUT/MC_CMD_GET_VERSION_OUT_FIRMWARE */
#define       MC_CMD_GET_VERSION_EXT_OUT_PCOL_OFST 4
/* 128bit mask of functions supported by the current firmware */
#define       MC_CMD_GET_VERSION_EXT_OUT_SUPPORTED_FUNCS_OFST 8
#define       MC_CMD_GET_VERSION_EXT_OUT_SUPPORTED_FUNCS_LEN 16
#define       MC_CMD_GET_VERSION_EXT_OUT_VERSION_OFST 24
#define       MC_CMD_GET_VERSION_EXT_OUT_VERSION_LEN 8
#define       MC_CMD_GET_VERSION_EXT_OUT_VERSION_LO_OFST 24
#define       MC_CMD_GET_VERSION_EXT_OUT_VERSION_HI_OFST 28
/* extra info */
#define       MC_CMD_GET_VERSION_EXT_OUT_EXTRA_OFST 32
#define       MC_CMD_GET_VERSION_EXT_OUT_EXTRA_LEN 16


/***********************************/
/* MC_CMD_PTP
 * Perform PTP operation
 */
#define MC_CMD_PTP 0xb

/* MC_CMD_PTP_IN msgrequest */
#define    MC_CMD_PTP_IN_LEN 1
/* PTP operation code */
#define       MC_CMD_PTP_IN_OP_OFST 0
#define       MC_CMD_PTP_IN_OP_LEN 1
/* enum: Enable PTP packet timestamping operation. */
#define          MC_CMD_PTP_OP_ENABLE 0x1
/* enum: Disable PTP packet timestamping operation. */
#define          MC_CMD_PTP_OP_DISABLE 0x2
/* enum: Send a PTP packet. */
#define          MC_CMD_PTP_OP_TRANSMIT 0x3
/* enum: Read the current NIC time. */
#define          MC_CMD_PTP_OP_READ_NIC_TIME 0x4
/* enum: Get the current PTP status. */
#define          MC_CMD_PTP_OP_STATUS 0x5
/* enum: Adjust the PTP NIC's time. */
#define          MC_CMD_PTP_OP_ADJUST 0x6
/* enum: Synchronize host and NIC time. */
#define          MC_CMD_PTP_OP_SYNCHRONIZE 0x7
/* enum: Basic manufacturing tests. */
#define          MC_CMD_PTP_OP_MANFTEST_BASIC 0x8
/* enum: Packet based manufacturing tests. */
#define          MC_CMD_PTP_OP_MANFTEST_PACKET 0x9
/* enum: Reset some of the PTP related statistics */
#define          MC_CMD_PTP_OP_RESET_STATS 0xa
/* enum: Debug operations to MC. */
#define          MC_CMD_PTP_OP_DEBUG 0xb
/* enum: Read an FPGA register */
#define          MC_CMD_PTP_OP_FPGAREAD 0xc
/* enum: Write an FPGA register */
#define          MC_CMD_PTP_OP_FPGAWRITE 0xd
/* enum: Apply an offset to the NIC clock */
#define          MC_CMD_PTP_OP_CLOCK_OFFSET_ADJUST 0xe
/* enum: Change Apply an offset to the NIC clock */
#define          MC_CMD_PTP_OP_CLOCK_FREQ_ADJUST 0xf
/* enum: Set the MC packet filter VLAN tags for received PTP packets */
#define          MC_CMD_PTP_OP_RX_SET_VLAN_FILTER 0x10
/* enum: Set the MC packet filter UUID for received PTP packets */
#define          MC_CMD_PTP_OP_RX_SET_UUID_FILTER 0x11
/* enum: Set the MC packet filter Domain for received PTP packets */
#define          MC_CMD_PTP_OP_RX_SET_DOMAIN_FILTER 0x12
/* enum: Set the clock source */
#define          MC_CMD_PTP_OP_SET_CLK_SRC 0x13
/* enum: Reset value of Timer Reg. */
#define          MC_CMD_PTP_OP_RST_CLK 0x14
/* enum: Enable the forwarding of PPS events to the host */
#define          MC_CMD_PTP_OP_PPS_ENABLE 0x15
/* enum: Above this for future use. */
#define          MC_CMD_PTP_OP_MAX 0x16

/* MC_CMD_PTP_IN_ENABLE msgrequest */
#define    MC_CMD_PTP_IN_ENABLE_LEN 16
#define       MC_CMD_PTP_IN_CMD_OFST 0
#define       MC_CMD_PTP_IN_PERIPH_ID_OFST 4
/* Event queue for PTP events */
#define       MC_CMD_PTP_IN_ENABLE_QUEUE_OFST 8
/* PTP timestamping mode */
#define       MC_CMD_PTP_IN_ENABLE_MODE_OFST 12
/* enum: PTP, version 1 */
#define          MC_CMD_PTP_MODE_V1 0x0
/* enum: PTP, version 1, with VLAN headers - deprecated */
#define          MC_CMD_PTP_MODE_V1_VLAN 0x1
/* enum: PTP, version 2 */
#define          MC_CMD_PTP_MODE_V2 0x2
/* enum: PTP, version 2, with VLAN headers - deprecated */
#define          MC_CMD_PTP_MODE_V2_VLAN 0x3
/* enum: PTP, version 2, with improved UUID filtering */
#define          MC_CMD_PTP_MODE_V2_ENHANCED 0x4
/* enum: FCoE (seconds and microseconds) */
#define          MC_CMD_PTP_MODE_FCOE 0x5

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
/* Transmit packet length */
#define       MC_CMD_PTP_IN_TRANSMIT_LENGTH_OFST 8
/* Transmit packet data */
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
/* Frequency adjustment 40 bit fixed point ns */
#define       MC_CMD_PTP_IN_ADJUST_FREQ_OFST 8
#define       MC_CMD_PTP_IN_ADJUST_FREQ_LEN 8
#define       MC_CMD_PTP_IN_ADJUST_FREQ_LO_OFST 8
#define       MC_CMD_PTP_IN_ADJUST_FREQ_HI_OFST 12
/* enum: Number of fractional bits in frequency adjustment */
#define          MC_CMD_PTP_IN_ADJUST_BITS 0x28
/* Time adjustment in seconds */
#define       MC_CMD_PTP_IN_ADJUST_SECONDS_OFST 16
/* Time adjustment in nanoseconds */
#define       MC_CMD_PTP_IN_ADJUST_NANOSECONDS_OFST 20

/* MC_CMD_PTP_IN_SYNCHRONIZE msgrequest */
#define    MC_CMD_PTP_IN_SYNCHRONIZE_LEN 20
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* Number of time readings to capture */
#define       MC_CMD_PTP_IN_SYNCHRONIZE_NUMTIMESETS_OFST 8
/* Host address in which to write "synchronization started" indication (64
 * bits)
 */
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
/* Enable or disable packet testing */
#define       MC_CMD_PTP_IN_MANFTEST_PACKET_TEST_ENABLE_OFST 8

/* MC_CMD_PTP_IN_RESET_STATS msgrequest */
#define    MC_CMD_PTP_IN_RESET_STATS_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/* Reset PTP statistics */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_DEBUG msgrequest */
#define    MC_CMD_PTP_IN_DEBUG_LEN 12
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* Debug operations */
#define       MC_CMD_PTP_IN_DEBUG_DEBUG_PARAM_OFST 8

/* MC_CMD_PTP_IN_FPGAREAD msgrequest */
#define    MC_CMD_PTP_IN_FPGAREAD_LEN 16
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
#define       MC_CMD_PTP_IN_FPGAREAD_ADDR_OFST 8
#define       MC_CMD_PTP_IN_FPGAREAD_NUMBYTES_OFST 12

/* MC_CMD_PTP_IN_FPGAWRITE msgrequest */
#define    MC_CMD_PTP_IN_FPGAWRITE_LENMIN 13
#define    MC_CMD_PTP_IN_FPGAWRITE_LENMAX 252
#define    MC_CMD_PTP_IN_FPGAWRITE_LEN(num) (12+1*(num))
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
#define       MC_CMD_PTP_IN_FPGAWRITE_ADDR_OFST 8
#define       MC_CMD_PTP_IN_FPGAWRITE_BUFFER_OFST 12
#define       MC_CMD_PTP_IN_FPGAWRITE_BUFFER_LEN 1
#define       MC_CMD_PTP_IN_FPGAWRITE_BUFFER_MINNUM 1
#define       MC_CMD_PTP_IN_FPGAWRITE_BUFFER_MAXNUM 240

/* MC_CMD_PTP_IN_CLOCK_OFFSET_ADJUST msgrequest */
#define    MC_CMD_PTP_IN_CLOCK_OFFSET_ADJUST_LEN 16
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* Time adjustment in seconds */
#define       MC_CMD_PTP_IN_CLOCK_OFFSET_ADJUST_SECONDS_OFST 8
/* Time adjustment in nanoseconds */
#define       MC_CMD_PTP_IN_CLOCK_OFFSET_ADJUST_NANOSECONDS_OFST 12

/* MC_CMD_PTP_IN_CLOCK_FREQ_ADJUST msgrequest */
#define    MC_CMD_PTP_IN_CLOCK_FREQ_ADJUST_LEN 16
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* Frequency adjustment 40 bit fixed point ns */
#define       MC_CMD_PTP_IN_CLOCK_FREQ_ADJUST_FREQ_OFST 8
#define       MC_CMD_PTP_IN_CLOCK_FREQ_ADJUST_FREQ_LEN 8
#define       MC_CMD_PTP_IN_CLOCK_FREQ_ADJUST_FREQ_LO_OFST 8
#define       MC_CMD_PTP_IN_CLOCK_FREQ_ADJUST_FREQ_HI_OFST 12
/* enum: Number of fractional bits in frequency adjustment */
/*               MC_CMD_PTP_IN_ADJUST_BITS 0x28 */

/* MC_CMD_PTP_IN_RX_SET_VLAN_FILTER msgrequest */
#define    MC_CMD_PTP_IN_RX_SET_VLAN_FILTER_LEN 24
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* Number of VLAN tags, 0 if not VLAN */
#define       MC_CMD_PTP_IN_RX_SET_VLAN_FILTER_NUM_VLAN_TAGS_OFST 8
/* Set of VLAN tags to filter against */
#define       MC_CMD_PTP_IN_RX_SET_VLAN_FILTER_VLAN_TAG_OFST 12
#define       MC_CMD_PTP_IN_RX_SET_VLAN_FILTER_VLAN_TAG_LEN 4
#define       MC_CMD_PTP_IN_RX_SET_VLAN_FILTER_VLAN_TAG_NUM 3

/* MC_CMD_PTP_IN_RX_SET_UUID_FILTER msgrequest */
#define    MC_CMD_PTP_IN_RX_SET_UUID_FILTER_LEN 20
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* 1 to enable UUID filtering, 0 to disable */
#define       MC_CMD_PTP_IN_RX_SET_UUID_FILTER_ENABLE_OFST 8
/* UUID to filter against */
#define       MC_CMD_PTP_IN_RX_SET_UUID_FILTER_UUID_OFST 12
#define       MC_CMD_PTP_IN_RX_SET_UUID_FILTER_UUID_LEN 8
#define       MC_CMD_PTP_IN_RX_SET_UUID_FILTER_UUID_LO_OFST 12
#define       MC_CMD_PTP_IN_RX_SET_UUID_FILTER_UUID_HI_OFST 16

/* MC_CMD_PTP_IN_RX_SET_DOMAIN_FILTER msgrequest */
#define    MC_CMD_PTP_IN_RX_SET_DOMAIN_FILTER_LEN 16
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* 1 to enable Domain filtering, 0 to disable */
#define       MC_CMD_PTP_IN_RX_SET_DOMAIN_FILTER_ENABLE_OFST 8
/* Domain number to filter against */
#define       MC_CMD_PTP_IN_RX_SET_DOMAIN_FILTER_DOMAIN_OFST 12

/* MC_CMD_PTP_IN_SET_CLK_SRC msgrequest */
#define    MC_CMD_PTP_IN_SET_CLK_SRC_LEN 12
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* Set the clock source. */
#define       MC_CMD_PTP_IN_SET_CLK_SRC_CLK_OFST 8
/* enum: Internal. */
#define          MC_CMD_PTP_CLK_SRC_INTERNAL 0x0
/* enum: External. */
#define          MC_CMD_PTP_CLK_SRC_EXTERNAL 0x1

/* MC_CMD_PTP_IN_RST_CLK msgrequest */
#define    MC_CMD_PTP_IN_RST_CLK_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/* Reset value of Timer Reg. */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_PPS_ENABLE msgrequest */
#define    MC_CMD_PTP_IN_PPS_ENABLE_LEN 12
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/* Enable or disable */
#define       MC_CMD_PTP_IN_PPS_ENABLE_OP_OFST 4
/* enum: Enable */
#define          MC_CMD_PTP_ENABLE_PPS 0x0
/* enum: Disable */
#define          MC_CMD_PTP_DISABLE_PPS 0x1
/* Queueid to send events back */
#define       MC_CMD_PTP_IN_PPS_ENABLE_QUEUE_ID_OFST 8

/* MC_CMD_PTP_OUT msgresponse */
#define    MC_CMD_PTP_OUT_LEN 0

/* MC_CMD_PTP_OUT_TRANSMIT msgresponse */
#define    MC_CMD_PTP_OUT_TRANSMIT_LEN 8
/* Value of seconds timestamp */
#define       MC_CMD_PTP_OUT_TRANSMIT_SECONDS_OFST 0
/* Value of nanoseconds timestamp */
#define       MC_CMD_PTP_OUT_TRANSMIT_NANOSECONDS_OFST 4

/* MC_CMD_PTP_OUT_READ_NIC_TIME msgresponse */
#define    MC_CMD_PTP_OUT_READ_NIC_TIME_LEN 8
/* Value of seconds timestamp */
#define       MC_CMD_PTP_OUT_READ_NIC_TIME_SECONDS_OFST 0
/* Value of nanoseconds timestamp */
#define       MC_CMD_PTP_OUT_READ_NIC_TIME_NANOSECONDS_OFST 4

/* MC_CMD_PTP_OUT_STATUS msgresponse */
#define    MC_CMD_PTP_OUT_STATUS_LEN 64
/* Frequency of NIC's hardware clock */
#define       MC_CMD_PTP_OUT_STATUS_CLOCK_FREQ_OFST 0
/* Number of packets transmitted and timestamped */
#define       MC_CMD_PTP_OUT_STATUS_STATS_TX_OFST 4
/* Number of packets received and timestamped */
#define       MC_CMD_PTP_OUT_STATUS_STATS_RX_OFST 8
/* Number of packets timestamped by the FPGA */
#define       MC_CMD_PTP_OUT_STATUS_STATS_TS_OFST 12
/* Number of packets filter matched */
#define       MC_CMD_PTP_OUT_STATUS_STATS_FM_OFST 16
/* Number of packets not filter matched */
#define       MC_CMD_PTP_OUT_STATUS_STATS_NFM_OFST 20
/* Number of PPS overflows (noise on input?) */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFLOW_OFST 24
/* Number of PPS bad periods */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_BAD_OFST 28
/* Minimum period of PPS pulse */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MIN_OFST 32
/* Maximum period of PPS pulse */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MAX_OFST 36
/* Last period of PPS pulse */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_LAST_OFST 40
/* Mean period of PPS pulse */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MEAN_OFST 44
/* Minimum offset of PPS pulse (signed) */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_MIN_OFST 48
/* Maximum offset of PPS pulse (signed) */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_MAX_OFST 52
/* Last offset of PPS pulse (signed) */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_LAST_OFST 56
/* Mean offset of PPS pulse (signed) */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_MEAN_OFST 60

/* MC_CMD_PTP_OUT_SYNCHRONIZE msgresponse */
#define    MC_CMD_PTP_OUT_SYNCHRONIZE_LENMIN 20
#define    MC_CMD_PTP_OUT_SYNCHRONIZE_LENMAX 240
#define    MC_CMD_PTP_OUT_SYNCHRONIZE_LEN(num) (0+20*(num))
/* A set of host and NIC times */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_OFST 0
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_LEN 20
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_MINNUM 1
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_MAXNUM 12
/* Host time immediately before NIC's hardware clock read */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_HOSTSTART_OFST 0
/* Value of seconds timestamp */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_SECONDS_OFST 4
/* Value of nanoseconds timestamp */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_NANOSECONDS_OFST 8
/* Host time immediately after NIC's hardware clock read */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_HOSTEND_OFST 12
/* Number of nanoseconds waited after reading NIC's hardware clock */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_WAITNS_OFST 16

/* MC_CMD_PTP_OUT_MANFTEST_BASIC msgresponse */
#define    MC_CMD_PTP_OUT_MANFTEST_BASIC_LEN 8
/* Results of testing */
#define       MC_CMD_PTP_OUT_MANFTEST_BASIC_TEST_RESULT_OFST 0
/* enum: Successful test */
#define          MC_CMD_PTP_MANF_SUCCESS 0x0
/* enum: FPGA load failed */
#define          MC_CMD_PTP_MANF_FPGA_LOAD 0x1
/* enum: FPGA version invalid */
#define          MC_CMD_PTP_MANF_FPGA_VERSION 0x2
/* enum: FPGA registers incorrect */
#define          MC_CMD_PTP_MANF_FPGA_REGISTERS 0x3
/* enum: Oscillator possibly not working? */
#define          MC_CMD_PTP_MANF_OSCILLATOR 0x4
/* enum: Timestamps not increasing */
#define          MC_CMD_PTP_MANF_TIMESTAMPS 0x5
/* enum: Mismatched packet count */
#define          MC_CMD_PTP_MANF_PACKET_COUNT 0x6
/* enum: Mismatched packet count (Siena filter and FPGA) */
#define          MC_CMD_PTP_MANF_FILTER_COUNT 0x7
/* enum: Not enough packets to perform timestamp check */
#define          MC_CMD_PTP_MANF_PACKET_ENOUGH 0x8
/* enum: Timestamp trigger GPIO not working */
#define          MC_CMD_PTP_MANF_GPIO_TRIGGER 0x9
/* Presence of external oscillator */
#define       MC_CMD_PTP_OUT_MANFTEST_BASIC_TEST_EXTOSC_OFST 4

/* MC_CMD_PTP_OUT_MANFTEST_PACKET msgresponse */
#define    MC_CMD_PTP_OUT_MANFTEST_PACKET_LEN 12
/* Results of testing */
#define       MC_CMD_PTP_OUT_MANFTEST_PACKET_TEST_RESULT_OFST 0
/* Number of packets received by FPGA */
#define       MC_CMD_PTP_OUT_MANFTEST_PACKET_TEST_FPGACOUNT_OFST 4
/* Number of packets received by Siena filters */
#define       MC_CMD_PTP_OUT_MANFTEST_PACKET_TEST_FILTERCOUNT_OFST 8

/* MC_CMD_PTP_OUT_FPGAREAD msgresponse */
#define    MC_CMD_PTP_OUT_FPGAREAD_LENMIN 1
#define    MC_CMD_PTP_OUT_FPGAREAD_LENMAX 252
#define    MC_CMD_PTP_OUT_FPGAREAD_LEN(num) (0+1*(num))
#define       MC_CMD_PTP_OUT_FPGAREAD_BUFFER_OFST 0
#define       MC_CMD_PTP_OUT_FPGAREAD_BUFFER_LEN 1
#define       MC_CMD_PTP_OUT_FPGAREAD_BUFFER_MINNUM 1
#define       MC_CMD_PTP_OUT_FPGAREAD_BUFFER_MAXNUM 252


/***********************************/
/* MC_CMD_CSR_READ32
 * Read 32bit words from the indirect memory map.
 */
#define MC_CMD_CSR_READ32 0xc

/* MC_CMD_CSR_READ32_IN msgrequest */
#define    MC_CMD_CSR_READ32_IN_LEN 12
/* Address */
#define       MC_CMD_CSR_READ32_IN_ADDR_OFST 0
#define       MC_CMD_CSR_READ32_IN_STEP_OFST 4
#define       MC_CMD_CSR_READ32_IN_NUMWORDS_OFST 8

/* MC_CMD_CSR_READ32_OUT msgresponse */
#define    MC_CMD_CSR_READ32_OUT_LENMIN 4
#define    MC_CMD_CSR_READ32_OUT_LENMAX 252
#define    MC_CMD_CSR_READ32_OUT_LEN(num) (0+4*(num))
/* The last dword is the status, not a value read */
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
/* Address */
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
/* MC_CMD_HP
 * These commands are used for HP related features. They are grouped under one
 * MCDI command to avoid creating too many MCDI commands.
 */
#define MC_CMD_HP 0x54

/* MC_CMD_HP_IN msgrequest */
#define    MC_CMD_HP_IN_LEN 16
/* HP OCSD sub-command. When address is not NULL, request activation of OCSD at
 * the specified address with the specified interval.When address is NULL,
 * INTERVAL is interpreted as a command: 0: stop OCSD / 1: Report OCSD current
 * state / 2: (debug) Show temperature reported by one of the supported
 * sensors.
 */
#define       MC_CMD_HP_IN_SUBCMD_OFST 0
/* enum: OCSD (Option Card Sensor Data) sub-command. */
#define          MC_CMD_HP_IN_OCSD_SUBCMD 0x0
/* enum: Last known valid HP sub-command. */
#define          MC_CMD_HP_IN_LAST_SUBCMD 0x0
/* The address to the array of sensor fields. (Or NULL to use a sub-command.)
 */
#define       MC_CMD_HP_IN_OCSD_ADDR_OFST 4
#define       MC_CMD_HP_IN_OCSD_ADDR_LEN 8
#define       MC_CMD_HP_IN_OCSD_ADDR_LO_OFST 4
#define       MC_CMD_HP_IN_OCSD_ADDR_HI_OFST 8
/* The requested update interval, in seconds. (Or the sub-command if ADDR is
 * NULL.)
 */
#define       MC_CMD_HP_IN_OCSD_INTERVAL_OFST 12

/* MC_CMD_HP_OUT msgresponse */
#define    MC_CMD_HP_OUT_LEN 4
#define       MC_CMD_HP_OUT_OCSD_STATUS_OFST 0
/* enum: OCSD stopped for this card. */
#define          MC_CMD_HP_OUT_OCSD_STOPPED 0x1
/* enum: OCSD was successfully started with the address provided. */
#define          MC_CMD_HP_OUT_OCSD_STARTED 0x2
/* enum: OCSD was already started for this card. */
#define          MC_CMD_HP_OUT_OCSD_ALREADY_STARTED 0x3


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
/* (thread ptr, stack size, free space) for each thread in system */
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
/* Bus number; there are two MDIO buses: one for the internal PHY, and one for
 * external devices.
 */
#define       MC_CMD_MDIO_READ_IN_BUS_OFST 0
/* enum: Internal. */
#define          MC_CMD_MDIO_BUS_INTERNAL 0x0
/* enum: External. */
#define          MC_CMD_MDIO_BUS_EXTERNAL 0x1
/* Port address */
#define       MC_CMD_MDIO_READ_IN_PRTAD_OFST 4
/* Device Address or clause 22. */
#define       MC_CMD_MDIO_READ_IN_DEVAD_OFST 8
/* enum: By default all the MCDI MDIO operations perform clause45 mode. If you
 * want to use clause22 then set DEVAD = MC_CMD_MDIO_CLAUSE22.
 */
#define          MC_CMD_MDIO_CLAUSE22 0x20
/* Address */
#define       MC_CMD_MDIO_READ_IN_ADDR_OFST 12

/* MC_CMD_MDIO_READ_OUT msgresponse */
#define    MC_CMD_MDIO_READ_OUT_LEN 8
/* Value */
#define       MC_CMD_MDIO_READ_OUT_VALUE_OFST 0
/* Status the MDIO commands return the raw status bits from the MDIO block. A
 * "good" transaction should have the DONE bit set and all other bits clear.
 */
#define       MC_CMD_MDIO_READ_OUT_STATUS_OFST 4
/* enum: Good. */
#define          MC_CMD_MDIO_STATUS_GOOD 0x8


/***********************************/
/* MC_CMD_MDIO_WRITE
 * MDIO register write.
 */
#define MC_CMD_MDIO_WRITE 0x11

/* MC_CMD_MDIO_WRITE_IN msgrequest */
#define    MC_CMD_MDIO_WRITE_IN_LEN 20
/* Bus number; there are two MDIO buses: one for the internal PHY, and one for
 * external devices.
 */
#define       MC_CMD_MDIO_WRITE_IN_BUS_OFST 0
/* enum: Internal. */
/*               MC_CMD_MDIO_BUS_INTERNAL 0x0 */
/* enum: External. */
/*               MC_CMD_MDIO_BUS_EXTERNAL 0x1 */
/* Port address */
#define       MC_CMD_MDIO_WRITE_IN_PRTAD_OFST 4
/* Device Address or clause 22. */
#define       MC_CMD_MDIO_WRITE_IN_DEVAD_OFST 8
/* enum: By default all the MCDI MDIO operations perform clause45 mode. If you
 * want to use clause22 then set DEVAD = MC_CMD_MDIO_CLAUSE22.
 */
/*               MC_CMD_MDIO_CLAUSE22 0x20 */
/* Address */
#define       MC_CMD_MDIO_WRITE_IN_ADDR_OFST 12
/* Value */
#define       MC_CMD_MDIO_WRITE_IN_VALUE_OFST 16

/* MC_CMD_MDIO_WRITE_OUT msgresponse */
#define    MC_CMD_MDIO_WRITE_OUT_LEN 4
/* Status; the MDIO commands return the raw status bits from the MDIO block. A
 * "good" transaction should have the DONE bit set and all other bits clear.
 */
#define       MC_CMD_MDIO_WRITE_OUT_STATUS_OFST 0
/* enum: Good. */
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
/* Each write op consists of an address (offset 0), byte enable/VF/CS2 (offset
 * 32) and value (offset 64). See MC_CMD_DBIWROP_TYPEDEF.
 */
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
#define       MC_CMD_DBIWROP_TYPEDEF_PARMS_OFST 4
#define        MC_CMD_DBIWROP_TYPEDEF_VF_NUM_LBN 16
#define        MC_CMD_DBIWROP_TYPEDEF_VF_NUM_WIDTH 16
#define        MC_CMD_DBIWROP_TYPEDEF_VF_ACTIVE_LBN 15
#define        MC_CMD_DBIWROP_TYPEDEF_VF_ACTIVE_WIDTH 1
#define        MC_CMD_DBIWROP_TYPEDEF_CS2_LBN 14
#define        MC_CMD_DBIWROP_TYPEDEF_CS2_WIDTH 1
#define       MC_CMD_DBIWROP_TYPEDEF_PARMS_LBN 32
#define       MC_CMD_DBIWROP_TYPEDEF_PARMS_WIDTH 32
#define       MC_CMD_DBIWROP_TYPEDEF_VALUE_OFST 8
#define       MC_CMD_DBIWROP_TYPEDEF_VALUE_LBN 64
#define       MC_CMD_DBIWROP_TYPEDEF_VALUE_WIDTH 32


/***********************************/
/* MC_CMD_PORT_READ32
 * Read a 32-bit register from the indirect port register map. The port to
 * access is implied by the Shared memory channel used.
 */
#define MC_CMD_PORT_READ32 0x14

/* MC_CMD_PORT_READ32_IN msgrequest */
#define    MC_CMD_PORT_READ32_IN_LEN 4
/* Address */
#define       MC_CMD_PORT_READ32_IN_ADDR_OFST 0

/* MC_CMD_PORT_READ32_OUT msgresponse */
#define    MC_CMD_PORT_READ32_OUT_LEN 8
/* Value */
#define       MC_CMD_PORT_READ32_OUT_VALUE_OFST 0
/* Status */
#define       MC_CMD_PORT_READ32_OUT_STATUS_OFST 4


/***********************************/
/* MC_CMD_PORT_WRITE32
 * Write a 32-bit register to the indirect port register map. The port to
 * access is implied by the Shared memory channel used.
 */
#define MC_CMD_PORT_WRITE32 0x15

/* MC_CMD_PORT_WRITE32_IN msgrequest */
#define    MC_CMD_PORT_WRITE32_IN_LEN 8
/* Address */
#define       MC_CMD_PORT_WRITE32_IN_ADDR_OFST 0
/* Value */
#define       MC_CMD_PORT_WRITE32_IN_VALUE_OFST 4

/* MC_CMD_PORT_WRITE32_OUT msgresponse */
#define    MC_CMD_PORT_WRITE32_OUT_LEN 4
/* Status */
#define       MC_CMD_PORT_WRITE32_OUT_STATUS_OFST 0


/***********************************/
/* MC_CMD_PORT_READ128
 * Read a 128-bit register from the indirect port register map. The port to
 * access is implied by the Shared memory channel used.
 */
#define MC_CMD_PORT_READ128 0x16

/* MC_CMD_PORT_READ128_IN msgrequest */
#define    MC_CMD_PORT_READ128_IN_LEN 4
/* Address */
#define       MC_CMD_PORT_READ128_IN_ADDR_OFST 0

/* MC_CMD_PORT_READ128_OUT msgresponse */
#define    MC_CMD_PORT_READ128_OUT_LEN 20
/* Value */
#define       MC_CMD_PORT_READ128_OUT_VALUE_OFST 0
#define       MC_CMD_PORT_READ128_OUT_VALUE_LEN 16
/* Status */
#define       MC_CMD_PORT_READ128_OUT_STATUS_OFST 16


/***********************************/
/* MC_CMD_PORT_WRITE128
 * Write a 128-bit register to the indirect port register map. The port to
 * access is implied by the Shared memory channel used.
 */
#define MC_CMD_PORT_WRITE128 0x17

/* MC_CMD_PORT_WRITE128_IN msgrequest */
#define    MC_CMD_PORT_WRITE128_IN_LEN 20
/* Address */
#define       MC_CMD_PORT_WRITE128_IN_ADDR_OFST 0
/* Value */
#define       MC_CMD_PORT_WRITE128_IN_VALUE_OFST 4
#define       MC_CMD_PORT_WRITE128_IN_VALUE_LEN 16

/* MC_CMD_PORT_WRITE128_OUT msgresponse */
#define    MC_CMD_PORT_WRITE128_OUT_LEN 4
/* Status */
#define       MC_CMD_PORT_WRITE128_OUT_STATUS_OFST 0

/* MC_CMD_CAPABILITIES structuredef */
#define    MC_CMD_CAPABILITIES_LEN 4
/* Small buf table. */
#define       MC_CMD_CAPABILITIES_SMALL_BUF_TBL_LBN 0
#define       MC_CMD_CAPABILITIES_SMALL_BUF_TBL_WIDTH 1
/* Turbo mode (for Maranello). */
#define       MC_CMD_CAPABILITIES_TURBO_LBN 1
#define       MC_CMD_CAPABILITIES_TURBO_WIDTH 1
/* Turbo mode active (for Maranello). */
#define       MC_CMD_CAPABILITIES_TURBO_ACTIVE_LBN 2
#define       MC_CMD_CAPABILITIES_TURBO_ACTIVE_WIDTH 1
/* PTP offload. */
#define       MC_CMD_CAPABILITIES_PTP_LBN 3
#define       MC_CMD_CAPABILITIES_PTP_WIDTH 1
/* AOE mode. */
#define       MC_CMD_CAPABILITIES_AOE_LBN 4
#define       MC_CMD_CAPABILITIES_AOE_WIDTH 1
/* AOE mode active. */
#define       MC_CMD_CAPABILITIES_AOE_ACTIVE_LBN 5
#define       MC_CMD_CAPABILITIES_AOE_ACTIVE_WIDTH 1
/* AOE mode active. */
#define       MC_CMD_CAPABILITIES_FC_ACTIVE_LBN 6
#define       MC_CMD_CAPABILITIES_FC_ACTIVE_WIDTH 1
#define       MC_CMD_CAPABILITIES_RESERVED_LBN 7
#define       MC_CMD_CAPABILITIES_RESERVED_WIDTH 25


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
/* See MC_CMD_CAPABILITIES */
#define       MC_CMD_GET_BOARD_CFG_OUT_CAPABILITIES_PORT0_OFST 36
/* See MC_CMD_CAPABILITIES */
#define       MC_CMD_GET_BOARD_CFG_OUT_CAPABILITIES_PORT1_OFST 40
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT0_OFST 44
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT0_LEN 6
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT1_OFST 50
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_ADDR_BASE_PORT1_LEN 6
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_COUNT_PORT0_OFST 56
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_COUNT_PORT1_OFST 60
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_STRIDE_PORT0_OFST 64
#define       MC_CMD_GET_BOARD_CFG_OUT_MAC_STRIDE_PORT1_OFST 68
/* This field contains a 16-bit value for each of the types of NVRAM area. The
 * values are defined in the firmware/mc/platform/.c file for a specific board
 * type, but otherwise have no meaning to the MC; they are used by the driver
 * to manage selection of appropriate firmware updates.
 */
#define       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_OFST 72
#define       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_LEN 2
#define       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_MINNUM 12
#define       MC_CMD_GET_BOARD_CFG_OUT_FW_SUBTYPE_LIST_MAXNUM 32


/***********************************/
/* MC_CMD_DBI_READX
 * Read DBI register(s) -- extended functionality
 */
#define MC_CMD_DBI_READX 0x19

/* MC_CMD_DBI_READX_IN msgrequest */
#define    MC_CMD_DBI_READX_IN_LENMIN 8
#define    MC_CMD_DBI_READX_IN_LENMAX 248
#define    MC_CMD_DBI_READX_IN_LEN(num) (0+8*(num))
/* Each Read op consists of an address (offset 0), VF/CS2) */
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
/* Value */
#define       MC_CMD_DBI_READX_OUT_VALUE_OFST 0
#define       MC_CMD_DBI_READX_OUT_VALUE_LEN 4
#define       MC_CMD_DBI_READX_OUT_VALUE_MINNUM 1
#define       MC_CMD_DBI_READX_OUT_VALUE_MAXNUM 63

/* MC_CMD_DBIRDOP_TYPEDEF structuredef */
#define    MC_CMD_DBIRDOP_TYPEDEF_LEN 8
#define       MC_CMD_DBIRDOP_TYPEDEF_ADDRESS_OFST 0
#define       MC_CMD_DBIRDOP_TYPEDEF_ADDRESS_LBN 0
#define       MC_CMD_DBIRDOP_TYPEDEF_ADDRESS_WIDTH 32
#define       MC_CMD_DBIRDOP_TYPEDEF_PARMS_OFST 4
#define        MC_CMD_DBIRDOP_TYPEDEF_VF_NUM_LBN 16
#define        MC_CMD_DBIRDOP_TYPEDEF_VF_NUM_WIDTH 16
#define        MC_CMD_DBIRDOP_TYPEDEF_VF_ACTIVE_LBN 15
#define        MC_CMD_DBIRDOP_TYPEDEF_VF_ACTIVE_WIDTH 1
#define        MC_CMD_DBIRDOP_TYPEDEF_CS2_LBN 14
#define        MC_CMD_DBIRDOP_TYPEDEF_CS2_WIDTH 1
#define       MC_CMD_DBIRDOP_TYPEDEF_PARMS_LBN 32
#define       MC_CMD_DBIRDOP_TYPEDEF_PARMS_WIDTH 32


/***********************************/
/* MC_CMD_SET_RAND_SEED
 * Set the 16byte seed for the MC pseudo-random generator.
 */
#define MC_CMD_SET_RAND_SEED 0x1a

/* MC_CMD_SET_RAND_SEED_IN msgrequest */
#define    MC_CMD_SET_RAND_SEED_IN_LEN 16
/* Seed value. */
#define       MC_CMD_SET_RAND_SEED_IN_SEED_OFST 0
#define       MC_CMD_SET_RAND_SEED_IN_SEED_LEN 16

/* MC_CMD_SET_RAND_SEED_OUT msgresponse */
#define    MC_CMD_SET_RAND_SEED_OUT_LEN 0


/***********************************/
/* MC_CMD_LTSSM_HIST
 * Retrieve the history of the LTSSM, if the build supports it.
 */
#define MC_CMD_LTSSM_HIST 0x1b

/* MC_CMD_LTSSM_HIST_IN msgrequest */
#define    MC_CMD_LTSSM_HIST_IN_LEN 0

/* MC_CMD_LTSSM_HIST_OUT msgresponse */
#define    MC_CMD_LTSSM_HIST_OUT_LENMIN 0
#define    MC_CMD_LTSSM_HIST_OUT_LENMAX 252
#define    MC_CMD_LTSSM_HIST_OUT_LEN(num) (0+4*(num))
/* variable number of LTSSM values, as bytes. The history is read-to-clear. */
#define       MC_CMD_LTSSM_HIST_OUT_DATA_OFST 0
#define       MC_CMD_LTSSM_HIST_OUT_DATA_LEN 4
#define       MC_CMD_LTSSM_HIST_OUT_DATA_MINNUM 0
#define       MC_CMD_LTSSM_HIST_OUT_DATA_MAXNUM 63


/***********************************/
/* MC_CMD_DRV_ATTACH
 * Inform MCPU that this port is managed on the host (i.e. driver active). For
 * Huntington, also request the preferred datapath firmware to use if possible
 * (it may not be possible for this request to be fulfilled; the driver must
 * issue a subsequent MC_CMD_GET_CAPABILITIES command to determine which
 * features are actually available). The FIRMWARE_ID field is ignored by older
 * platforms.
 */
#define MC_CMD_DRV_ATTACH 0x1c

/* MC_CMD_DRV_ATTACH_IN msgrequest */
#define    MC_CMD_DRV_ATTACH_IN_LEN 12
/* new state (0=detached, 1=attached) to set if UPDATE=1 */
#define       MC_CMD_DRV_ATTACH_IN_NEW_STATE_OFST 0
/* 1 to set new state, or 0 to just report the existing state */
#define       MC_CMD_DRV_ATTACH_IN_UPDATE_OFST 4
/* preferred datapath firmware (for Huntington; ignored for Siena) */
#define       MC_CMD_DRV_ATTACH_IN_FIRMWARE_ID_OFST 8
/* enum: Prefer to use full featured firmware */
#define          MC_CMD_FW_FULL_FEATURED 0x0
/* enum: Prefer to use firmware with fewer features but lower latency */
#define          MC_CMD_FW_LOW_LATENCY 0x1

/* MC_CMD_DRV_ATTACH_OUT msgresponse */
#define    MC_CMD_DRV_ATTACH_OUT_LEN 4
/* previous or existing state (0=detached, 1=attached) */
#define       MC_CMD_DRV_ATTACH_OUT_OLD_STATE_OFST 0

/* MC_CMD_DRV_ATTACH_EXT_OUT msgresponse */
#define    MC_CMD_DRV_ATTACH_EXT_OUT_LEN 8
/* previous or existing state (0=detached, 1=attached) */
#define       MC_CMD_DRV_ATTACH_EXT_OUT_OLD_STATE_OFST 0
/* Flags associated with this function */
#define       MC_CMD_DRV_ATTACH_EXT_OUT_FUNC_FLAGS_OFST 4
/* enum: Labels the lowest-numbered function visible to the OS */
#define          MC_CMD_DRV_ATTACH_EXT_OUT_FLAG_PRIMARY 0x0
/* enum: The function can control the link state of the physical port it is
 * bound to.
 */
#define          MC_CMD_DRV_ATTACH_EXT_OUT_FLAG_LINKCTRL 0x1
/* enum: The function can perform privileged operations */
#define          MC_CMD_DRV_ATTACH_EXT_OUT_FLAG_TRUSTED 0x2


/***********************************/
/* MC_CMD_SHMUART
 * Route UART output to circular buffer in shared memory instead.
 */
#define MC_CMD_SHMUART 0x1f

/* MC_CMD_SHMUART_IN msgrequest */
#define    MC_CMD_SHMUART_IN_LEN 4
/* ??? */
#define       MC_CMD_SHMUART_IN_FLAG_OFST 0

/* MC_CMD_SHMUART_OUT msgresponse */
#define    MC_CMD_SHMUART_OUT_LEN 0


/***********************************/
/* MC_CMD_PORT_RESET
 * Generic per-port reset. There is no equivalent for per-board reset. Locks
 * required: None; Return code: 0, ETIME. NOTE: This command is deprecated -
 * use MC_CMD_ENTITY_RESET instead.
 */
#define MC_CMD_PORT_RESET 0x20

/* MC_CMD_PORT_RESET_IN msgrequest */
#define    MC_CMD_PORT_RESET_IN_LEN 0

/* MC_CMD_PORT_RESET_OUT msgresponse */
#define    MC_CMD_PORT_RESET_OUT_LEN 0


/***********************************/
/* MC_CMD_ENTITY_RESET
 * Generic per-resource reset. There is no equivalent for per-board reset.
 * Locks required: None; Return code: 0, ETIME. NOTE: This command is an
 * extended version of the deprecated MC_CMD_PORT_RESET with added fields.
 */
#define MC_CMD_ENTITY_RESET 0x20

/* MC_CMD_ENTITY_RESET_IN msgrequest */
#define    MC_CMD_ENTITY_RESET_IN_LEN 4
/* Optional flags field. Omitting this will perform a "legacy" reset action
 * (TBD).
 */
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
/* poll period. 0 is disabled */
#define       MC_CMD_PCIE_CREDITS_IN_POLL_PERIOD_OFST 0
/* wipe statistics */
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
 * Copy the given ASCII string out onto UART and/or out of the network port.
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
 * Report PHY configuration. This guarantees to succeed even if the PHY is in a
 * 'zombie' state. Locks required: None
 */
#define MC_CMD_GET_PHY_CFG 0x24

/* MC_CMD_GET_PHY_CFG_IN msgrequest */
#define    MC_CMD_GET_PHY_CFG_IN_LEN 0

/* MC_CMD_GET_PHY_CFG_OUT msgresponse */
#define    MC_CMD_GET_PHY_CFG_OUT_LEN 72
/* flags */
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
/* ?? */
#define       MC_CMD_GET_PHY_CFG_OUT_TYPE_OFST 4
/* Bitmask of supported capabilities */
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
#define        MC_CMD_PHY_CAP_40000FDX_LBN 11
#define        MC_CMD_PHY_CAP_40000FDX_WIDTH 1
#define        MC_CMD_PHY_CAP_DDM_LBN 12
#define        MC_CMD_PHY_CAP_DDM_WIDTH 1
/* ?? */
#define       MC_CMD_GET_PHY_CFG_OUT_CHANNEL_OFST 12
/* ?? */
#define       MC_CMD_GET_PHY_CFG_OUT_PRT_OFST 16
/* ?? */
#define       MC_CMD_GET_PHY_CFG_OUT_STATS_MASK_OFST 20
/* ?? */
#define       MC_CMD_GET_PHY_CFG_OUT_NAME_OFST 24
#define       MC_CMD_GET_PHY_CFG_OUT_NAME_LEN 20
/* ?? */
#define       MC_CMD_GET_PHY_CFG_OUT_MEDIA_TYPE_OFST 44
/* enum: Xaui. */
#define          MC_CMD_MEDIA_XAUI 0x1
/* enum: CX4. */
#define          MC_CMD_MEDIA_CX4 0x2
/* enum: KX4. */
#define          MC_CMD_MEDIA_KX4 0x3
/* enum: XFP Far. */
#define          MC_CMD_MEDIA_XFP 0x4
/* enum: SFP+. */
#define          MC_CMD_MEDIA_SFP_PLUS 0x5
/* enum: 10GBaseT. */
#define          MC_CMD_MEDIA_BASE_T 0x6
#define       MC_CMD_GET_PHY_CFG_OUT_MMD_MASK_OFST 48
/* enum: Native clause 22 */
#define          MC_CMD_MMD_CLAUSE22 0x0
#define          MC_CMD_MMD_CLAUSE45_PMAPMD 0x1 /* enum */
#define          MC_CMD_MMD_CLAUSE45_WIS 0x2 /* enum */
#define          MC_CMD_MMD_CLAUSE45_PCS 0x3 /* enum */
#define          MC_CMD_MMD_CLAUSE45_PHYXS 0x4 /* enum */
#define          MC_CMD_MMD_CLAUSE45_DTEXS 0x5 /* enum */
#define          MC_CMD_MMD_CLAUSE45_TC 0x6 /* enum */
#define          MC_CMD_MMD_CLAUSE45_AN 0x7 /* enum */
/* enum: Clause22 proxied over clause45 by PHY. */
#define          MC_CMD_MMD_CLAUSE45_C22EXT 0x1d
#define          MC_CMD_MMD_CLAUSE45_VEND1 0x1e /* enum */
#define          MC_CMD_MMD_CLAUSE45_VEND2 0x1f /* enum */
#define       MC_CMD_GET_PHY_CFG_OUT_REVISION_OFST 52
#define       MC_CMD_GET_PHY_CFG_OUT_REVISION_LEN 20


/***********************************/
/* MC_CMD_START_BIST
 * Start a BIST test on the PHY. Locks required: PHY_LOCK if doing a PHY BIST
 * Return code: 0, EINVAL, EACCES (if PHY_LOCK is not held)
 */
#define MC_CMD_START_BIST 0x25

/* MC_CMD_START_BIST_IN msgrequest */
#define    MC_CMD_START_BIST_IN_LEN 4
/* Type of test. */
#define       MC_CMD_START_BIST_IN_TYPE_OFST 0
/* enum: Run the PHY's short cable BIST. */
#define          MC_CMD_PHY_BIST_CABLE_SHORT 0x1
/* enum: Run the PHY's long cable BIST. */
#define          MC_CMD_PHY_BIST_CABLE_LONG 0x2
/* enum: Run BIST on the currently selected BPX Serdes (XAUI or XFI) . */
#define          MC_CMD_BPX_SERDES_BIST 0x3
/* enum: Run the MC loopback tests. */
#define          MC_CMD_MC_LOOPBACK_BIST 0x4
/* enum: Run the PHY's standard BIST. */
#define          MC_CMD_PHY_BIST 0x5
/* enum: Run MC RAM test. */
#define          MC_CMD_MC_MEM_BIST 0x6
/* enum: Run Port RAM test. */
#define          MC_CMD_PORT_MEM_BIST 0x7
/* enum: Run register test. */
#define          MC_CMD_REG_BIST 0x8

/* MC_CMD_START_BIST_OUT msgresponse */
#define    MC_CMD_START_BIST_OUT_LEN 0


/***********************************/
/* MC_CMD_POLL_BIST
 * Poll for BIST completion. Returns a single status code, and optionally some
 * PHY specific bist output. The driver should only consume the BIST output
 * after validating OUTLEN and MC_CMD_GET_PHY_CFG.TYPE. If a driver can't
 * successfully parse the BIST output, it should still respect the pass/Fail in
 * OUT.RESULT. Locks required: PHY_LOCK if doing a PHY BIST. Return code: 0,
 * EACCES (if PHY_LOCK is not held).
 */
#define MC_CMD_POLL_BIST 0x26

/* MC_CMD_POLL_BIST_IN msgrequest */
#define    MC_CMD_POLL_BIST_IN_LEN 0

/* MC_CMD_POLL_BIST_OUT msgresponse */
#define    MC_CMD_POLL_BIST_OUT_LEN 8
/* result */
#define       MC_CMD_POLL_BIST_OUT_RESULT_OFST 0
/* enum: Running. */
#define          MC_CMD_POLL_BIST_RUNNING 0x1
/* enum: Passed. */
#define          MC_CMD_POLL_BIST_PASSED 0x2
/* enum: Failed. */
#define          MC_CMD_POLL_BIST_FAILED 0x3
/* enum: Timed-out. */
#define          MC_CMD_POLL_BIST_TIMEOUT 0x4
#define       MC_CMD_POLL_BIST_OUT_PRIVATE_OFST 4

/* MC_CMD_POLL_BIST_OUT_SFT9001 msgresponse */
#define    MC_CMD_POLL_BIST_OUT_SFT9001_LEN 36
/* result */
/*            MC_CMD_POLL_BIST_OUT_RESULT_OFST 0 */
/*            Enum values, see field(s): */
/*               MC_CMD_POLL_BIST_OUT/MC_CMD_POLL_BIST_OUT_RESULT */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_A_OFST 4
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_B_OFST 8
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_C_OFST 12
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_LENGTH_D_OFST 16
/* Status of each channel A */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_A_OFST 20
/* enum: Ok. */
#define          MC_CMD_POLL_BIST_SFT9001_PAIR_OK 0x1
/* enum: Open. */
#define          MC_CMD_POLL_BIST_SFT9001_PAIR_OPEN 0x2
/* enum: Intra-pair short. */
#define          MC_CMD_POLL_BIST_SFT9001_INTRA_PAIR_SHORT 0x3
/* enum: Inter-pair short. */
#define          MC_CMD_POLL_BIST_SFT9001_INTER_PAIR_SHORT 0x4
/* enum: Busy. */
#define          MC_CMD_POLL_BIST_SFT9001_PAIR_BUSY 0x9
/* Status of each channel B */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_B_OFST 24
/*            Enum values, see field(s): */
/*               CABLE_STATUS_A */
/* Status of each channel C */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_C_OFST 28
/*            Enum values, see field(s): */
/*               CABLE_STATUS_A */
/* Status of each channel D */
#define       MC_CMD_POLL_BIST_OUT_SFT9001_CABLE_STATUS_D_OFST 32
/*            Enum values, see field(s): */
/*               CABLE_STATUS_A */

/* MC_CMD_POLL_BIST_OUT_MRSFP msgresponse */
#define    MC_CMD_POLL_BIST_OUT_MRSFP_LEN 8
/* result */
/*            MC_CMD_POLL_BIST_OUT_RESULT_OFST 0 */
/*            Enum values, see field(s): */
/*               MC_CMD_POLL_BIST_OUT/MC_CMD_POLL_BIST_OUT_RESULT */
#define       MC_CMD_POLL_BIST_OUT_MRSFP_TEST_OFST 4
/* enum: Complete. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_COMPLETE 0x0
/* enum: Bus switch off I2C write. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_WRITE 0x1
/* enum: Bus switch off I2C no access IO exp. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_NO_ACCESS_IO_EXP 0x2
/* enum: Bus switch off I2C no access module. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_OFF_I2C_NO_ACCESS_MODULE 0x3
/* enum: IO exp I2C configure. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_IO_EXP_I2C_CONFIGURE 0x4
/* enum: Bus switch I2C no cross talk. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_BUS_SWITCH_I2C_NO_CROSSTALK 0x5
/* enum: Module presence. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_PRESENCE 0x6
/* enum: Module ID I2C access. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_ID_I2C_ACCESS 0x7
/* enum: Module ID sane value. */
#define          MC_CMD_POLL_BIST_MRSFP_TEST_MODULE_ID_SANE_VALUE 0x8

/* MC_CMD_POLL_BIST_OUT_MEM msgresponse */
#define    MC_CMD_POLL_BIST_OUT_MEM_LEN 36
/* result */
/*            MC_CMD_POLL_BIST_OUT_RESULT_OFST 0 */
/*            Enum values, see field(s): */
/*               MC_CMD_POLL_BIST_OUT/MC_CMD_POLL_BIST_OUT_RESULT */
#define       MC_CMD_POLL_BIST_OUT_MEM_TEST_OFST 4
/* enum: Test has completed. */
#define          MC_CMD_POLL_BIST_MEM_COMPLETE 0x0
/* enum: RAM test - walk ones. */
#define          MC_CMD_POLL_BIST_MEM_MEM_WALK_ONES 0x1
/* enum: RAM test - walk zeros. */
#define          MC_CMD_POLL_BIST_MEM_MEM_WALK_ZEROS 0x2
/* enum: RAM test - walking inversions zeros/ones. */
#define          MC_CMD_POLL_BIST_MEM_MEM_INV_ZERO_ONE 0x3
/* enum: RAM test - walking inversions checkerboard. */
#define          MC_CMD_POLL_BIST_MEM_MEM_INV_CHKBOARD 0x4
/* enum: Register test - set / clear individual bits. */
#define          MC_CMD_POLL_BIST_MEM_REG 0x5
/* enum: ECC error detected. */
#define          MC_CMD_POLL_BIST_MEM_ECC 0x6
/* Failure address, only valid if result is POLL_BIST_FAILED */
#define       MC_CMD_POLL_BIST_OUT_MEM_ADDR_OFST 8
/* Bus or address space to which the failure address corresponds */
#define       MC_CMD_POLL_BIST_OUT_MEM_BUS_OFST 12
/* enum: MC MIPS bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_MC 0x0
/* enum: CSR IREG bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_CSR 0x1
/* enum: RX DPCPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DPCPU_RX 0x2
/* enum: TX0 DPCPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DPCPU_TX0 0x3
/* enum: TX1 DPCPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DPCPU_TX1 0x4
/* enum: RX DICPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DICPU_RX 0x5
/* enum: TX DICPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DICPU_TX 0x6
/* Pattern written to RAM / register */
#define       MC_CMD_POLL_BIST_OUT_MEM_EXPECT_OFST 16
/* Actual value read from RAM / register */
#define       MC_CMD_POLL_BIST_OUT_MEM_ACTUAL_OFST 20
/* ECC error mask */
#define       MC_CMD_POLL_BIST_OUT_MEM_ECC_OFST 24
/* ECC parity error mask */
#define       MC_CMD_POLL_BIST_OUT_MEM_ECC_PARITY_OFST 28
/* ECC fatal error mask */
#define       MC_CMD_POLL_BIST_OUT_MEM_ECC_FATAL_OFST 32


/***********************************/
/* MC_CMD_FLUSH_RX_QUEUES
 * Flush receive queue(s). If SRIOV is enabled (via MC_CMD_SRIOV), then RXQ
 * flushes should be initiated via this MCDI operation, rather than via
 * directly writing FLUSH_CMD.
 *
 * The flush is completed (either done/fail) asynchronously (after this command
 * returns). The driver must still wait for flush done/failure events as usual.
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
 * Returns a bitmask of loopback modes available at each speed.
 */
#define MC_CMD_GET_LOOPBACK_MODES 0x28

/* MC_CMD_GET_LOOPBACK_MODES_IN msgrequest */
#define    MC_CMD_GET_LOOPBACK_MODES_IN_LEN 0

/* MC_CMD_GET_LOOPBACK_MODES_OUT msgresponse */
#define    MC_CMD_GET_LOOPBACK_MODES_OUT_LEN 40
/* Supported loopbacks. */
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_100M_OFST 0
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_100M_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_100M_LO_OFST 0
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_100M_HI_OFST 4
/* enum: None. */
#define          MC_CMD_LOOPBACK_NONE  0x0
/* enum: Data. */
#define          MC_CMD_LOOPBACK_DATA  0x1
/* enum: GMAC. */
#define          MC_CMD_LOOPBACK_GMAC  0x2
/* enum: XGMII. */
#define          MC_CMD_LOOPBACK_XGMII 0x3
/* enum: XGXS. */
#define          MC_CMD_LOOPBACK_XGXS  0x4
/* enum: XAUI. */
#define          MC_CMD_LOOPBACK_XAUI  0x5
/* enum: GMII. */
#define          MC_CMD_LOOPBACK_GMII  0x6
/* enum: SGMII. */
#define          MC_CMD_LOOPBACK_SGMII  0x7
/* enum: XGBR. */
#define          MC_CMD_LOOPBACK_XGBR  0x8
/* enum: XFI. */
#define          MC_CMD_LOOPBACK_XFI  0x9
/* enum: XAUI Far. */
#define          MC_CMD_LOOPBACK_XAUI_FAR  0xa
/* enum: GMII Far. */
#define          MC_CMD_LOOPBACK_GMII_FAR  0xb
/* enum: SGMII Far. */
#define          MC_CMD_LOOPBACK_SGMII_FAR  0xc
/* enum: XFI Far. */
#define          MC_CMD_LOOPBACK_XFI_FAR  0xd
/* enum: GPhy. */
#define          MC_CMD_LOOPBACK_GPHY  0xe
/* enum: PhyXS. */
#define          MC_CMD_LOOPBACK_PHYXS  0xf
/* enum: PCS. */
#define          MC_CMD_LOOPBACK_PCS  0x10
/* enum: PMA-PMD. */
#define          MC_CMD_LOOPBACK_PMAPMD  0x11
/* enum: Cross-Port. */
#define          MC_CMD_LOOPBACK_XPORT  0x12
/* enum: XGMII-Wireside. */
#define          MC_CMD_LOOPBACK_XGMII_WS  0x13
/* enum: XAUI Wireside. */
#define          MC_CMD_LOOPBACK_XAUI_WS  0x14
/* enum: XAUI Wireside Far. */
#define          MC_CMD_LOOPBACK_XAUI_WS_FAR  0x15
/* enum: XAUI Wireside near. */
#define          MC_CMD_LOOPBACK_XAUI_WS_NEAR  0x16
/* enum: GMII Wireside. */
#define          MC_CMD_LOOPBACK_GMII_WS  0x17
/* enum: XFI Wireside. */
#define          MC_CMD_LOOPBACK_XFI_WS  0x18
/* enum: XFI Wireside Far. */
#define          MC_CMD_LOOPBACK_XFI_WS_FAR  0x19
/* enum: PhyXS Wireside. */
#define          MC_CMD_LOOPBACK_PHYXS_WS  0x1a
/* enum: PMA lanes MAC-Serdes. */
#define          MC_CMD_LOOPBACK_PMA_INT  0x1b
/* enum: KR Serdes Parallel (Encoder). */
#define          MC_CMD_LOOPBACK_SD_NEAR  0x1c
/* enum: KR Serdes Serial. */
#define          MC_CMD_LOOPBACK_SD_FAR  0x1d
/* enum: PMA lanes MAC-Serdes Wireside. */
#define          MC_CMD_LOOPBACK_PMA_INT_WS  0x1e
/* enum: KR Serdes Parallel Wireside (Full PCS). */
#define          MC_CMD_LOOPBACK_SD_FEP2_WS  0x1f
/* enum: KR Serdes Parallel Wireside (Sym Aligner to TX). */
#define          MC_CMD_LOOPBACK_SD_FEP1_5_WS  0x20
/* enum: KR Serdes Parallel Wireside (Deserializer to Serializer). */
#define          MC_CMD_LOOPBACK_SD_FEP_WS  0x21
/* enum: KR Serdes Serial Wireside. */
#define          MC_CMD_LOOPBACK_SD_FES_WS  0x22
/* Supported loopbacks. */
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_1G_OFST 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_1G_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_1G_LO_OFST 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_1G_HI_OFST 12
/*            Enum values, see field(s): */
/*               100M */
/* Supported loopbacks. */
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_10G_OFST 16
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_10G_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_10G_LO_OFST 16
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_10G_HI_OFST 20
/*            Enum values, see field(s): */
/*               100M */
/* Supported loopbacks. */
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_OFST 24
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_LO_OFST 24
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_SUGGESTED_HI_OFST 28
/*            Enum values, see field(s): */
/*               100M */
/* Supported loopbacks. */
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_40G_OFST 32
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_40G_LEN 8
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_40G_LO_OFST 32
#define       MC_CMD_GET_LOOPBACK_MODES_OUT_40G_HI_OFST 36
/*            Enum values, see field(s): */
/*               100M */


/***********************************/
/* MC_CMD_GET_LINK
 * Read the unified MAC/PHY link state. Locks required: None Return code: 0,
 * ETIME.
 */
#define MC_CMD_GET_LINK 0x29

/* MC_CMD_GET_LINK_IN msgrequest */
#define    MC_CMD_GET_LINK_IN_LEN 0

/* MC_CMD_GET_LINK_OUT msgresponse */
#define    MC_CMD_GET_LINK_OUT_LEN 28
/* near-side advertised capabilities */
#define       MC_CMD_GET_LINK_OUT_CAP_OFST 0
/* link-partner advertised capabilities */
#define       MC_CMD_GET_LINK_OUT_LP_CAP_OFST 4
/* Autonegotiated speed in mbit/s. The link may still be down even if this
 * reads non-zero.
 */
#define       MC_CMD_GET_LINK_OUT_LINK_SPEED_OFST 8
/* Current loopback setting. */
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
/* This returns the negotiated flow control value. */
#define       MC_CMD_GET_LINK_OUT_FCNTL_OFST 20
/* enum: Flow control is off. */
#define          MC_CMD_FCNTL_OFF 0x0
/* enum: Respond to flow control. */
#define          MC_CMD_FCNTL_RESPOND 0x1
/* enum: Respond to and Issue flow control. */
#define          MC_CMD_FCNTL_BIDIR 0x2
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
 * Write the unified MAC/PHY link configuration. Locks required: None. Return
 * code: 0, EINVAL, ETIME
 */
#define MC_CMD_SET_LINK 0x2a

/* MC_CMD_SET_LINK_IN msgrequest */
#define    MC_CMD_SET_LINK_IN_LEN 16
/* ??? */
#define       MC_CMD_SET_LINK_IN_CAP_OFST 0
/* Flags */
#define       MC_CMD_SET_LINK_IN_FLAGS_OFST 4
#define        MC_CMD_SET_LINK_IN_LOWPOWER_LBN 0
#define        MC_CMD_SET_LINK_IN_LOWPOWER_WIDTH 1
#define        MC_CMD_SET_LINK_IN_POWEROFF_LBN 1
#define        MC_CMD_SET_LINK_IN_POWEROFF_WIDTH 1
#define        MC_CMD_SET_LINK_IN_TXDIS_LBN 2
#define        MC_CMD_SET_LINK_IN_TXDIS_WIDTH 1
/* Loopback mode. */
#define       MC_CMD_SET_LINK_IN_LOOPBACK_MODE_OFST 8
/*            Enum values, see field(s): */
/*               MC_CMD_GET_LOOPBACK_MODES/MC_CMD_GET_LOOPBACK_MODES_OUT/100M */
/* A loopback speed of "0" is supported, and means (choose any available
 * speed).
 */
#define       MC_CMD_SET_LINK_IN_LOOPBACK_SPEED_OFST 12

/* MC_CMD_SET_LINK_OUT msgresponse */
#define    MC_CMD_SET_LINK_OUT_LEN 0


/***********************************/
/* MC_CMD_SET_ID_LED
 * Set identification LED state. Locks required: None. Return code: 0, EINVAL
 */
#define MC_CMD_SET_ID_LED 0x2b

/* MC_CMD_SET_ID_LED_IN msgrequest */
#define    MC_CMD_SET_ID_LED_IN_LEN 4
/* Set LED state. */
#define       MC_CMD_SET_ID_LED_IN_STATE_OFST 0
#define          MC_CMD_LED_OFF  0x0 /* enum */
#define          MC_CMD_LED_ON  0x1 /* enum */
#define          MC_CMD_LED_DEFAULT  0x2 /* enum */

/* MC_CMD_SET_ID_LED_OUT msgresponse */
#define    MC_CMD_SET_ID_LED_OUT_LEN 0


/***********************************/
/* MC_CMD_SET_MAC
 * Set MAC configuration. Locks required: None. Return code: 0, EINVAL
 */
#define MC_CMD_SET_MAC 0x2c

/* MC_CMD_SET_MAC_IN msgrequest */
#define    MC_CMD_SET_MAC_IN_LEN 24
/* The MTU is the MTU programmed directly into the XMAC/GMAC (inclusive of
 * EtherII, VLAN, bug16011 padding).
 */
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
/* enum: Flow control is off. */
/*               MC_CMD_FCNTL_OFF 0x0 */
/* enum: Respond to flow control. */
/*               MC_CMD_FCNTL_RESPOND 0x1 */
/* enum: Respond to and Issue flow control. */
/*               MC_CMD_FCNTL_BIDIR 0x2 */
/* enum: Auto neg flow control. */
#define          MC_CMD_FCNTL_AUTO 0x3

/* MC_CMD_SET_MAC_OUT msgresponse */
#define    MC_CMD_SET_MAC_OUT_LEN 0


/***********************************/
/* MC_CMD_PHY_STATS
 * Get generic PHY statistics. This call returns the statistics for a generic
 * PHY in a sparse array (indexed by the enumerate). Each value is represented
 * by a 32bit number. If the DMA_ADDR is 0, then no DMA is performed, and the
 * statistics may be read from the message response. If DMA_ADDR != 0, then the
 * statistics are dmad to that (page-aligned location). Locks required: None.
 * Returns: 0, ETIME
 */
#define MC_CMD_PHY_STATS 0x2d

/* MC_CMD_PHY_STATS_IN msgrequest */
#define    MC_CMD_PHY_STATS_IN_LEN 8
/* ??? */
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
/* enum: OUI. */
#define          MC_CMD_OUI  0x0
/* enum: PMA-PMD Link Up. */
#define          MC_CMD_PMA_PMD_LINK_UP  0x1
/* enum: PMA-PMD RX Fault. */
#define          MC_CMD_PMA_PMD_RX_FAULT  0x2
/* enum: PMA-PMD TX Fault. */
#define          MC_CMD_PMA_PMD_TX_FAULT  0x3
/* enum: PMA-PMD Signal */
#define          MC_CMD_PMA_PMD_SIGNAL  0x4
/* enum: PMA-PMD SNR A. */
#define          MC_CMD_PMA_PMD_SNR_A  0x5
/* enum: PMA-PMD SNR B. */
#define          MC_CMD_PMA_PMD_SNR_B  0x6
/* enum: PMA-PMD SNR C. */
#define          MC_CMD_PMA_PMD_SNR_C  0x7
/* enum: PMA-PMD SNR D. */
#define          MC_CMD_PMA_PMD_SNR_D  0x8
/* enum: PCS Link Up. */
#define          MC_CMD_PCS_LINK_UP  0x9
/* enum: PCS RX Fault. */
#define          MC_CMD_PCS_RX_FAULT  0xa
/* enum: PCS TX Fault. */
#define          MC_CMD_PCS_TX_FAULT  0xb
/* enum: PCS BER. */
#define          MC_CMD_PCS_BER  0xc
/* enum: PCS Block Errors. */
#define          MC_CMD_PCS_BLOCK_ERRORS  0xd
/* enum: PhyXS Link Up. */
#define          MC_CMD_PHYXS_LINK_UP  0xe
/* enum: PhyXS RX Fault. */
#define          MC_CMD_PHYXS_RX_FAULT  0xf
/* enum: PhyXS TX Fault. */
#define          MC_CMD_PHYXS_TX_FAULT  0x10
/* enum: PhyXS Align. */
#define          MC_CMD_PHYXS_ALIGN  0x11
/* enum: PhyXS Sync. */
#define          MC_CMD_PHYXS_SYNC  0x12
/* enum: AN link-up. */
#define          MC_CMD_AN_LINK_UP  0x13
/* enum: AN Complete. */
#define          MC_CMD_AN_COMPLETE  0x14
/* enum: AN 10GBaseT Status. */
#define          MC_CMD_AN_10GBT_STATUS  0x15
/* enum: Clause 22 Link-Up. */
#define          MC_CMD_CL22_LINK_UP  0x16
/* enum: (Last entry) */
#define          MC_CMD_PHY_NSTATS  0x17


/***********************************/
/* MC_CMD_MAC_STATS
 * Get generic MAC statistics. This call returns unified statistics maintained
 * by the MC as it switches between the GMAC and XMAC. The MC will write out
 * all supported stats. The driver should zero initialise the buffer to
 * guarantee consistent results. If the DMA_ADDR is 0, then no DMA is
 * performed, and the statistics may be read from the message response. If
 * DMA_ADDR != 0, then the statistics are dmad to that (page-aligned location).
 * Locks required: None. Returns: 0, ETIME
 */
#define MC_CMD_MAC_STATS 0x2e

/* MC_CMD_MAC_STATS_IN msgrequest */
#define    MC_CMD_MAC_STATS_IN_LEN 16
/* ??? */
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
/* this is only used for the first record */
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
 * DMA write data into (Rid,Addr), either by dma reading (Rid,Addr), or by data
 * embedded directly in the command.
 *
 * A common pattern is for a client to use generation counts to signal a dma
 * update of a datastructure. To facilitate this, this MCDI operation can
 * contain multiple requests which are executed in strict order. Requests take
 * the form of duplicating the entire MCDI request continuously (including the
 * requests record, which is ignored in all but the first structure)
 *
 * The source data can either come from a DMA from the host, or it can be
 * embedded within the request directly, thereby eliminating a DMA read. To
 * indicate this, the client sets FROM_RID=%RID_INLINE, ADDR_HI=0, and
 * ADDR_LO=offset, and inserts the data at %offset from the start of the
 * payload. It's the callers responsibility to ensure that the embedded data
 * doesn't overlap the records.
 *
 * Returns: 0, EINVAL (invalid RID)
 */
#define MC_CMD_MEMCPY 0x31

/* MC_CMD_MEMCPY_IN msgrequest */
#define    MC_CMD_MEMCPY_IN_LENMIN 32
#define    MC_CMD_MEMCPY_IN_LENMAX 224
#define    MC_CMD_MEMCPY_IN_LEN(num) (0+32*(num))
/* see MC_CMD_MEMCPY_RECORD_TYPEDEF */
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
/* A type value of 1 is unused. */
#define       MC_CMD_WOL_FILTER_SET_IN_WOL_TYPE_OFST 4
/* enum: Magic */
#define          MC_CMD_WOL_TYPE_MAGIC      0x0
/* enum: MS Windows Magic */
#define          MC_CMD_WOL_TYPE_WIN_MAGIC 0x2
/* enum: IPv4 Syn */
#define          MC_CMD_WOL_TYPE_IPV4_SYN   0x3
/* enum: IPv6 Syn */
#define          MC_CMD_WOL_TYPE_IPV6_SYN   0x4
/* enum: Bitmap */
#define          MC_CMD_WOL_TYPE_BITMAP     0x5
/* enum: Link */
#define          MC_CMD_WOL_TYPE_LINK       0x6
/* enum: (Above this for future use) */
#define          MC_CMD_WOL_TYPE_MAX        0x7
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
 * Remove a WoL filter. Locks required: None. Returns: 0, EINVAL, ENOSYS
 */
#define MC_CMD_WOL_FILTER_REMOVE 0x33

/* MC_CMD_WOL_FILTER_REMOVE_IN msgrequest */
#define    MC_CMD_WOL_FILTER_REMOVE_IN_LEN 4
#define       MC_CMD_WOL_FILTER_REMOVE_IN_FILTER_ID_OFST 0

/* MC_CMD_WOL_FILTER_REMOVE_OUT msgresponse */
#define    MC_CMD_WOL_FILTER_REMOVE_OUT_LEN 0


/***********************************/
/* MC_CMD_WOL_FILTER_RESET
 * Reset (i.e. remove all) WoL filters. Locks required: None. Returns: 0,
 * ENOSYS
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
 * Set the MCAST hash value without otherwise reconfiguring the MAC
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
 * Return bitfield indicating available types of virtual NVRAM partitions.
 * Locks required: none. Returns: 0
 */
#define MC_CMD_NVRAM_TYPES 0x36

/* MC_CMD_NVRAM_TYPES_IN msgrequest */
#define    MC_CMD_NVRAM_TYPES_IN_LEN 0

/* MC_CMD_NVRAM_TYPES_OUT msgresponse */
#define    MC_CMD_NVRAM_TYPES_OUT_LEN 4
/* Bit mask of supported types. */
#define       MC_CMD_NVRAM_TYPES_OUT_TYPES_OFST 0
/* enum: Disabled callisto. */
#define          MC_CMD_NVRAM_TYPE_DISABLED_CALLISTO 0x0
/* enum: MC firmware. */
#define          MC_CMD_NVRAM_TYPE_MC_FW 0x1
/* enum: MC backup firmware. */
#define          MC_CMD_NVRAM_TYPE_MC_FW_BACKUP 0x2
/* enum: Static configuration Port0. */
#define          MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT0 0x3
/* enum: Static configuration Port1. */
#define          MC_CMD_NVRAM_TYPE_STATIC_CFG_PORT1 0x4
/* enum: Dynamic configuration Port0. */
#define          MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT0 0x5
/* enum: Dynamic configuration Port1. */
#define          MC_CMD_NVRAM_TYPE_DYNAMIC_CFG_PORT1 0x6
/* enum: Expansion Rom. */
#define          MC_CMD_NVRAM_TYPE_EXP_ROM 0x7
/* enum: Expansion Rom Configuration Port0. */
#define          MC_CMD_NVRAM_TYPE_EXP_ROM_CFG_PORT0 0x8
/* enum: Expansion Rom Configuration Port1. */
#define          MC_CMD_NVRAM_TYPE_EXP_ROM_CFG_PORT1 0x9
/* enum: Phy Configuration Port0. */
#define          MC_CMD_NVRAM_TYPE_PHY_PORT0 0xa
/* enum: Phy Configuration Port1. */
#define          MC_CMD_NVRAM_TYPE_PHY_PORT1 0xb
/* enum: Log. */
#define          MC_CMD_NVRAM_TYPE_LOG 0xc
/* enum: FPGA image. */
#define          MC_CMD_NVRAM_TYPE_FPGA 0xd
/* enum: FPGA backup image */
#define          MC_CMD_NVRAM_TYPE_FPGA_BACKUP 0xe
/* enum: FC firmware. */
#define          MC_CMD_NVRAM_TYPE_FC_FW 0xf
/* enum: FC backup firmware. */
#define          MC_CMD_NVRAM_TYPE_FC_FW_BACKUP 0x10
/* enum: CPLD image. */
#define          MC_CMD_NVRAM_TYPE_CPLD 0x11
/* enum: Licensing information. */
#define          MC_CMD_NVRAM_TYPE_LICENSE 0x12
/* enum: FC Log. */
#define          MC_CMD_NVRAM_TYPE_FC_LOG 0x13


/***********************************/
/* MC_CMD_NVRAM_INFO
 * Read info about a virtual NVRAM partition. Locks required: none. Returns: 0,
 * EINVAL (bad type).
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
#define        MC_CMD_NVRAM_INFO_OUT_TLV_LBN 1
#define        MC_CMD_NVRAM_INFO_OUT_TLV_WIDTH 1
#define        MC_CMD_NVRAM_INFO_OUT_A_B_LBN 7
#define        MC_CMD_NVRAM_INFO_OUT_A_B_WIDTH 1
#define       MC_CMD_NVRAM_INFO_OUT_PHYSDEV_OFST 16
#define       MC_CMD_NVRAM_INFO_OUT_PHYSADDR_OFST 20


/***********************************/
/* MC_CMD_NVRAM_UPDATE_START
 * Start a group of update operations on a virtual NVRAM partition. Locks
 * required: PHY_LOCK if type==*PHY*. Returns: 0, EINVAL (bad type), EACCES (if
 * PHY_LOCK required and not held).
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
 * Read data from a virtual NVRAM partition. Locks required: PHY_LOCK if
 * type==*PHY*. Returns: 0, EINVAL (bad type/offset/length), EACCES (if
 * PHY_LOCK required and not held)
 */
#define MC_CMD_NVRAM_READ 0x39

/* MC_CMD_NVRAM_READ_IN msgrequest */
#define    MC_CMD_NVRAM_READ_IN_LEN 12
#define       MC_CMD_NVRAM_READ_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_READ_IN_OFFSET_OFST 4
/* amount to read in bytes */
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
 * Write data to a virtual NVRAM partition. Locks required: PHY_LOCK if
 * type==*PHY*. Returns: 0, EINVAL (bad type/offset/length), EACCES (if
 * PHY_LOCK required and not held)
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
 * Erase sector(s) from a virtual NVRAM partition. Locks required: PHY_LOCK if
 * type==*PHY*. Returns: 0, EINVAL (bad type/offset/length), EACCES (if
 * PHY_LOCK required and not held)
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
 * Finish a group of update operations on a virtual NVRAM partition. Locks
 * required: PHY_LOCK if type==*PHY*. Returns: 0, EINVAL (bad
 * type/offset/length), EACCES (if PHY_LOCK required and not held)
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
 *
 * The AFTER_ASSERTION flag is intended to be used when the driver notices an
 * assertion failure (at which point it is expected to perform a complete tear
 * down and reinitialise), to allow both ports to reset the MC once in an
 * atomic fashion.
 *
 * Production mc firmwares are generally compiled with REBOOT_ON_ASSERT=1,
 * which means that they will automatically reboot out of the assertion
 * handler, so this is in practise an optional operation. It is still
 * recommended that drivers execute this to support custom firmwares with
 * REBOOT_ON_ASSERT=0.
 *
 * Locks required: NONE Returns: Nothing. You get back a response with ERR=1,
 * DATALEN=0
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
 * Request scheduler info. Locks required: NONE. Returns: An array of
 * (timeslice,maximum overrun), one for each thread, in ascending order of
 * thread address.
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
 * Set the mode for the next MC reboot. Locks required: NONE. Sets the reboot
 * mode to the specified value. Returns the old mode.
 */
#define MC_CMD_REBOOT_MODE 0x3f

/* MC_CMD_REBOOT_MODE_IN msgrequest */
#define    MC_CMD_REBOOT_MODE_IN_LEN 4
#define       MC_CMD_REBOOT_MODE_IN_VALUE_OFST 0
/* enum: Normal. */
#define          MC_CMD_REBOOT_MODE_NORMAL 0x0
/* enum: Power-on Reset. */
#define          MC_CMD_REBOOT_MODE_POR 0x2
/* enum: Snapper. */
#define          MC_CMD_REBOOT_MODE_SNAPPER 0x3
/* enum: snapper fake POR */
#define          MC_CMD_REBOOT_MODE_SNAPPER_POR 0x4
#define        MC_CMD_REBOOT_MODE_IN_FAKE_LBN 7
#define        MC_CMD_REBOOT_MODE_IN_FAKE_WIDTH 1

/* MC_CMD_REBOOT_MODE_OUT msgresponse */
#define    MC_CMD_REBOOT_MODE_OUT_LEN 4
#define       MC_CMD_REBOOT_MODE_OUT_VALUE_OFST 0


/***********************************/
/* MC_CMD_SENSOR_INFO
 * Returns information about every available sensor.
 *
 * Each sensor has a single (16bit) value, and a corresponding state. The
 * mapping between value and state is nominally determined by the MC, but may
 * be implemented using up to 2 ranges per sensor.
 *
 * This call returns a mask (32bit) of the sensors that are supported by this
 * platform, then an array of sensor information structures, in order of sensor
 * type (but without gaps for unimplemented sensors). Each structure defines
 * the ranges for the corresponding sensor. An unused range is indicated by
 * equal limit values. If one range is used, a value outside that range results
 * in STATE_FATAL. If two ranges are used, a value outside the second range
 * results in STATE_FATAL while a value outside the first and inside the second
 * range results in STATE_WARNING.
 *
 * Sensor masks and sensor information arrays are organised into pages. For
 * backward compatibility, older host software can only use sensors in page 0.
 * Bit 32 in the sensor mask was previously unused, and is no reserved for use
 * as the next page flag.
 *
 * If the request does not contain a PAGE value then firmware will only return
 * page 0 of sensor information, with bit 31 in the sensor mask cleared.
 *
 * If the request contains a PAGE value then firmware responds with the sensor
 * mask and sensor information array for that page of sensors. In this case bit
 * 31 in the mask is set if another page exists.
 *
 * Locks required: None Returns: 0
 */
#define MC_CMD_SENSOR_INFO 0x41

/* MC_CMD_SENSOR_INFO_IN msgrequest */
#define    MC_CMD_SENSOR_INFO_IN_LEN 0

/* MC_CMD_SENSOR_INFO_EXT_IN msgrequest */
#define    MC_CMD_SENSOR_INFO_EXT_IN_LEN 4
/* Which page of sensors to report.
 *
 * Page 0 contains sensors 0 to 30 (sensor 31 is the next page bit).
 *
 * Page 1 contains sensors 32 to 62 (sensor 63 is the next page bit). etc.
 */
#define       MC_CMD_SENSOR_INFO_EXT_IN_PAGE_OFST 0

/* MC_CMD_SENSOR_INFO_OUT msgresponse */
#define    MC_CMD_SENSOR_INFO_OUT_LENMIN 12
#define    MC_CMD_SENSOR_INFO_OUT_LENMAX 252
#define    MC_CMD_SENSOR_INFO_OUT_LEN(num) (4+8*(num))
#define       MC_CMD_SENSOR_INFO_OUT_MASK_OFST 0
/* enum: Controller temperature: degC */
#define          MC_CMD_SENSOR_CONTROLLER_TEMP  0x0
/* enum: Phy common temperature: degC */
#define          MC_CMD_SENSOR_PHY_COMMON_TEMP  0x1
/* enum: Controller cooling: bool */
#define          MC_CMD_SENSOR_CONTROLLER_COOLING  0x2
/* enum: Phy 0 temperature: degC */
#define          MC_CMD_SENSOR_PHY0_TEMP  0x3
/* enum: Phy 0 cooling: bool */
#define          MC_CMD_SENSOR_PHY0_COOLING  0x4
/* enum: Phy 1 temperature: degC */
#define          MC_CMD_SENSOR_PHY1_TEMP  0x5
/* enum: Phy 1 cooling: bool */
#define          MC_CMD_SENSOR_PHY1_COOLING  0x6
/* enum: 1.0v power: mV */
#define          MC_CMD_SENSOR_IN_1V0  0x7
/* enum: 1.2v power: mV */
#define          MC_CMD_SENSOR_IN_1V2  0x8
/* enum: 1.8v power: mV */
#define          MC_CMD_SENSOR_IN_1V8  0x9
/* enum: 2.5v power: mV */
#define          MC_CMD_SENSOR_IN_2V5  0xa
/* enum: 3.3v power: mV */
#define          MC_CMD_SENSOR_IN_3V3  0xb
/* enum: 12v power: mV */
#define          MC_CMD_SENSOR_IN_12V0  0xc
/* enum: 1.2v analogue power: mV */
#define          MC_CMD_SENSOR_IN_1V2A  0xd
/* enum: reference voltage: mV */
#define          MC_CMD_SENSOR_IN_VREF  0xe
/* enum: AOE FPGA power: mV */
#define          MC_CMD_SENSOR_OUT_VAOE  0xf
/* enum: AOE FPGA temperature: degC */
#define          MC_CMD_SENSOR_AOE_TEMP  0x10
/* enum: AOE FPGA PSU temperature: degC */
#define          MC_CMD_SENSOR_PSU_AOE_TEMP  0x11
/* enum: AOE PSU temperature: degC */
#define          MC_CMD_SENSOR_PSU_TEMP  0x12
/* enum: Fan 0 speed: RPM */
#define          MC_CMD_SENSOR_FAN_0  0x13
/* enum: Fan 1 speed: RPM */
#define          MC_CMD_SENSOR_FAN_1  0x14
/* enum: Fan 2 speed: RPM */
#define          MC_CMD_SENSOR_FAN_2  0x15
/* enum: Fan 3 speed: RPM */
#define          MC_CMD_SENSOR_FAN_3  0x16
/* enum: Fan 4 speed: RPM */
#define          MC_CMD_SENSOR_FAN_4  0x17
/* enum: AOE FPGA input power: mV */
#define          MC_CMD_SENSOR_IN_VAOE  0x18
/* enum: AOE FPGA current: mA */
#define          MC_CMD_SENSOR_OUT_IAOE  0x19
/* enum: AOE FPGA input current: mA */
#define          MC_CMD_SENSOR_IN_IAOE  0x1a
/* enum: NIC power consumption: W */
#define          MC_CMD_SENSOR_NIC_POWER  0x1b
/* enum: 0.9v power voltage: mV */
#define          MC_CMD_SENSOR_IN_0V9  0x1c
/* enum: 0.9v power current: mA */
#define          MC_CMD_SENSOR_IN_I0V9  0x1d
/* enum: 1.2v power current: mA */
#define          MC_CMD_SENSOR_IN_I1V2  0x1e
/* enum: Not a sensor: reserved for the next page flag */
#define          MC_CMD_SENSOR_PAGE0_NEXT  0x1f
/* enum: 0.9v power voltage (at ADC): mV */
#define          MC_CMD_SENSOR_IN_0V9_ADC  0x20
/* enum: Controller temperature 2: degC */
#define          MC_CMD_SENSOR_CONTROLLER_2_TEMP  0x21
/* enum: Voltage regulator internal temperature: degC */
#define          MC_CMD_SENSOR_VREG_INTERNAL_TEMP  0x22
/* enum: 0.9V voltage regulator temperature: degC */
#define          MC_CMD_SENSOR_VREG_0V9_TEMP  0x23
/* enum: 1.2V voltage regulator temperature: degC */
#define          MC_CMD_SENSOR_VREG_1V2_TEMP  0x24
/* enum: controller internal temperature sensor voltage (internal ADC): mV */
#define          MC_CMD_SENSOR_CONTROLLER_VPTAT  0x25
/* enum: controller internal temperature (internal ADC): degC */
#define          MC_CMD_SENSOR_CONTROLLER_INTERNAL_TEMP  0x26
/* enum: controller internal temperature sensor voltage (external ADC): mV */
#define          MC_CMD_SENSOR_CONTROLLER_VPTAT_EXTADC  0x27
/* enum: controller internal temperature (external ADC): degC */
#define          MC_CMD_SENSOR_CONTROLLER_INTERNAL_TEMP_EXTADC  0x28
/* enum: ambient temperature: degC */
#define          MC_CMD_SENSOR_AMBIENT_TEMP  0x29
/* enum: air flow: bool */
#define          MC_CMD_SENSOR_AIRFLOW  0x2a
/* enum: voltage between VSS08D and VSS08D at CSR: mV */
#define          MC_CMD_SENSOR_VDD08D_VSS08D_CSR  0x2b
/* enum: voltage between VSS08D and VSS08D at CSR (external ADC): mV */
#define          MC_CMD_SENSOR_VDD08D_VSS08D_CSR_EXTADC  0x2c
/* MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF */
#define       MC_CMD_SENSOR_ENTRY_OFST 4
#define       MC_CMD_SENSOR_ENTRY_LEN 8
#define       MC_CMD_SENSOR_ENTRY_LO_OFST 4
#define       MC_CMD_SENSOR_ENTRY_HI_OFST 8
#define       MC_CMD_SENSOR_ENTRY_MINNUM 1
#define       MC_CMD_SENSOR_ENTRY_MAXNUM 31

/* MC_CMD_SENSOR_INFO_EXT_OUT msgresponse */
#define    MC_CMD_SENSOR_INFO_EXT_OUT_LENMIN 12
#define    MC_CMD_SENSOR_INFO_EXT_OUT_LENMAX 252
#define    MC_CMD_SENSOR_INFO_EXT_OUT_LEN(num) (4+8*(num))
#define       MC_CMD_SENSOR_INFO_EXT_OUT_MASK_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_SENSOR_INFO_OUT */
#define        MC_CMD_SENSOR_INFO_EXT_OUT_NEXT_PAGE_LBN 31
#define        MC_CMD_SENSOR_INFO_EXT_OUT_NEXT_PAGE_WIDTH 1
/* MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF */
/*            MC_CMD_SENSOR_ENTRY_OFST 4 */
/*            MC_CMD_SENSOR_ENTRY_LEN 8 */
/*            MC_CMD_SENSOR_ENTRY_LO_OFST 4 */
/*            MC_CMD_SENSOR_ENTRY_HI_OFST 8 */
/*            MC_CMD_SENSOR_ENTRY_MINNUM 1 */
/*            MC_CMD_SENSOR_ENTRY_MAXNUM 31 */

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
 * Returns the current reading from each sensor. DMAs an array of sensor
 * readings, in order of sensor type (but without gaps for unimplemented
 * sensors), into host memory. Each array element is a
 * MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF dword.
 *
 * If the request does not contain the LENGTH field then only sensors 0 to 30
 * are reported, to avoid DMA buffer overflow in older host software. If the
 * sensor reading require more space than the LENGTH allows, then return
 * EINVAL.
 *
 * The MC will send a SENSOREVT event every time any sensor changes state. The
 * driver is responsible for ensuring that it doesn't miss any events. The
 * board will function normally if all sensors are in STATE_OK or
 * STATE_WARNING. Otherwise the board should not be expected to function.
 */
#define MC_CMD_READ_SENSORS 0x42

/* MC_CMD_READ_SENSORS_IN msgrequest */
#define    MC_CMD_READ_SENSORS_IN_LEN 8
/* DMA address of host buffer for sensor readings (must be 4Kbyte aligned). */
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_OFST 0
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_LEN 8
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_LO_OFST 0
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_HI_OFST 4

/* MC_CMD_READ_SENSORS_EXT_IN msgrequest */
#define    MC_CMD_READ_SENSORS_EXT_IN_LEN 12
/* DMA address of host buffer for sensor readings */
#define       MC_CMD_READ_SENSORS_EXT_IN_DMA_ADDR_OFST 0
#define       MC_CMD_READ_SENSORS_EXT_IN_DMA_ADDR_LEN 8
#define       MC_CMD_READ_SENSORS_EXT_IN_DMA_ADDR_LO_OFST 0
#define       MC_CMD_READ_SENSORS_EXT_IN_DMA_ADDR_HI_OFST 4
/* Size in bytes of host buffer. */
#define       MC_CMD_READ_SENSORS_EXT_IN_LENGTH_OFST 8

/* MC_CMD_READ_SENSORS_OUT msgresponse */
#define    MC_CMD_READ_SENSORS_OUT_LEN 0

/* MC_CMD_READ_SENSORS_EXT_OUT msgresponse */
#define    MC_CMD_READ_SENSORS_EXT_OUT_LEN 0

/* MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF structuredef */
#define    MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_LEN 4
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE_OFST 0
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE_LEN 2
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE_LBN 0
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE_WIDTH 16
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE_OFST 2
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE_LEN 1
/* enum: Ok. */
#define          MC_CMD_SENSOR_STATE_OK  0x0
/* enum: Breached warning threshold. */
#define          MC_CMD_SENSOR_STATE_WARNING  0x1
/* enum: Breached fatal threshold. */
#define          MC_CMD_SENSOR_STATE_FATAL  0x2
/* enum: Fault with sensor. */
#define          MC_CMD_SENSOR_STATE_BROKEN  0x3
/* enum: Sensor is working but does not currently have a reading. */
#define          MC_CMD_SENSOR_STATE_NO_READING  0x4
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE_LBN 16
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE_WIDTH 8
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_TYPE_OFST 3
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_TYPE_LEN 1
/*            Enum values, see field(s): */
/*               MC_CMD_SENSOR_INFO/MC_CMD_SENSOR_INFO_OUT/MASK */
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_TYPE_LBN 24
#define       MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_TYPE_WIDTH 8


/***********************************/
/* MC_CMD_GET_PHY_STATE
 * Report current state of PHY. A 'zombie' PHY is a PHY that has failed to boot
 * (e.g. due to missing or corrupted firmware). Locks required: None. Return
 * code: 0
 */
#define MC_CMD_GET_PHY_STATE 0x43

/* MC_CMD_GET_PHY_STATE_IN msgrequest */
#define    MC_CMD_GET_PHY_STATE_IN_LEN 0

/* MC_CMD_GET_PHY_STATE_OUT msgresponse */
#define    MC_CMD_GET_PHY_STATE_OUT_LEN 4
#define       MC_CMD_GET_PHY_STATE_OUT_STATE_OFST 0
/* enum: Ok. */
#define          MC_CMD_PHY_STATE_OK 0x1
/* enum: Faulty. */
#define          MC_CMD_PHY_STATE_ZOMBIE 0x2


/***********************************/
/* MC_CMD_SETUP_8021QBB
 * 802.1Qbb control. 8 Tx queues that map to priorities 0 - 7. Use all 1s to
 * disable 802.Qbb for a given priority.
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
 * Retrieve ID of any WoL filters. Locks required: None. Returns: 0, ENOSYS
 */
#define MC_CMD_WOL_FILTER_GET 0x45

/* MC_CMD_WOL_FILTER_GET_IN msgrequest */
#define    MC_CMD_WOL_FILTER_GET_IN_LEN 0

/* MC_CMD_WOL_FILTER_GET_OUT msgresponse */
#define    MC_CMD_WOL_FILTER_GET_OUT_LEN 4
#define       MC_CMD_WOL_FILTER_GET_OUT_FILTER_ID_OFST 0


/***********************************/
/* MC_CMD_ADD_LIGHTSOUT_OFFLOAD
 * Add a protocol offload to NIC for lights-out state. Locks required: None.
 * Returns: 0, ENOSYS
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
 * Remove a protocol offload from NIC for lights-out state. Locks required:
 * None. Returns: 0, ENOSYS
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
 * Restore MAC after block reset. Locks required: None. Returns: 0.
 */
#define MC_CMD_MAC_RESET_RESTORE 0x48

/* MC_CMD_MAC_RESET_RESTORE_IN msgrequest */
#define    MC_CMD_MAC_RESET_RESTORE_IN_LEN 0

/* MC_CMD_MAC_RESET_RESTORE_OUT msgresponse */
#define    MC_CMD_MAC_RESET_RESTORE_OUT_LEN 0


/***********************************/
/* MC_CMD_TESTASSERT
 * Deliberately trigger an assert-detonation in the firmware for testing
 * purposes (i.e. to allow tests that the driver copes gracefully). Locks
 * required: None Returns: 0
 */
#define MC_CMD_TESTASSERT 0x49

/* MC_CMD_TESTASSERT_IN msgrequest */
#define    MC_CMD_TESTASSERT_IN_LEN 0

/* MC_CMD_TESTASSERT_OUT msgresponse */
#define    MC_CMD_TESTASSERT_OUT_LEN 0


/***********************************/
/* MC_CMD_WORKAROUND
 * Enable/Disable a given workaround. The mcfw will return EINVAL if it doesn't
 * understand the given workaround number - which should not be treated as a
 * hard error by client code. This op does not imply any semantics about each
 * workaround, that's between the driver and the mcfw on a per-workaround
 * basis. Locks required: None. Returns: 0, EINVAL .
 */
#define MC_CMD_WORKAROUND 0x4a

/* MC_CMD_WORKAROUND_IN msgrequest */
#define    MC_CMD_WORKAROUND_IN_LEN 8
#define       MC_CMD_WORKAROUND_IN_TYPE_OFST 0
/* enum: Bug 17230 work around. */
#define          MC_CMD_WORKAROUND_BUG17230 0x1
/* enum: Bug 35388 work around (unsafe EVQ writes). */
#define          MC_CMD_WORKAROUND_BUG35388 0x2
/* enum: Bug35017 workaround (A64 tables must be identity map) */
#define          MC_CMD_WORKAROUND_BUG35017 0x3
#define       MC_CMD_WORKAROUND_IN_ENABLED_OFST 4

/* MC_CMD_WORKAROUND_OUT msgresponse */
#define    MC_CMD_WORKAROUND_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_PHY_MEDIA_INFO
 * Read media-specific data from PHY (e.g. SFP/SFP+ module ID information for
 * SFP+ PHYs). The 'media type' can be found via GET_PHY_CFG
 * (GET_PHY_CFG_OUT_MEDIA_TYPE); the valid 'page number' input values, and the
 * output data, are interpreted on a per-type basis. For SFP+: PAGE=0 or 1
 * returns a 128-byte block read from module I2C address 0xA0 offset 0 or 0x80.
 * Anything else: currently undefined. Locks required: None. Return code: 0.
 */
#define MC_CMD_GET_PHY_MEDIA_INFO 0x4b

/* MC_CMD_GET_PHY_MEDIA_INFO_IN msgrequest */
#define    MC_CMD_GET_PHY_MEDIA_INFO_IN_LEN 4
#define       MC_CMD_GET_PHY_MEDIA_INFO_IN_PAGE_OFST 0

/* MC_CMD_GET_PHY_MEDIA_INFO_OUT msgresponse */
#define    MC_CMD_GET_PHY_MEDIA_INFO_OUT_LENMIN 5
#define    MC_CMD_GET_PHY_MEDIA_INFO_OUT_LENMAX 252
#define    MC_CMD_GET_PHY_MEDIA_INFO_OUT_LEN(num) (4+1*(num))
/* in bytes */
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATALEN_OFST 0
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_OFST 4
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_LEN 1
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_MINNUM 1
#define       MC_CMD_GET_PHY_MEDIA_INFO_OUT_DATA_MAXNUM 248


/***********************************/
/* MC_CMD_NVRAM_TEST
 * Test a particular NVRAM partition for valid contents (where "valid" depends
 * on the type of partition).
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
/* enum: Passed. */
#define          MC_CMD_NVRAM_TEST_PASS 0x0
/* enum: Failed. */
#define          MC_CMD_NVRAM_TEST_FAIL 0x1
/* enum: Not supported. */
#define          MC_CMD_NVRAM_TEST_NOTSUPP 0x2


/***********************************/
/* MC_CMD_MRSFP_TWEAK
 * Read status and/or set parameters for the 'mrsfp' driver in mr_rusty builds.
 * I2C I/O expander bits are always read; if equaliser parameters are supplied,
 * they are configured first. Locks required: None. Return code: 0, EINVAL.
 */
#define MC_CMD_MRSFP_TWEAK 0x4d

/* MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG msgrequest */
#define    MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_LEN 16
/* 0-6 low->high de-emph. */
#define       MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_TXEQ_LEVEL_OFST 0
/* 0-8 low->high ref.V */
#define       MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_TXEQ_DT_CFG_OFST 4
/* 0-8 0-8 low->high boost */
#define       MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_RXEQ_BOOST_OFST 8
/* 0-8 low->high ref.V */
#define       MC_CMD_MRSFP_TWEAK_IN_EQ_CONFIG_RXEQ_DT_CFG_OFST 12

/* MC_CMD_MRSFP_TWEAK_IN_READ_ONLY msgrequest */
#define    MC_CMD_MRSFP_TWEAK_IN_READ_ONLY_LEN 0

/* MC_CMD_MRSFP_TWEAK_OUT msgresponse */
#define    MC_CMD_MRSFP_TWEAK_OUT_LEN 12
/* input bits */
#define       MC_CMD_MRSFP_TWEAK_OUT_IOEXP_INPUTS_OFST 0
/* output bits */
#define       MC_CMD_MRSFP_TWEAK_OUT_IOEXP_OUTPUTS_OFST 4
/* direction */
#define       MC_CMD_MRSFP_TWEAK_OUT_IOEXP_DIRECTION_OFST 8
/* enum: Out. */
#define          MC_CMD_MRSFP_TWEAK_OUT_IOEXP_DIRECTION_OUT 0x0
/* enum: In. */
#define          MC_CMD_MRSFP_TWEAK_OUT_IOEXP_DIRECTION_IN 0x1


/***********************************/
/* MC_CMD_SENSOR_SET_LIMS
 * Adjusts the sensor limits. This is a warranty-voiding operation. Returns:
 * ENOENT if the sensor specified does not exist, EINVAL if the limits are out
 * of range.
 */
#define MC_CMD_SENSOR_SET_LIMS 0x4e

/* MC_CMD_SENSOR_SET_LIMS_IN msgrequest */
#define    MC_CMD_SENSOR_SET_LIMS_IN_LEN 20
#define       MC_CMD_SENSOR_SET_LIMS_IN_SENSOR_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_SENSOR_INFO/MC_CMD_SENSOR_INFO_OUT/MASK */
/* interpretation is is sensor-specific. */
#define       MC_CMD_SENSOR_SET_LIMS_IN_LOW0_OFST 4
/* interpretation is is sensor-specific. */
#define       MC_CMD_SENSOR_SET_LIMS_IN_HI0_OFST 8
/* interpretation is is sensor-specific. */
#define       MC_CMD_SENSOR_SET_LIMS_IN_LOW1_OFST 12
/* interpretation is is sensor-specific. */
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


/***********************************/
/* MC_CMD_NVRAM_PARTITIONS
 * Reads the list of available virtual NVRAM partition types. Locks required:
 * none. Returns: 0, EINVAL (bad type).
 */
#define MC_CMD_NVRAM_PARTITIONS 0x51

/* MC_CMD_NVRAM_PARTITIONS_IN msgrequest */
#define    MC_CMD_NVRAM_PARTITIONS_IN_LEN 0

/* MC_CMD_NVRAM_PARTITIONS_OUT msgresponse */
#define    MC_CMD_NVRAM_PARTITIONS_OUT_LENMIN 4
#define    MC_CMD_NVRAM_PARTITIONS_OUT_LENMAX 252
#define    MC_CMD_NVRAM_PARTITIONS_OUT_LEN(num) (4+4*(num))
/* total number of partitions */
#define       MC_CMD_NVRAM_PARTITIONS_OUT_NUM_PARTITIONS_OFST 0
/* type ID code for each of NUM_PARTITIONS partitions */
#define       MC_CMD_NVRAM_PARTITIONS_OUT_TYPE_ID_OFST 4
#define       MC_CMD_NVRAM_PARTITIONS_OUT_TYPE_ID_LEN 4
#define       MC_CMD_NVRAM_PARTITIONS_OUT_TYPE_ID_MINNUM 0
#define       MC_CMD_NVRAM_PARTITIONS_OUT_TYPE_ID_MAXNUM 62


/***********************************/
/* MC_CMD_NVRAM_METADATA
 * Reads soft metadata for a virtual NVRAM partition type. Locks required:
 * none. Returns: 0, EINVAL (bad type).
 */
#define MC_CMD_NVRAM_METADATA 0x52

/* MC_CMD_NVRAM_METADATA_IN msgrequest */
#define    MC_CMD_NVRAM_METADATA_IN_LEN 4
/* Partition type ID code */
#define       MC_CMD_NVRAM_METADATA_IN_TYPE_OFST 0

/* MC_CMD_NVRAM_METADATA_OUT msgresponse */
#define    MC_CMD_NVRAM_METADATA_OUT_LENMIN 20
#define    MC_CMD_NVRAM_METADATA_OUT_LENMAX 252
#define    MC_CMD_NVRAM_METADATA_OUT_LEN(num) (20+1*(num))
/* Partition type ID code */
#define       MC_CMD_NVRAM_METADATA_OUT_TYPE_OFST 0
#define       MC_CMD_NVRAM_METADATA_OUT_FLAGS_OFST 4
#define        MC_CMD_NVRAM_METADATA_OUT_SUBTYPE_VALID_LBN 0
#define        MC_CMD_NVRAM_METADATA_OUT_SUBTYPE_VALID_WIDTH 1
#define        MC_CMD_NVRAM_METADATA_OUT_VERSION_VALID_LBN 1
#define        MC_CMD_NVRAM_METADATA_OUT_VERSION_VALID_WIDTH 1
#define        MC_CMD_NVRAM_METADATA_OUT_DESCRIPTION_VALID_LBN 2
#define        MC_CMD_NVRAM_METADATA_OUT_DESCRIPTION_VALID_WIDTH 1
/* Subtype ID code for content of this partition */
#define       MC_CMD_NVRAM_METADATA_OUT_SUBTYPE_OFST 8
/* 1st component of W.X.Y.Z version number for content of this partition */
#define       MC_CMD_NVRAM_METADATA_OUT_VERSION_W_OFST 12
#define       MC_CMD_NVRAM_METADATA_OUT_VERSION_W_LEN 2
/* 2nd component of W.X.Y.Z version number for content of this partition */
#define       MC_CMD_NVRAM_METADATA_OUT_VERSION_X_OFST 14
#define       MC_CMD_NVRAM_METADATA_OUT_VERSION_X_LEN 2
/* 3rd component of W.X.Y.Z version number for content of this partition */
#define       MC_CMD_NVRAM_METADATA_OUT_VERSION_Y_OFST 16
#define       MC_CMD_NVRAM_METADATA_OUT_VERSION_Y_LEN 2
/* 4th component of W.X.Y.Z version number for content of this partition */
#define       MC_CMD_NVRAM_METADATA_OUT_VERSION_Z_OFST 18
#define       MC_CMD_NVRAM_METADATA_OUT_VERSION_Z_LEN 2
/* Zero-terminated string describing the content of this partition */
#define       MC_CMD_NVRAM_METADATA_OUT_DESCRIPTION_OFST 20
#define       MC_CMD_NVRAM_METADATA_OUT_DESCRIPTION_LEN 1
#define       MC_CMD_NVRAM_METADATA_OUT_DESCRIPTION_MINNUM 0
#define       MC_CMD_NVRAM_METADATA_OUT_DESCRIPTION_MAXNUM 232


/***********************************/
/* MC_CMD_GET_MAC_ADDRESSES
 * Returns the base MAC, count and stride for the requestiong function
 */
#define MC_CMD_GET_MAC_ADDRESSES 0x55

/* MC_CMD_GET_MAC_ADDRESSES_IN msgrequest */
#define    MC_CMD_GET_MAC_ADDRESSES_IN_LEN 0

/* MC_CMD_GET_MAC_ADDRESSES_OUT msgresponse */
#define    MC_CMD_GET_MAC_ADDRESSES_OUT_LEN 16
/* Base MAC address */
#define       MC_CMD_GET_MAC_ADDRESSES_OUT_MAC_ADDR_BASE_OFST 0
#define       MC_CMD_GET_MAC_ADDRESSES_OUT_MAC_ADDR_BASE_LEN 6
/* Padding */
#define       MC_CMD_GET_MAC_ADDRESSES_OUT_RESERVED_OFST 6
#define       MC_CMD_GET_MAC_ADDRESSES_OUT_RESERVED_LEN 2
/* Number of allocated MAC addresses */
#define       MC_CMD_GET_MAC_ADDRESSES_OUT_MAC_COUNT_OFST 8
/* Spacing of allocated MAC addresses */
#define       MC_CMD_GET_MAC_ADDRESSES_OUT_MAC_STRIDE_OFST 12

/* MC_CMD_RESOURCE_SPECIFIER enum */
/* enum: Any */
#define          MC_CMD_RESOURCE_INSTANCE_ANY 0xffffffff
/* enum: None */
#define          MC_CMD_RESOURCE_INSTANCE_NONE 0xfffffffe

/* EVB_PORT_ID structuredef */
#define    EVB_PORT_ID_LEN 4
#define       EVB_PORT_ID_PORT_ID_OFST 0
/* enum: An invalid port handle. */
#define          EVB_PORT_ID_NULL  0x0
/* enum: The port assigned to this function.. */
#define          EVB_PORT_ID_ASSIGNED  0x1000000
/* enum: External network port 0 */
#define          EVB_PORT_ID_MAC0  0x2000000
/* enum: External network port 1 */
#define          EVB_PORT_ID_MAC1  0x2000001
/* enum: External network port 2 */
#define          EVB_PORT_ID_MAC2  0x2000002
/* enum: External network port 3 */
#define          EVB_PORT_ID_MAC3  0x2000003
#define       EVB_PORT_ID_PORT_ID_LBN 0
#define       EVB_PORT_ID_PORT_ID_WIDTH 32

/* EVB_VLAN_TAG structuredef */
#define    EVB_VLAN_TAG_LEN 2
/* The VLAN tag value */
#define       EVB_VLAN_TAG_VLAN_ID_LBN 0
#define       EVB_VLAN_TAG_VLAN_ID_WIDTH 12
#define       EVB_VLAN_TAG_MODE_LBN 12
#define       EVB_VLAN_TAG_MODE_WIDTH 4
/* enum: Insert the VLAN. */
#define          EVB_VLAN_TAG_INSERT  0x0
/* enum: Replace the VLAN if already present. */
#define          EVB_VLAN_TAG_REPLACE 0x1

/* BUFTBL_ENTRY structuredef */
#define    BUFTBL_ENTRY_LEN 12
/* the owner ID */
#define       BUFTBL_ENTRY_OID_OFST 0
#define       BUFTBL_ENTRY_OID_LEN 2
#define       BUFTBL_ENTRY_OID_LBN 0
#define       BUFTBL_ENTRY_OID_WIDTH 16
/* the page parameter as one of ESE_DZ_SMC_PAGE_SIZE_ */
#define       BUFTBL_ENTRY_PGSZ_OFST 2
#define       BUFTBL_ENTRY_PGSZ_LEN 2
#define       BUFTBL_ENTRY_PGSZ_LBN 16
#define       BUFTBL_ENTRY_PGSZ_WIDTH 16
/* the raw 64-bit address field from the SMC, not adjusted for page size */
#define       BUFTBL_ENTRY_RAWADDR_OFST 4
#define       BUFTBL_ENTRY_RAWADDR_LEN 8
#define       BUFTBL_ENTRY_RAWADDR_LO_OFST 4
#define       BUFTBL_ENTRY_RAWADDR_HI_OFST 8
#define       BUFTBL_ENTRY_RAWADDR_LBN 32
#define       BUFTBL_ENTRY_RAWADDR_WIDTH 64

/* NVRAM_PARTITION_TYPE structuredef */
#define    NVRAM_PARTITION_TYPE_LEN 2
#define       NVRAM_PARTITION_TYPE_ID_OFST 0
#define       NVRAM_PARTITION_TYPE_ID_LEN 2
/* enum: Primary MC firmware partition */
#define          NVRAM_PARTITION_TYPE_MC_FIRMWARE          0x100
/* enum: Secondary MC firmware partition */
#define          NVRAM_PARTITION_TYPE_MC_FIRMWARE_BACKUP   0x200
/* enum: Expansion ROM partition */
#define          NVRAM_PARTITION_TYPE_EXPANSION_ROM        0x300
/* enum: Static configuration TLV partition */
#define          NVRAM_PARTITION_TYPE_STATIC_CONFIG        0x400
/* enum: Dynamic configuration TLV partition */
#define          NVRAM_PARTITION_TYPE_DYNAMIC_CONFIG       0x500
/* enum: Expansion ROM configuration data for port 0 */
#define          NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT0  0x600
/* enum: Expansion ROM configuration data for port 1 */
#define          NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT1  0x601
/* enum: Expansion ROM configuration data for port 2 */
#define          NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT2  0x602
/* enum: Expansion ROM configuration data for port 3 */
#define          NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT3  0x603
/* enum: Non-volatile log output partition */
#define          NVRAM_PARTITION_TYPE_LOG                  0x700
/* enum: Device state dump output partition */
#define          NVRAM_PARTITION_TYPE_DUMP                 0x800
/* enum: Application license key storage partition */
#define          NVRAM_PARTITION_TYPE_LICENSE              0x900
/* enum: Start of reserved value range (firmware may use for any purpose) */
#define          NVRAM_PARTITION_TYPE_RESERVED_VALUES_MIN  0xff00
/* enum: End of reserved value range (firmware may use for any purpose) */
#define          NVRAM_PARTITION_TYPE_RESERVED_VALUES_MAX  0xfffd
/* enum: Recovery partition map (provided if real map is missing or corrupt) */
#define          NVRAM_PARTITION_TYPE_RECOVERY_MAP         0xfffe
/* enum: Partition map (real map as stored in flash) */
#define          NVRAM_PARTITION_TYPE_PARTITION_MAP        0xffff
#define       NVRAM_PARTITION_TYPE_ID_LBN 0
#define       NVRAM_PARTITION_TYPE_ID_WIDTH 16


/***********************************/
/* MC_CMD_READ_REGS
 * Get a dump of the MCPU registers
 */
#define MC_CMD_READ_REGS 0x50

/* MC_CMD_READ_REGS_IN msgrequest */
#define    MC_CMD_READ_REGS_IN_LEN 0

/* MC_CMD_READ_REGS_OUT msgresponse */
#define    MC_CMD_READ_REGS_OUT_LEN 308
/* Whether the corresponding register entry contains a valid value */
#define       MC_CMD_READ_REGS_OUT_MASK_OFST 0
#define       MC_CMD_READ_REGS_OUT_MASK_LEN 16
/* Same order as MIPS GDB (r0-r31, sr, lo, hi, bad, cause, 32 x float, fsr,
 * fir, fp)
 */
#define       MC_CMD_READ_REGS_OUT_REGS_OFST 16
#define       MC_CMD_READ_REGS_OUT_REGS_LEN 4
#define       MC_CMD_READ_REGS_OUT_REGS_NUM 73


/***********************************/
/* MC_CMD_INIT_EVQ
 * Set up an event queue according to the supplied parameters. The IN arguments
 * end with an address for each 4k of host memory required to back the EVQ.
 */
#define MC_CMD_INIT_EVQ 0x80

/* MC_CMD_INIT_EVQ_IN msgrequest */
#define    MC_CMD_INIT_EVQ_IN_LENMIN 44
#define    MC_CMD_INIT_EVQ_IN_LENMAX 548
#define    MC_CMD_INIT_EVQ_IN_LEN(num) (36+8*(num))
/* Size, in entries */
#define       MC_CMD_INIT_EVQ_IN_SIZE_OFST 0
/* Desired instance. Must be set to a specific instance, which is a function
 * local queue index.
 */
#define       MC_CMD_INIT_EVQ_IN_INSTANCE_OFST 4
/* The initial timer value. The load value is ignored if the timer mode is DIS.
 */
#define       MC_CMD_INIT_EVQ_IN_TMR_LOAD_OFST 8
/* The reload value is ignored in one-shot modes */
#define       MC_CMD_INIT_EVQ_IN_TMR_RELOAD_OFST 12
/* tbd */
#define       MC_CMD_INIT_EVQ_IN_FLAGS_OFST 16
#define        MC_CMD_INIT_EVQ_IN_FLAG_INTERRUPTING_LBN 0
#define        MC_CMD_INIT_EVQ_IN_FLAG_INTERRUPTING_WIDTH 1
#define        MC_CMD_INIT_EVQ_IN_FLAG_RPTR_DOS_LBN 1
#define        MC_CMD_INIT_EVQ_IN_FLAG_RPTR_DOS_WIDTH 1
#define        MC_CMD_INIT_EVQ_IN_FLAG_INT_ARMD_LBN 2
#define        MC_CMD_INIT_EVQ_IN_FLAG_INT_ARMD_WIDTH 1
#define        MC_CMD_INIT_EVQ_IN_FLAG_CUT_THRU_LBN 3
#define        MC_CMD_INIT_EVQ_IN_FLAG_CUT_THRU_WIDTH 1
#define        MC_CMD_INIT_EVQ_IN_FLAG_RX_MERGE_LBN 4
#define        MC_CMD_INIT_EVQ_IN_FLAG_RX_MERGE_WIDTH 1
#define        MC_CMD_INIT_EVQ_IN_FLAG_TX_MERGE_LBN 5
#define        MC_CMD_INIT_EVQ_IN_FLAG_TX_MERGE_WIDTH 1
#define       MC_CMD_INIT_EVQ_IN_TMR_MODE_OFST 20
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_IN_TMR_MODE_DIS 0x0
/* enum: Immediate */
#define          MC_CMD_INIT_EVQ_IN_TMR_IMMED_START 0x1
/* enum: Triggered */
#define          MC_CMD_INIT_EVQ_IN_TMR_TRIG_START 0x2
/* enum: Hold-off */
#define          MC_CMD_INIT_EVQ_IN_TMR_INT_HLDOFF 0x3
/* Target EVQ for wakeups if in wakeup mode. */
#define       MC_CMD_INIT_EVQ_IN_TARGET_EVQ_OFST 24
/* Target interrupt if in interrupting mode (note union with target EVQ). Use
 * MC_CMD_RESOURCE_INSTANCE_ANY unless a specific one required for test
 * purposes.
 */
#define       MC_CMD_INIT_EVQ_IN_IRQ_NUM_OFST 24
/* Event Counter Mode. */
#define       MC_CMD_INIT_EVQ_IN_COUNT_MODE_OFST 28
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_IN_COUNT_MODE_DIS 0x0
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_IN_COUNT_MODE_RX 0x1
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_IN_COUNT_MODE_TX 0x2
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_IN_COUNT_MODE_RXTX 0x3
/* Event queue packet count threshold. */
#define       MC_CMD_INIT_EVQ_IN_COUNT_THRSHLD_OFST 32
/* 64-bit address of 4k of 4k-aligned host memory buffer */
#define       MC_CMD_INIT_EVQ_IN_DMA_ADDR_OFST 36
#define       MC_CMD_INIT_EVQ_IN_DMA_ADDR_LEN 8
#define       MC_CMD_INIT_EVQ_IN_DMA_ADDR_LO_OFST 36
#define       MC_CMD_INIT_EVQ_IN_DMA_ADDR_HI_OFST 40
#define       MC_CMD_INIT_EVQ_IN_DMA_ADDR_MINNUM 1
#define       MC_CMD_INIT_EVQ_IN_DMA_ADDR_MAXNUM 64

/* MC_CMD_INIT_EVQ_OUT msgresponse */
#define    MC_CMD_INIT_EVQ_OUT_LEN 4
/* Only valid if INTRFLAG was true */
#define       MC_CMD_INIT_EVQ_OUT_IRQ_OFST 0

/* QUEUE_CRC_MODE structuredef */
#define    QUEUE_CRC_MODE_LEN 1
#define       QUEUE_CRC_MODE_MODE_LBN 0
#define       QUEUE_CRC_MODE_MODE_WIDTH 4
/* enum: No CRC. */
#define          QUEUE_CRC_MODE_NONE  0x0
/* enum: CRC Fiber channel over ethernet. */
#define          QUEUE_CRC_MODE_FCOE  0x1
/* enum: CRC (digest) iSCSI header only. */
#define          QUEUE_CRC_MODE_ISCSI_HDR  0x2
/* enum: CRC (digest) iSCSI header and payload. */
#define          QUEUE_CRC_MODE_ISCSI  0x3
/* enum: CRC Fiber channel over IP over ethernet. */
#define          QUEUE_CRC_MODE_FCOIPOE  0x4
/* enum: CRC MPA. */
#define          QUEUE_CRC_MODE_MPA  0x5
#define       QUEUE_CRC_MODE_SPARE_LBN 4
#define       QUEUE_CRC_MODE_SPARE_WIDTH 4


/***********************************/
/* MC_CMD_INIT_RXQ
 * set up a receive queue according to the supplied parameters. The IN
 * arguments end with an address for each 4k of host memory required to back
 * the RXQ.
 */
#define MC_CMD_INIT_RXQ 0x81

/* MC_CMD_INIT_RXQ_IN msgrequest */
#define    MC_CMD_INIT_RXQ_IN_LENMIN 36
#define    MC_CMD_INIT_RXQ_IN_LENMAX 252
#define    MC_CMD_INIT_RXQ_IN_LEN(num) (28+8*(num))
/* Size, in entries */
#define       MC_CMD_INIT_RXQ_IN_SIZE_OFST 0
/* The EVQ to send events to. This is an index originally specified to INIT_EVQ
 */
#define       MC_CMD_INIT_RXQ_IN_TARGET_EVQ_OFST 4
/* The value to put in the event data. Check hardware spec. for valid range. */
#define       MC_CMD_INIT_RXQ_IN_LABEL_OFST 8
/* Desired instance. Must be set to a specific instance, which is a function
 * local queue index.
 */
#define       MC_CMD_INIT_RXQ_IN_INSTANCE_OFST 12
/* There will be more flags here. */
#define       MC_CMD_INIT_RXQ_IN_FLAGS_OFST 16
#define        MC_CMD_INIT_RXQ_IN_FLAG_BUFF_MODE_LBN 0
#define        MC_CMD_INIT_RXQ_IN_FLAG_BUFF_MODE_WIDTH 1
#define        MC_CMD_INIT_RXQ_IN_FLAG_HDR_SPLIT_LBN 1
#define        MC_CMD_INIT_RXQ_IN_FLAG_HDR_SPLIT_WIDTH 1
#define        MC_CMD_INIT_RXQ_IN_FLAG_TIMESTAMP_LBN 2
#define        MC_CMD_INIT_RXQ_IN_FLAG_TIMESTAMP_WIDTH 1
#define        MC_CMD_INIT_RXQ_IN_CRC_MODE_LBN 3
#define        MC_CMD_INIT_RXQ_IN_CRC_MODE_WIDTH 4
#define        MC_CMD_INIT_RXQ_IN_FLAG_CHAIN_LBN 7
#define        MC_CMD_INIT_RXQ_IN_FLAG_CHAIN_WIDTH 1
#define        MC_CMD_INIT_RXQ_IN_FLAG_PREFIX_LBN 8
#define        MC_CMD_INIT_RXQ_IN_FLAG_PREFIX_WIDTH 1
/* Owner ID to use if in buffer mode (zero if physical) */
#define       MC_CMD_INIT_RXQ_IN_OWNER_ID_OFST 20
/* The port ID associated with the v-adaptor which should contain this DMAQ. */
#define       MC_CMD_INIT_RXQ_IN_PORT_ID_OFST 24
/* 64-bit address of 4k of 4k-aligned host memory buffer */
#define       MC_CMD_INIT_RXQ_IN_DMA_ADDR_OFST 28
#define       MC_CMD_INIT_RXQ_IN_DMA_ADDR_LEN 8
#define       MC_CMD_INIT_RXQ_IN_DMA_ADDR_LO_OFST 28
#define       MC_CMD_INIT_RXQ_IN_DMA_ADDR_HI_OFST 32
#define       MC_CMD_INIT_RXQ_IN_DMA_ADDR_MINNUM 1
#define       MC_CMD_INIT_RXQ_IN_DMA_ADDR_MAXNUM 28

/* MC_CMD_INIT_RXQ_OUT msgresponse */
#define    MC_CMD_INIT_RXQ_OUT_LEN 0


/***********************************/
/* MC_CMD_INIT_TXQ
 */
#define MC_CMD_INIT_TXQ 0x82

/* MC_CMD_INIT_TXQ_IN msgrequest */
#define    MC_CMD_INIT_TXQ_IN_LENMIN 36
#define    MC_CMD_INIT_TXQ_IN_LENMAX 252
#define    MC_CMD_INIT_TXQ_IN_LEN(num) (28+8*(num))
/* Size, in entries */
#define       MC_CMD_INIT_TXQ_IN_SIZE_OFST 0
/* The EVQ to send events to. This is an index originally specified to
 * INIT_EVQ.
 */
#define       MC_CMD_INIT_TXQ_IN_TARGET_EVQ_OFST 4
/* The value to put in the event data. Check hardware spec. for valid range. */
#define       MC_CMD_INIT_TXQ_IN_LABEL_OFST 8
/* Desired instance. Must be set to a specific instance, which is a function
 * local queue index.
 */
#define       MC_CMD_INIT_TXQ_IN_INSTANCE_OFST 12
/* There will be more flags here. */
#define       MC_CMD_INIT_TXQ_IN_FLAGS_OFST 16
#define        MC_CMD_INIT_TXQ_IN_FLAG_BUFF_MODE_LBN 0
#define        MC_CMD_INIT_TXQ_IN_FLAG_BUFF_MODE_WIDTH 1
#define        MC_CMD_INIT_TXQ_IN_FLAG_IP_CSUM_DIS_LBN 1
#define        MC_CMD_INIT_TXQ_IN_FLAG_IP_CSUM_DIS_WIDTH 1
#define        MC_CMD_INIT_TXQ_IN_FLAG_TCP_CSUM_DIS_LBN 2
#define        MC_CMD_INIT_TXQ_IN_FLAG_TCP_CSUM_DIS_WIDTH 1
#define        MC_CMD_INIT_TXQ_IN_FLAG_TCP_UDP_ONLY_LBN 3
#define        MC_CMD_INIT_TXQ_IN_FLAG_TCP_UDP_ONLY_WIDTH 1
#define        MC_CMD_INIT_TXQ_IN_CRC_MODE_LBN 4
#define        MC_CMD_INIT_TXQ_IN_CRC_MODE_WIDTH 4
#define        MC_CMD_INIT_TXQ_IN_FLAG_TIMESTAMP_LBN 8
#define        MC_CMD_INIT_TXQ_IN_FLAG_TIMESTAMP_WIDTH 1
#define        MC_CMD_INIT_TXQ_IN_FLAG_PACER_BYPASS_LBN 9
#define        MC_CMD_INIT_TXQ_IN_FLAG_PACER_BYPASS_WIDTH 1
/* Owner ID to use if in buffer mode (zero if physical) */
#define       MC_CMD_INIT_TXQ_IN_OWNER_ID_OFST 20
/* The port ID associated with the v-adaptor which should contain this DMAQ. */
#define       MC_CMD_INIT_TXQ_IN_PORT_ID_OFST 24
/* 64-bit address of 4k of 4k-aligned host memory buffer */
#define       MC_CMD_INIT_TXQ_IN_DMA_ADDR_OFST 28
#define       MC_CMD_INIT_TXQ_IN_DMA_ADDR_LEN 8
#define       MC_CMD_INIT_TXQ_IN_DMA_ADDR_LO_OFST 28
#define       MC_CMD_INIT_TXQ_IN_DMA_ADDR_HI_OFST 32
#define       MC_CMD_INIT_TXQ_IN_DMA_ADDR_MINNUM 1
#define       MC_CMD_INIT_TXQ_IN_DMA_ADDR_MAXNUM 28

/* MC_CMD_INIT_TXQ_OUT msgresponse */
#define    MC_CMD_INIT_TXQ_OUT_LEN 0


/***********************************/
/* MC_CMD_FINI_EVQ
 * Teardown an EVQ.
 *
 * All DMAQs or EVQs that point to the EVQ to tear down must be torn down first
 * or the operation will fail with EBUSY
 */
#define MC_CMD_FINI_EVQ 0x83

/* MC_CMD_FINI_EVQ_IN msgrequest */
#define    MC_CMD_FINI_EVQ_IN_LEN 4
/* Instance of EVQ to destroy. Should be the same instance as that previously
 * passed to INIT_EVQ
 */
#define       MC_CMD_FINI_EVQ_IN_INSTANCE_OFST 0

/* MC_CMD_FINI_EVQ_OUT msgresponse */
#define    MC_CMD_FINI_EVQ_OUT_LEN 0


/***********************************/
/* MC_CMD_FINI_RXQ
 * Teardown a RXQ.
 */
#define MC_CMD_FINI_RXQ 0x84

/* MC_CMD_FINI_RXQ_IN msgrequest */
#define    MC_CMD_FINI_RXQ_IN_LEN 4
/* Instance of RXQ to destroy */
#define       MC_CMD_FINI_RXQ_IN_INSTANCE_OFST 0

/* MC_CMD_FINI_RXQ_OUT msgresponse */
#define    MC_CMD_FINI_RXQ_OUT_LEN 0


/***********************************/
/* MC_CMD_FINI_TXQ
 * Teardown a TXQ.
 */
#define MC_CMD_FINI_TXQ 0x85

/* MC_CMD_FINI_TXQ_IN msgrequest */
#define    MC_CMD_FINI_TXQ_IN_LEN 4
/* Instance of TXQ to destroy */
#define       MC_CMD_FINI_TXQ_IN_INSTANCE_OFST 0

/* MC_CMD_FINI_TXQ_OUT msgresponse */
#define    MC_CMD_FINI_TXQ_OUT_LEN 0


/***********************************/
/* MC_CMD_DRIVER_EVENT
 * Generate an event on an EVQ belonging to the function issuing the command.
 */
#define MC_CMD_DRIVER_EVENT 0x86

/* MC_CMD_DRIVER_EVENT_IN msgrequest */
#define    MC_CMD_DRIVER_EVENT_IN_LEN 12
/* Handle of target EVQ */
#define       MC_CMD_DRIVER_EVENT_IN_EVQ_OFST 0
/* Bits 0 - 63 of event */
#define       MC_CMD_DRIVER_EVENT_IN_DATA_OFST 4
#define       MC_CMD_DRIVER_EVENT_IN_DATA_LEN 8
#define       MC_CMD_DRIVER_EVENT_IN_DATA_LO_OFST 4
#define       MC_CMD_DRIVER_EVENT_IN_DATA_HI_OFST 8

/* MC_CMD_DRIVER_EVENT_OUT msgresponse */
#define    MC_CMD_DRIVER_EVENT_OUT_LEN 0


/***********************************/
/* MC_CMD_PROXY_CMD
 * Execute an arbitrary MCDI command on behalf of a different function, subject
 * to security restrictions. The command to be proxied follows immediately
 * afterward in the host buffer (or on the UART). This command supercedes
 * MC_CMD_SET_FUNC, which remains available for Siena but now deprecated.
 */
#define MC_CMD_PROXY_CMD 0x5b

/* MC_CMD_PROXY_CMD_IN msgrequest */
#define    MC_CMD_PROXY_CMD_IN_LEN 4
/* The handle of the target function. */
#define       MC_CMD_PROXY_CMD_IN_TARGET_OFST 0
#define        MC_CMD_PROXY_CMD_IN_TARGET_PF_LBN 0
#define        MC_CMD_PROXY_CMD_IN_TARGET_PF_WIDTH 16
#define        MC_CMD_PROXY_CMD_IN_TARGET_VF_LBN 16
#define        MC_CMD_PROXY_CMD_IN_TARGET_VF_WIDTH 16
#define          MC_CMD_PROXY_CMD_IN_VF_NULL  0xffff /* enum */


/***********************************/
/* MC_CMD_ALLOC_BUFTBL_CHUNK
 * Allocate a set of buffer table entries using the specified owner ID. This
 * operation allocates the required buffer table entries (and fails if it
 * cannot do so). The buffer table entries will initially be zeroed.
 */
#define MC_CMD_ALLOC_BUFTBL_CHUNK 0x87

/* MC_CMD_ALLOC_BUFTBL_CHUNK_IN msgrequest */
#define    MC_CMD_ALLOC_BUFTBL_CHUNK_IN_LEN 8
/* Owner ID to use */
#define       MC_CMD_ALLOC_BUFTBL_CHUNK_IN_OWNER_OFST 0
/* Size of buffer table pages to use, in bytes (note that only a few values are
 * legal on any specific hardware).
 */
#define       MC_CMD_ALLOC_BUFTBL_CHUNK_IN_PAGE_SIZE_OFST 4

/* MC_CMD_ALLOC_BUFTBL_CHUNK_OUT msgresponse */
#define    MC_CMD_ALLOC_BUFTBL_CHUNK_OUT_LEN 12
#define       MC_CMD_ALLOC_BUFTBL_CHUNK_OUT_HANDLE_OFST 0
#define       MC_CMD_ALLOC_BUFTBL_CHUNK_OUT_NUMENTRIES_OFST 4
/* Buffer table IDs for use in DMA descriptors. */
#define       MC_CMD_ALLOC_BUFTBL_CHUNK_OUT_ID_OFST 8


/***********************************/
/* MC_CMD_PROGRAM_BUFTBL_ENTRIES
 * Reprogram a set of buffer table entries in the specified chunk.
 */
#define MC_CMD_PROGRAM_BUFTBL_ENTRIES 0x88

/* MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN msgrequest */
#define    MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_LENMIN 20
#define    MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_LENMAX 252
#define    MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_LEN(num) (12+8*(num))
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_HANDLE_OFST 0
/* ID */
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_FIRSTID_OFST 4
/* Num entries */
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_NUMENTRIES_OFST 8
/* Buffer table entry address */
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_ENTRY_OFST 12
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_ENTRY_LEN 8
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_ENTRY_LO_OFST 12
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_ENTRY_HI_OFST 16
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_ENTRY_MINNUM 1
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_ENTRY_MAXNUM 30

/* MC_CMD_PROGRAM_BUFTBL_ENTRIES_OUT msgresponse */
#define    MC_CMD_PROGRAM_BUFTBL_ENTRIES_OUT_LEN 0


/***********************************/
/* MC_CMD_FREE_BUFTBL_CHUNK
 */
#define MC_CMD_FREE_BUFTBL_CHUNK 0x89

/* MC_CMD_FREE_BUFTBL_CHUNK_IN msgrequest */
#define    MC_CMD_FREE_BUFTBL_CHUNK_IN_LEN 4
#define       MC_CMD_FREE_BUFTBL_CHUNK_IN_HANDLE_OFST 0

/* MC_CMD_FREE_BUFTBL_CHUNK_OUT msgresponse */
#define    MC_CMD_FREE_BUFTBL_CHUNK_OUT_LEN 0


/***********************************/
/* MC_CMD_FILTER_OP
 * Multiplexed MCDI call for filter operations
 */
#define MC_CMD_FILTER_OP 0x8a

/* MC_CMD_FILTER_OP_IN msgrequest */
#define    MC_CMD_FILTER_OP_IN_LEN 108
/* identifies the type of operation requested */
#define       MC_CMD_FILTER_OP_IN_OP_OFST 0
/* enum: single-recipient filter insert */
#define          MC_CMD_FILTER_OP_IN_OP_INSERT  0x0
/* enum: single-recipient filter remove */
#define          MC_CMD_FILTER_OP_IN_OP_REMOVE  0x1
/* enum: multi-recipient filter subscribe */
#define          MC_CMD_FILTER_OP_IN_OP_SUBSCRIBE  0x2
/* enum: multi-recipient filter unsubscribe */
#define          MC_CMD_FILTER_OP_IN_OP_UNSUBSCRIBE  0x3
/* enum: replace one recipient with another (warning - the filter handle may
 * change)
 */
#define          MC_CMD_FILTER_OP_IN_OP_REPLACE  0x4
/* filter handle (for remove / unsubscribe operations) */
#define       MC_CMD_FILTER_OP_IN_HANDLE_OFST 4
#define       MC_CMD_FILTER_OP_IN_HANDLE_LEN 8
#define       MC_CMD_FILTER_OP_IN_HANDLE_LO_OFST 4
#define       MC_CMD_FILTER_OP_IN_HANDLE_HI_OFST 8
/* The port ID associated with the v-adaptor which should contain this filter.
 */
#define       MC_CMD_FILTER_OP_IN_PORT_ID_OFST 12
/* fields to include in match criteria */
#define       MC_CMD_FILTER_OP_IN_MATCH_FIELDS_OFST 16
#define        MC_CMD_FILTER_OP_IN_MATCH_SRC_IP_LBN 0
#define        MC_CMD_FILTER_OP_IN_MATCH_SRC_IP_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_DST_IP_LBN 1
#define        MC_CMD_FILTER_OP_IN_MATCH_DST_IP_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_SRC_MAC_LBN 2
#define        MC_CMD_FILTER_OP_IN_MATCH_SRC_MAC_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_SRC_PORT_LBN 3
#define        MC_CMD_FILTER_OP_IN_MATCH_SRC_PORT_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_DST_MAC_LBN 4
#define        MC_CMD_FILTER_OP_IN_MATCH_DST_MAC_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_DST_PORT_LBN 5
#define        MC_CMD_FILTER_OP_IN_MATCH_DST_PORT_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_ETHER_TYPE_LBN 6
#define        MC_CMD_FILTER_OP_IN_MATCH_ETHER_TYPE_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_INNER_VLAN_LBN 7
#define        MC_CMD_FILTER_OP_IN_MATCH_INNER_VLAN_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_OUTER_VLAN_LBN 8
#define        MC_CMD_FILTER_OP_IN_MATCH_OUTER_VLAN_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_IP_PROTO_LBN 9
#define        MC_CMD_FILTER_OP_IN_MATCH_IP_PROTO_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_FWDEF0_LBN 10
#define        MC_CMD_FILTER_OP_IN_MATCH_FWDEF0_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_FWDEF1_LBN 11
#define        MC_CMD_FILTER_OP_IN_MATCH_FWDEF1_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_MCAST_DST_LBN 30
#define        MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_MCAST_DST_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_UCAST_DST_LBN 31
#define        MC_CMD_FILTER_OP_IN_MATCH_UNKNOWN_UCAST_DST_WIDTH 1
/* receive destination */
#define       MC_CMD_FILTER_OP_IN_RX_DEST_OFST 20
/* enum: drop packets */
#define          MC_CMD_FILTER_OP_IN_RX_DEST_DROP  0x0
/* enum: receive to host */
#define          MC_CMD_FILTER_OP_IN_RX_DEST_HOST  0x1
/* enum: receive to MC */
#define          MC_CMD_FILTER_OP_IN_RX_DEST_MC  0x2
/* enum: loop back to port 0 TX MAC */
#define          MC_CMD_FILTER_OP_IN_RX_DEST_TX0  0x3
/* enum: loop back to port 1 TX MAC */
#define          MC_CMD_FILTER_OP_IN_RX_DEST_TX1  0x4
/* receive queue handle (for multiple queue modes, this is the base queue) */
#define       MC_CMD_FILTER_OP_IN_RX_QUEUE_OFST 24
/* receive mode */
#define       MC_CMD_FILTER_OP_IN_RX_MODE_OFST 28
/* enum: receive to just the specified queue */
#define          MC_CMD_FILTER_OP_IN_RX_MODE_SIMPLE  0x0
/* enum: receive to multiple queues using RSS context */
#define          MC_CMD_FILTER_OP_IN_RX_MODE_RSS  0x1
/* enum: receive to multiple queues using .1p mapping */
#define          MC_CMD_FILTER_OP_IN_RX_MODE_DOT1P_MAPPING  0x2
/* enum: install a filter entry that will never match; for test purposes only
 */
#define          MC_CMD_FILTER_OP_IN_RX_MODE_TEST_NEVER_MATCH  0x80000000
/* RSS context (for RX_MODE_RSS) or .1p mapping handle (for
 * RX_MODE_DOT1P_MAPPING), as returned by MC_CMD_RSS_CONTEXT_ALLOC or
 * MC_CMD_DOT1P_MAPPING_ALLOC. Note that these handles should be considered
 * opaque to the host, although a value of 0xFFFFFFFF is guaranteed never to be
 * a valid handle.
 */
#define       MC_CMD_FILTER_OP_IN_RX_CONTEXT_OFST 32
/* transmit domain (reserved; set to 0) */
#define       MC_CMD_FILTER_OP_IN_TX_DOMAIN_OFST 36
/* transmit destination (either set the MAC and/or PM bits for explicit
 * control, or set this field to TX_DEST_DEFAULT for sensible default
 * behaviour)
 */
#define       MC_CMD_FILTER_OP_IN_TX_DEST_OFST 40
/* enum: request default behaviour (based on filter type) */
#define          MC_CMD_FILTER_OP_IN_TX_DEST_DEFAULT  0xffffffff
#define        MC_CMD_FILTER_OP_IN_TX_DEST_MAC_LBN 0
#define        MC_CMD_FILTER_OP_IN_TX_DEST_MAC_WIDTH 1
#define        MC_CMD_FILTER_OP_IN_TX_DEST_PM_LBN 1
#define        MC_CMD_FILTER_OP_IN_TX_DEST_PM_WIDTH 1
/* source MAC address to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_IN_SRC_MAC_OFST 44
#define       MC_CMD_FILTER_OP_IN_SRC_MAC_LEN 6
/* source port to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_IN_SRC_PORT_OFST 50
#define       MC_CMD_FILTER_OP_IN_SRC_PORT_LEN 2
/* destination MAC address to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_IN_DST_MAC_OFST 52
#define       MC_CMD_FILTER_OP_IN_DST_MAC_LEN 6
/* destination port to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_IN_DST_PORT_OFST 58
#define       MC_CMD_FILTER_OP_IN_DST_PORT_LEN 2
/* Ethernet type to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_IN_ETHER_TYPE_OFST 60
#define       MC_CMD_FILTER_OP_IN_ETHER_TYPE_LEN 2
/* Inner VLAN tag to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_IN_INNER_VLAN_OFST 62
#define       MC_CMD_FILTER_OP_IN_INNER_VLAN_LEN 2
/* Outer VLAN tag to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_IN_OUTER_VLAN_OFST 64
#define       MC_CMD_FILTER_OP_IN_OUTER_VLAN_LEN 2
/* IP protocol to match (in low byte; set high byte to 0) */
#define       MC_CMD_FILTER_OP_IN_IP_PROTO_OFST 66
#define       MC_CMD_FILTER_OP_IN_IP_PROTO_LEN 2
/* Firmware defined register 0 to match (reserved; set to 0) */
#define       MC_CMD_FILTER_OP_IN_FWDEF0_OFST 68
/* Firmware defined register 1 to match (reserved; set to 0) */
#define       MC_CMD_FILTER_OP_IN_FWDEF1_OFST 72
/* source IP address to match (as bytes in network order; set last 12 bytes to
 * 0 for IPv4 address)
 */
#define       MC_CMD_FILTER_OP_IN_SRC_IP_OFST 76
#define       MC_CMD_FILTER_OP_IN_SRC_IP_LEN 16
/* destination IP address to match (as bytes in network order; set last 12
 * bytes to 0 for IPv4 address)
 */
#define       MC_CMD_FILTER_OP_IN_DST_IP_OFST 92
#define       MC_CMD_FILTER_OP_IN_DST_IP_LEN 16

/* MC_CMD_FILTER_OP_OUT msgresponse */
#define    MC_CMD_FILTER_OP_OUT_LEN 12
/* identifies the type of operation requested */
#define       MC_CMD_FILTER_OP_OUT_OP_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_FILTER_OP_IN/OP */
/* Returned filter handle (for insert / subscribe operations). Note that these
 * handles should be considered opaque to the host, although a value of
 * 0xFFFFFFFF_FFFFFFFF is guaranteed never to be a valid handle.
 */
#define       MC_CMD_FILTER_OP_OUT_HANDLE_OFST 4
#define       MC_CMD_FILTER_OP_OUT_HANDLE_LEN 8
#define       MC_CMD_FILTER_OP_OUT_HANDLE_LO_OFST 4
#define       MC_CMD_FILTER_OP_OUT_HANDLE_HI_OFST 8


/***********************************/
/* MC_CMD_GET_PARSER_DISP_INFO
 * Get information related to the parser-dispatcher subsystem
 */
#define MC_CMD_GET_PARSER_DISP_INFO 0xe4

/* MC_CMD_GET_PARSER_DISP_INFO_IN msgrequest */
#define    MC_CMD_GET_PARSER_DISP_INFO_IN_LEN 4
/* identifies the type of operation requested */
#define       MC_CMD_GET_PARSER_DISP_INFO_IN_OP_OFST 0
/* enum: read the list of supported RX filter matches */
#define          MC_CMD_GET_PARSER_DISP_INFO_IN_OP_GET_SUPPORTED_RX_MATCHES  0x1

/* MC_CMD_GET_PARSER_DISP_INFO_OUT msgresponse */
#define    MC_CMD_GET_PARSER_DISP_INFO_OUT_LENMIN 8
#define    MC_CMD_GET_PARSER_DISP_INFO_OUT_LENMAX 252
#define    MC_CMD_GET_PARSER_DISP_INFO_OUT_LEN(num) (8+4*(num))
/* identifies the type of operation requested */
#define       MC_CMD_GET_PARSER_DISP_INFO_OUT_OP_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_GET_PARSER_DISP_INFO_IN/OP */
/* number of supported match types */
#define       MC_CMD_GET_PARSER_DISP_INFO_OUT_NUM_SUPPORTED_MATCHES_OFST 4
/* array of supported match types (valid MATCH_FIELDS values for
 * MC_CMD_FILTER_OP) sorted in decreasing priority order
 */
#define       MC_CMD_GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES_OFST 8
#define       MC_CMD_GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES_LEN 4
#define       MC_CMD_GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES_MINNUM 0
#define       MC_CMD_GET_PARSER_DISP_INFO_OUT_SUPPORTED_MATCHES_MAXNUM 61


/***********************************/
/* MC_CMD_PARSER_DISP_RW
 * Direct read/write of parser-dispatcher state (DICPUs and LUE) for debugging
 */
#define MC_CMD_PARSER_DISP_RW 0xe5

/* MC_CMD_PARSER_DISP_RW_IN msgrequest */
#define    MC_CMD_PARSER_DISP_RW_IN_LEN 32
/* identifies the target of the operation */
#define       MC_CMD_PARSER_DISP_RW_IN_TARGET_OFST 0
/* enum: RX dispatcher CPU */
#define          MC_CMD_PARSER_DISP_RW_IN_RX_DICPU  0x0
/* enum: TX dispatcher CPU */
#define          MC_CMD_PARSER_DISP_RW_IN_TX_DICPU  0x1
/* enum: Lookup engine */
#define          MC_CMD_PARSER_DISP_RW_IN_LUE  0x2
/* identifies the type of operation requested */
#define       MC_CMD_PARSER_DISP_RW_IN_OP_OFST 4
/* enum: read a word of DICPU DMEM or a LUE entry */
#define          MC_CMD_PARSER_DISP_RW_IN_READ  0x0
/* enum: write a word of DICPU DMEM or a LUE entry */
#define          MC_CMD_PARSER_DISP_RW_IN_WRITE  0x1
/* enum: read-modify-write a word of DICPU DMEM (not valid for LUE) */
#define          MC_CMD_PARSER_DISP_RW_IN_RMW  0x2
/* data memory address or LUE index */
#define       MC_CMD_PARSER_DISP_RW_IN_ADDRESS_OFST 8
/* value to write (for DMEM writes) */
#define       MC_CMD_PARSER_DISP_RW_IN_DMEM_WRITE_VALUE_OFST 12
/* XOR value (for DMEM read-modify-writes: new = (old & mask) ^ value) */
#define       MC_CMD_PARSER_DISP_RW_IN_DMEM_RMW_XOR_VALUE_OFST 12
/* AND mask (for DMEM read-modify-writes: new = (old & mask) ^ value) */
#define       MC_CMD_PARSER_DISP_RW_IN_DMEM_RMW_AND_MASK_OFST 16
/* value to write (for LUE writes) */
#define       MC_CMD_PARSER_DISP_RW_IN_LUE_WRITE_VALUE_OFST 12
#define       MC_CMD_PARSER_DISP_RW_IN_LUE_WRITE_VALUE_LEN 20

/* MC_CMD_PARSER_DISP_RW_OUT msgresponse */
#define    MC_CMD_PARSER_DISP_RW_OUT_LEN 52
/* value read (for DMEM reads) */
#define       MC_CMD_PARSER_DISP_RW_OUT_DMEM_READ_VALUE_OFST 0
/* value read (for LUE reads) */
#define       MC_CMD_PARSER_DISP_RW_OUT_LUE_READ_VALUE_OFST 0
#define       MC_CMD_PARSER_DISP_RW_OUT_LUE_READ_VALUE_LEN 20
/* up to 8 32-bit words of additional soft state from the LUE manager (the
 * exact content is firmware-dependent and intended only for debug use)
 */
#define       MC_CMD_PARSER_DISP_RW_OUT_LUE_MGR_STATE_OFST 20
#define       MC_CMD_PARSER_DISP_RW_OUT_LUE_MGR_STATE_LEN 32


/***********************************/
/* MC_CMD_GET_PF_COUNT
 * Get number of PFs on the device.
 */
#define MC_CMD_GET_PF_COUNT 0xb6

/* MC_CMD_GET_PF_COUNT_IN msgrequest */
#define    MC_CMD_GET_PF_COUNT_IN_LEN 0

/* MC_CMD_GET_PF_COUNT_OUT msgresponse */
#define    MC_CMD_GET_PF_COUNT_OUT_LEN 1
/* Identifies the number of PFs on the device. */
#define       MC_CMD_GET_PF_COUNT_OUT_PF_COUNT_OFST 0
#define       MC_CMD_GET_PF_COUNT_OUT_PF_COUNT_LEN 1


/***********************************/
/* MC_CMD_SET_PF_COUNT
 * Set number of PFs on the device.
 */
#define MC_CMD_SET_PF_COUNT 0xb7

/* MC_CMD_SET_PF_COUNT_IN msgrequest */
#define    MC_CMD_SET_PF_COUNT_IN_LEN 4
/* New number of PFs on the device. */
#define       MC_CMD_SET_PF_COUNT_IN_PF_COUNT_OFST 0

/* MC_CMD_SET_PF_COUNT_OUT msgresponse */
#define    MC_CMD_SET_PF_COUNT_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_PORT_ASSIGNMENT
 * Get port assignment for current PCI function.
 */
#define MC_CMD_GET_PORT_ASSIGNMENT 0xb8

/* MC_CMD_GET_PORT_ASSIGNMENT_IN msgrequest */
#define    MC_CMD_GET_PORT_ASSIGNMENT_IN_LEN 0

/* MC_CMD_GET_PORT_ASSIGNMENT_OUT msgresponse */
#define    MC_CMD_GET_PORT_ASSIGNMENT_OUT_LEN 4
/* Identifies the port assignment for this function. */
#define       MC_CMD_GET_PORT_ASSIGNMENT_OUT_PORT_OFST 0


/***********************************/
/* MC_CMD_SET_PORT_ASSIGNMENT
 * Set port assignment for current PCI function.
 */
#define MC_CMD_SET_PORT_ASSIGNMENT 0xb9

/* MC_CMD_SET_PORT_ASSIGNMENT_IN msgrequest */
#define    MC_CMD_SET_PORT_ASSIGNMENT_IN_LEN 4
/* Identifies the port assignment for this function. */
#define       MC_CMD_SET_PORT_ASSIGNMENT_IN_PORT_OFST 0

/* MC_CMD_SET_PORT_ASSIGNMENT_OUT msgresponse */
#define    MC_CMD_SET_PORT_ASSIGNMENT_OUT_LEN 0


/***********************************/
/* MC_CMD_ALLOC_VIS
 * Allocate VIs for current PCI function.
 */
#define MC_CMD_ALLOC_VIS 0x8b

/* MC_CMD_ALLOC_VIS_IN msgrequest */
#define    MC_CMD_ALLOC_VIS_IN_LEN 8
/* The minimum number of VIs that is acceptable */
#define       MC_CMD_ALLOC_VIS_IN_MIN_VI_COUNT_OFST 0
/* The maximum number of VIs that would be useful */
#define       MC_CMD_ALLOC_VIS_IN_MAX_VI_COUNT_OFST 4

/* MC_CMD_ALLOC_VIS_OUT msgresponse */
#define    MC_CMD_ALLOC_VIS_OUT_LEN 8
/* The number of VIs allocated on this function */
#define       MC_CMD_ALLOC_VIS_OUT_VI_COUNT_OFST 0
/* The base absolute VI number allocated to this function. Required to
 * correctly interpret wakeup events.
 */
#define       MC_CMD_ALLOC_VIS_OUT_VI_BASE_OFST 4


/***********************************/
/* MC_CMD_FREE_VIS
 * Free VIs for current PCI function. Any linked PIO buffers will be unlinked,
 * but not freed.
 */
#define MC_CMD_FREE_VIS 0x8c

/* MC_CMD_FREE_VIS_IN msgrequest */
#define    MC_CMD_FREE_VIS_IN_LEN 0

/* MC_CMD_FREE_VIS_OUT msgresponse */
#define    MC_CMD_FREE_VIS_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_SRIOV_CFG
 * Get SRIOV config for this PF.
 */
#define MC_CMD_GET_SRIOV_CFG 0xba

/* MC_CMD_GET_SRIOV_CFG_IN msgrequest */
#define    MC_CMD_GET_SRIOV_CFG_IN_LEN 0

/* MC_CMD_GET_SRIOV_CFG_OUT msgresponse */
#define    MC_CMD_GET_SRIOV_CFG_OUT_LEN 20
/* Number of VFs currently enabled. */
#define       MC_CMD_GET_SRIOV_CFG_OUT_VF_CURRENT_OFST 0
/* Max number of VFs before sriov stride and offset may need to be changed. */
#define       MC_CMD_GET_SRIOV_CFG_OUT_VF_MAX_OFST 4
#define       MC_CMD_GET_SRIOV_CFG_OUT_FLAGS_OFST 8
#define        MC_CMD_GET_SRIOV_CFG_OUT_VF_ENABLED_LBN 0
#define        MC_CMD_GET_SRIOV_CFG_OUT_VF_ENABLED_WIDTH 1
/* RID offset of first VF from PF. */
#define       MC_CMD_GET_SRIOV_CFG_OUT_VF_OFFSET_OFST 12
/* RID offset of each subsequent VF from the previous. */
#define       MC_CMD_GET_SRIOV_CFG_OUT_VF_STRIDE_OFST 16


/***********************************/
/* MC_CMD_SET_SRIOV_CFG
 * Set SRIOV config for this PF.
 */
#define MC_CMD_SET_SRIOV_CFG 0xbb

/* MC_CMD_SET_SRIOV_CFG_IN msgrequest */
#define    MC_CMD_SET_SRIOV_CFG_IN_LEN 20
/* Number of VFs currently enabled. */
#define       MC_CMD_SET_SRIOV_CFG_IN_VF_CURRENT_OFST 0
/* Max number of VFs before sriov stride and offset may need to be changed. */
#define       MC_CMD_SET_SRIOV_CFG_IN_VF_MAX_OFST 4
#define       MC_CMD_SET_SRIOV_CFG_IN_FLAGS_OFST 8
#define        MC_CMD_SET_SRIOV_CFG_IN_VF_ENABLED_LBN 0
#define        MC_CMD_SET_SRIOV_CFG_IN_VF_ENABLED_WIDTH 1
/* RID offset of first VF from PF, or 0 for no change, or
 * MC_CMD_RESOURCE_INSTANCE_ANY to allow the system to allocate an offset.
 */
#define       MC_CMD_SET_SRIOV_CFG_IN_VF_OFFSET_OFST 12
/* RID offset of each subsequent VF from the previous, 0 for no change, or
 * MC_CMD_RESOURCE_INSTANCE_ANY to allow the system to allocate a stride.
 */
#define       MC_CMD_SET_SRIOV_CFG_IN_VF_STRIDE_OFST 16

/* MC_CMD_SET_SRIOV_CFG_OUT msgresponse */
#define    MC_CMD_SET_SRIOV_CFG_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_VI_ALLOC_INFO
 * Get information about number of VI's and base VI number allocated to this
 * function.
 */
#define MC_CMD_GET_VI_ALLOC_INFO 0x8d

/* MC_CMD_GET_VI_ALLOC_INFO_IN msgrequest */
#define    MC_CMD_GET_VI_ALLOC_INFO_IN_LEN 0

/* MC_CMD_GET_VI_ALLOC_INFO_OUT msgresponse */
#define    MC_CMD_GET_VI_ALLOC_INFO_OUT_LEN 8
/* The number of VIs allocated on this function */
#define       MC_CMD_GET_VI_ALLOC_INFO_OUT_VI_COUNT_OFST 0
/* The base absolute VI number allocated to this function. Required to
 * correctly interpret wakeup events.
 */
#define       MC_CMD_GET_VI_ALLOC_INFO_OUT_VI_BASE_OFST 4


/***********************************/
/* MC_CMD_DUMP_VI_STATE
 * For CmdClient use. Dump pertinent information on a specific absolute VI.
 */
#define MC_CMD_DUMP_VI_STATE 0x8e

/* MC_CMD_DUMP_VI_STATE_IN msgrequest */
#define    MC_CMD_DUMP_VI_STATE_IN_LEN 4
/* The VI number to query. */
#define       MC_CMD_DUMP_VI_STATE_IN_VI_NUMBER_OFST 0

/* MC_CMD_DUMP_VI_STATE_OUT msgresponse */
#define    MC_CMD_DUMP_VI_STATE_OUT_LEN 96
/* The PF part of the function owning this VI. */
#define       MC_CMD_DUMP_VI_STATE_OUT_OWNER_PF_OFST 0
#define       MC_CMD_DUMP_VI_STATE_OUT_OWNER_PF_LEN 2
/* The VF part of the function owning this VI. */
#define       MC_CMD_DUMP_VI_STATE_OUT_OWNER_VF_OFST 2
#define       MC_CMD_DUMP_VI_STATE_OUT_OWNER_VF_LEN 2
/* Base of VIs allocated to this function. */
#define       MC_CMD_DUMP_VI_STATE_OUT_FUNC_VI_BASE_OFST 4
#define       MC_CMD_DUMP_VI_STATE_OUT_FUNC_VI_BASE_LEN 2
/* Count of VIs allocated to the owner function. */
#define       MC_CMD_DUMP_VI_STATE_OUT_FUNC_VI_COUNT_OFST 6
#define       MC_CMD_DUMP_VI_STATE_OUT_FUNC_VI_COUNT_LEN 2
/* Base interrupt vector allocated to this function. */
#define       MC_CMD_DUMP_VI_STATE_OUT_FUNC_VECTOR_BASE_OFST 8
#define       MC_CMD_DUMP_VI_STATE_OUT_FUNC_VECTOR_BASE_LEN 2
/* Number of interrupt vectors allocated to this function. */
#define       MC_CMD_DUMP_VI_STATE_OUT_FUNC_VECTOR_COUNT_OFST 10
#define       MC_CMD_DUMP_VI_STATE_OUT_FUNC_VECTOR_COUNT_LEN 2
/* Raw evq ptr table data. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EVQ_PTR_RAW_OFST 12
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EVQ_PTR_RAW_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EVQ_PTR_RAW_LO_OFST 12
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EVQ_PTR_RAW_HI_OFST 16
/* Raw evq timer table data. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EV_TIMER_RAW_OFST 20
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EV_TIMER_RAW_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EV_TIMER_RAW_LO_OFST 20
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EV_TIMER_RAW_HI_OFST 24
/* Combined metadata field. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_EV_META_OFST 28
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_EV_META_BUFS_BASE_LBN 0
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_EV_META_BUFS_BASE_WIDTH 16
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_EV_META_BUFS_NPAGES_LBN 16
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_EV_META_BUFS_NPAGES_WIDTH 8
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_EV_META_WKUP_REF_LBN 24
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_EV_META_WKUP_REF_WIDTH 8
/* TXDPCPU raw table data for queue. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_0_OFST 32
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_0_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_0_LO_OFST 32
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_0_HI_OFST 36
/* TXDPCPU raw table data for queue. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_1_OFST 40
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_1_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_1_LO_OFST 40
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_1_HI_OFST 44
/* TXDPCPU raw table data for queue. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_2_OFST 48
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_2_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_2_LO_OFST 48
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_RAW_TBL_2_HI_OFST 52
/* Combined metadata field. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_OFST 56
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_LO_OFST 56
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_HI_OFST 60
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_BUFS_BASE_LBN 0
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_BUFS_BASE_WIDTH 16
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_BUFS_NPAGES_LBN 16
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_BUFS_NPAGES_WIDTH 8
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_QSTATE_LBN 24
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_QSTATE_WIDTH 8
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_WAITCOUNT_LBN 32
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_TX_META_WAITCOUNT_WIDTH 8
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_PADDING_LBN 40
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_PADDING_WIDTH 24
/* RXDPCPU raw table data for queue. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_0_OFST 64
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_0_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_0_LO_OFST 64
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_0_HI_OFST 68
/* RXDPCPU raw table data for queue. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_1_OFST 72
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_1_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_1_LO_OFST 72
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_1_HI_OFST 76
/* Reserved, currently 0. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_2_OFST 80
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_2_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_2_LO_OFST 80
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_RAW_TBL_2_HI_OFST 84
/* Combined metadata field. */
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_OFST 88
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_LEN 8
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_LO_OFST 88
#define       MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_HI_OFST 92
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_BUFS_BASE_LBN 0
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_BUFS_BASE_WIDTH 16
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_BUFS_NPAGES_LBN 16
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_BUFS_NPAGES_WIDTH 8
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_QSTATE_LBN 24
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_QSTATE_WIDTH 8
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_WAITCOUNT_LBN 32
#define        MC_CMD_DUMP_VI_STATE_OUT_VI_RX_META_WAITCOUNT_WIDTH 8


/***********************************/
/* MC_CMD_ALLOC_PIOBUF
 * Allocate a push I/O buffer for later use with a tx queue.
 */
#define MC_CMD_ALLOC_PIOBUF 0x8f

/* MC_CMD_ALLOC_PIOBUF_IN msgrequest */
#define    MC_CMD_ALLOC_PIOBUF_IN_LEN 0

/* MC_CMD_ALLOC_PIOBUF_OUT msgresponse */
#define    MC_CMD_ALLOC_PIOBUF_OUT_LEN 4
/* Handle for allocated push I/O buffer. */
#define       MC_CMD_ALLOC_PIOBUF_OUT_PIOBUF_HANDLE_OFST 0


/***********************************/
/* MC_CMD_FREE_PIOBUF
 * Free a push I/O buffer.
 */
#define MC_CMD_FREE_PIOBUF 0x90

/* MC_CMD_FREE_PIOBUF_IN msgrequest */
#define    MC_CMD_FREE_PIOBUF_IN_LEN 4
/* Handle for allocated push I/O buffer. */
#define       MC_CMD_FREE_PIOBUF_IN_PIOBUF_HANDLE_OFST 0

/* MC_CMD_FREE_PIOBUF_OUT msgresponse */
#define    MC_CMD_FREE_PIOBUF_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_VI_TLP_PROCESSING
 * Get TLP steering and ordering information for a VI.
 */
#define MC_CMD_GET_VI_TLP_PROCESSING 0xb0

/* MC_CMD_GET_VI_TLP_PROCESSING_IN msgrequest */
#define    MC_CMD_GET_VI_TLP_PROCESSING_IN_LEN 4
/* VI number to get information for. */
#define       MC_CMD_GET_VI_TLP_PROCESSING_IN_INSTANCE_OFST 0

/* MC_CMD_GET_VI_TLP_PROCESSING_OUT msgresponse */
#define    MC_CMD_GET_VI_TLP_PROCESSING_OUT_LEN 4
/* Transaction processing steering hint 1 for use with the Rx Queue. */
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_TPH_TAG1_RX_OFST 0
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_TPH_TAG1_RX_LEN 1
/* Transaction processing steering hint 2 for use with the Ev Queue. */
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_TPH_TAG2_EV_OFST 1
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_TPH_TAG2_EV_LEN 1
/* Use Relaxed ordering model for TLPs on this VI. */
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_RELAXED_ORDERING_LBN 16
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_RELAXED_ORDERING_WIDTH 1
/* Use ID based ordering for TLPs on this VI. */
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_ID_BASED_ORDERING_LBN 17
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_ID_BASED_ORDERING_WIDTH 1
/* Set no snoop bit for TLPs on this VI. */
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_NO_SNOOP_LBN 18
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_NO_SNOOP_WIDTH 1
/* Enable TPH for TLPs on this VI. */
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_TPH_ON_LBN 19
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_TPH_ON_WIDTH 1
#define       MC_CMD_GET_VI_TLP_PROCESSING_OUT_DATA_OFST 0


/***********************************/
/* MC_CMD_SET_VI_TLP_PROCESSING
 * Set TLP steering and ordering information for a VI.
 */
#define MC_CMD_SET_VI_TLP_PROCESSING 0xb1

/* MC_CMD_SET_VI_TLP_PROCESSING_IN msgrequest */
#define    MC_CMD_SET_VI_TLP_PROCESSING_IN_LEN 8
/* VI number to set information for. */
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_INSTANCE_OFST 0
/* Transaction processing steering hint 1 for use with the Rx Queue. */
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_TPH_TAG1_RX_OFST 4
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_TPH_TAG1_RX_LEN 1
/* Transaction processing steering hint 2 for use with the Ev Queue. */
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_TPH_TAG2_EV_OFST 5
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_TPH_TAG2_EV_LEN 1
/* Use Relaxed ordering model for TLPs on this VI. */
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_RELAXED_ORDERING_LBN 48
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_RELAXED_ORDERING_WIDTH 1
/* Use ID based ordering for TLPs on this VI. */
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_ID_BASED_ORDERING_LBN 49
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_ID_BASED_ORDERING_WIDTH 1
/* Set the no snoop bit for TLPs on this VI. */
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_NO_SNOOP_LBN 50
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_NO_SNOOP_WIDTH 1
/* Enable TPH for TLPs on this VI. */
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_TPH_ON_LBN 51
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_TPH_ON_WIDTH 1
#define       MC_CMD_SET_VI_TLP_PROCESSING_IN_DATA_OFST 4

/* MC_CMD_SET_VI_TLP_PROCESSING_OUT msgresponse */
#define    MC_CMD_SET_VI_TLP_PROCESSING_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_TLP_PROCESSING_GLOBALS
 * Get global PCIe steering and transaction processing configuration.
 */
#define MC_CMD_GET_TLP_PROCESSING_GLOBALS 0xbc

/* MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN msgrequest */
#define    MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN_LEN 4
#define       MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN_TLP_GLOBAL_CATEGORY_OFST 0
/* enum: MISC. */
#define          MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN_TLP_GLOBAL_CATEGORY_MISC  0x0
/* enum: IDO. */
#define          MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN_TLP_GLOBAL_CATEGORY_IDO  0x1
/* enum: RO. */
#define          MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN_TLP_GLOBAL_CATEGORY_RO  0x2
/* enum: TPH Type. */
#define          MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN_TLP_GLOBAL_CATEGORY_TPH_TYPE  0x3

/* MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT msgresponse */
#define    MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_LEN 8
#define       MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_GLOBAL_CATEGORY_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN/TLP_GLOBAL_CATEGORY */
/* Amalgamated TLP info word. */
#define       MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_WORD_OFST 4
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_MISC_WTAG_EN_LBN 0
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_MISC_WTAG_EN_WIDTH 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_MISC_SPARE_LBN 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_MISC_SPARE_WIDTH 31
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_DL_EN_LBN 0
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_DL_EN_WIDTH 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_TX_EN_LBN 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_TX_EN_WIDTH 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_EV_EN_LBN 2
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_EV_EN_WIDTH 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_RX_EN_LBN 3
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_RX_EN_WIDTH 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_SPARE_LBN 4
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_IDO_SPARE_WIDTH 28
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_RO_RXDMA_EN_LBN 0
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_RO_RXDMA_EN_WIDTH 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_RO_TXDMA_EN_LBN 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_RO_TXDMA_EN_WIDTH 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_RO_DL_EN_LBN 2
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_RO_DL_EN_WIDTH 1
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_RO_SPARE_LBN 3
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_RO_SPARE_WIDTH 29
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_MSIX_LBN 0
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_MSIX_WIDTH 2
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_DL_LBN 2
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_DL_WIDTH 2
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_TX_LBN 4
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_TX_WIDTH 2
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_EV_LBN 6
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_EV_WIDTH 2
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_RX_LBN 8
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TPH_TYPE_RX_WIDTH 2
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TLP_TYPE_SPARE_LBN 9
#define        MC_CMD_GET_TLP_PROCESSING_GLOBALS_OUT_TLP_INFO_TLP_TYPE_SPARE_WIDTH 23


/***********************************/
/* MC_CMD_SET_TLP_PROCESSING_GLOBALS
 * Set global PCIe steering and transaction processing configuration.
 */
#define MC_CMD_SET_TLP_PROCESSING_GLOBALS 0xbd

/* MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN msgrequest */
#define    MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_LEN 8
#define       MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_GLOBAL_CATEGORY_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_GET_TLP_PROCESSING_GLOBALS/MC_CMD_GET_TLP_PROCESSING_GLOBALS_IN/TLP_GLOBAL_CATEGORY */
/* Amalgamated TLP info word. */
#define       MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_WORD_OFST 4
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_MISC_WTAG_EN_LBN 0
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_MISC_WTAG_EN_WIDTH 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_IDO_DL_EN_LBN 0
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_IDO_DL_EN_WIDTH 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_IDO_TX_EN_LBN 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_IDO_TX_EN_WIDTH 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_IDO_EV_EN_LBN 2
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_IDO_EV_EN_WIDTH 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_IDO_RX_EN_LBN 3
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_IDO_RX_EN_WIDTH 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_RO_RXDMA_EN_LBN 0
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_RO_RXDMA_EN_WIDTH 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_RO_TXDMA_EN_LBN 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_RO_TXDMA_EN_WIDTH 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_RO_DL_EN_LBN 2
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_RO_DL_EN_WIDTH 1
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_MSIX_LBN 0
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_MSIX_WIDTH 2
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_DL_LBN 2
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_DL_WIDTH 2
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_TX_LBN 4
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_TX_WIDTH 2
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_EV_LBN 6
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_EV_WIDTH 2
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_RX_LBN 8
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_TPH_TYPE_RX_WIDTH 2
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_SPARE_LBN 10
#define        MC_CMD_SET_TLP_PROCESSING_GLOBALS_IN_TLP_INFO_SPARE_WIDTH 22

/* MC_CMD_SET_TLP_PROCESSING_GLOBALS_OUT msgresponse */
#define    MC_CMD_SET_TLP_PROCESSING_GLOBALS_OUT_LEN 0


/***********************************/
/* MC_CMD_SATELLITE_DOWNLOAD
 * Download a new set of images to the satellite CPUs from the host.
 */
#define MC_CMD_SATELLITE_DOWNLOAD 0x91

/* MC_CMD_SATELLITE_DOWNLOAD_IN msgrequest: The reset requirements for the CPUs
 * are subtle, and so downloads must proceed in a number of phases.
 *
 * 1) PHASE_RESET with a target of TARGET_ALL and chunk ID/length of 0.
 *
 * 2) PHASE_IMEMS for each of the IMEM targets (target IDs 0-11). Each download
 * may consist of multiple chunks. The final chunk (with CHUNK_ID_LAST) should
 * be a checksum (a simple 32-bit sum) of the transferred data. An individual
 * download may be aborted using CHUNK_ID_ABORT.
 *
 * 3) PHASE_VECTORS for each of the vector table targets (target IDs 12-15),
 * similar to PHASE_IMEMS.
 *
 * 4) PHASE_READY with a target of TARGET_ALL and chunk ID/length of 0.
 *
 * After any error (a requested abort is not considered to be an error) the
 * sequence must be restarted from PHASE_RESET.
 */
#define    MC_CMD_SATELLITE_DOWNLOAD_IN_LENMIN 20
#define    MC_CMD_SATELLITE_DOWNLOAD_IN_LENMAX 252
#define    MC_CMD_SATELLITE_DOWNLOAD_IN_LEN(num) (16+4*(num))
/* Download phase. (Note: the IDLE phase is used internally and is never valid
 * in a command from the host.)
 */
#define       MC_CMD_SATELLITE_DOWNLOAD_IN_PHASE_OFST 0
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_PHASE_IDLE     0x0 /* enum */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_PHASE_RESET    0x1 /* enum */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_PHASE_IMEMS    0x2 /* enum */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_PHASE_VECTORS  0x3 /* enum */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_PHASE_READY    0x4 /* enum */
/* Target for download. (These match the blob numbers defined in
 * mc_flash_layout.h.)
 */
#define       MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_OFST 4
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_TXDI_TEXT  0x0
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_RXDI_TEXT  0x1
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_TXDP_TEXT  0x2
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_RXDP_TEXT  0x3
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_RXHRSL_HR_LUT  0x4
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_RXHRSL_HR_LUT_CFG  0x5
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_TXHRSL_HR_LUT  0x6
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_TXHRSL_HR_LUT_CFG  0x7
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_RXHRSL_HR_PGM  0x8
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_RXHRSL_SL_PGM  0x9
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_TXHRSL_HR_PGM  0xa
/* enum: Valid in phase 2 (PHASE_IMEMS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_TXHRSL_SL_PGM  0xb
/* enum: Valid in phase 3 (PHASE_VECTORS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_RXDI_VTBL0  0xc
/* enum: Valid in phase 3 (PHASE_VECTORS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_TXDI_VTBL0  0xd
/* enum: Valid in phase 3 (PHASE_VECTORS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_RXDI_VTBL1  0xe
/* enum: Valid in phase 3 (PHASE_VECTORS) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_TXDI_VTBL1  0xf
/* enum: Valid in phases 1 (PHASE_RESET) and 4 (PHASE_READY) only */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_TARGET_ALL  0xffffffff
/* Chunk ID, or CHUNK_ID_LAST or CHUNK_ID_ABORT */
#define       MC_CMD_SATELLITE_DOWNLOAD_IN_CHUNK_ID_OFST 8
/* enum: Last chunk, containing checksum rather than data */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_CHUNK_ID_LAST  0xffffffff
/* enum: Abort download of this item */
#define          MC_CMD_SATELLITE_DOWNLOAD_IN_CHUNK_ID_ABORT  0xfffffffe
/* Length of this chunk in bytes */
#define       MC_CMD_SATELLITE_DOWNLOAD_IN_CHUNK_LEN_OFST 12
/* Data for this chunk */
#define       MC_CMD_SATELLITE_DOWNLOAD_IN_CHUNK_DATA_OFST 16
#define       MC_CMD_SATELLITE_DOWNLOAD_IN_CHUNK_DATA_LEN 4
#define       MC_CMD_SATELLITE_DOWNLOAD_IN_CHUNK_DATA_MINNUM 1
#define       MC_CMD_SATELLITE_DOWNLOAD_IN_CHUNK_DATA_MAXNUM 59

/* MC_CMD_SATELLITE_DOWNLOAD_OUT msgresponse */
#define    MC_CMD_SATELLITE_DOWNLOAD_OUT_LEN 8
/* Same as MC_CMD_ERR field, but included as 0 in success cases */
#define       MC_CMD_SATELLITE_DOWNLOAD_OUT_RESULT_OFST 0
/* Extra status information */
#define       MC_CMD_SATELLITE_DOWNLOAD_OUT_INFO_OFST 4
/* enum: Code download OK, completed. */
#define          MC_CMD_SATELLITE_DOWNLOAD_OUT_OK_COMPLETE  0x0
/* enum: Code download aborted as requested. */
#define          MC_CMD_SATELLITE_DOWNLOAD_OUT_OK_ABORTED  0x1
/* enum: Code download OK so far, send next chunk. */
#define          MC_CMD_SATELLITE_DOWNLOAD_OUT_OK_NEXT_CHUNK  0x2
/* enum: Download phases out of sequence */
#define          MC_CMD_SATELLITE_DOWNLOAD_OUT_ERR_BAD_PHASE  0x100
/* enum: Bad target for this phase */
#define          MC_CMD_SATELLITE_DOWNLOAD_OUT_ERR_BAD_TARGET  0x101
/* enum: Chunk ID out of sequence */
#define          MC_CMD_SATELLITE_DOWNLOAD_OUT_ERR_BAD_CHUNK_ID  0x200
/* enum: Chunk length zero or too large */
#define          MC_CMD_SATELLITE_DOWNLOAD_OUT_ERR_BAD_CHUNK_LEN  0x201
/* enum: Checksum was incorrect */
#define          MC_CMD_SATELLITE_DOWNLOAD_OUT_ERR_BAD_CHECKSUM  0x300


/***********************************/
/* MC_CMD_GET_CAPABILITIES
 * Get device capabilities.
 *
 * This is supplementary to the MC_CMD_GET_BOARD_CFG command, and intended to
 * reference inherent device capabilities as opposed to current NVRAM config.
 */
#define MC_CMD_GET_CAPABILITIES 0xbe

/* MC_CMD_GET_CAPABILITIES_IN msgrequest */
#define    MC_CMD_GET_CAPABILITIES_IN_LEN 0

/* MC_CMD_GET_CAPABILITIES_OUT msgresponse */
#define    MC_CMD_GET_CAPABILITIES_OUT_LEN 20
/* First word of flags. */
#define       MC_CMD_GET_CAPABILITIES_OUT_FLAGS1_OFST 0
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_VLAN_INSERTION_LBN 19
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_VLAN_INSERTION_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_VLAN_STRIPPING_LBN 20
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_VLAN_STRIPPING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_TSO_LBN 21
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_TSO_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_PREFIX_LEN_0_LBN 22
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_PREFIX_LEN_0_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_PREFIX_LEN_14_LBN 23
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_PREFIX_LEN_14_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_TIMESTAMP_LBN 24
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_TIMESTAMP_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_BATCHING_LBN 25
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_BATCHING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_MCAST_FILTER_CHAINING_LBN 26
#define        MC_CMD_GET_CAPABILITIES_OUT_MCAST_FILTER_CHAINING_WIDTH 1
/* RxDPCPU firmware id. */
#define       MC_CMD_GET_CAPABILITIES_OUT_RX_DPCPU_FW_ID_OFST 4
#define       MC_CMD_GET_CAPABILITIES_OUT_RX_DPCPU_FW_ID_LEN 2
/* enum: Standard RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP  0x0
/* enum: Low latency RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_LOW_LATENCY  0x1
/* enum: RXDP Test firmware image 1 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_FW_TO_MC_CUT_THROUGH  0x101
/* enum: RXDP Test firmware image 2 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_FW_TO_MC_STORE_FORWARD  0x102
/* enum: RXDP Test firmware image 3 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_FW_TO_MC_STORE_FORWARD_FIRST  0x103
/* enum: RXDP Test firmware image 4 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_EVERY_EVENT_BATCHABLE  0x104
/* enum: RXDP Test firmware image 5 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_BACKPRESSURE  0x105
/* enum: RXDP Test firmware image 6 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_FW_PACKET_EDITS  0x106
/* enum: RXDP Test firmware image 7 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_FW_RX_HDR_SPLIT  0x107
/* enum: RXDP Test firmware image 8 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_FW_DISABLE_DL  0x108
/* TxDPCPU firmware id. */
#define       MC_CMD_GET_CAPABILITIES_OUT_TX_DPCPU_FW_ID_OFST 6
#define       MC_CMD_GET_CAPABILITIES_OUT_TX_DPCPU_FW_ID_LEN 2
/* enum: Standard TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP  0x0
/* enum: Low latency TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_LOW_LATENCY  0x1
/* enum: TXDP Test firmware image 1 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_TEST_FW_TSO_EDIT  0x101
/* enum: TXDP Test firmware image 2 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_TEST_FW_PACKET_EDITS  0x102
#define       MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_OFST 8
#define       MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_LEN 2
#define        MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_REV_LBN 0
#define        MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_REV_WIDTH 12
#define        MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_TYPE_LBN 12
#define        MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_TYPE_WIDTH 4
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_FIRST_PKT  0x1 /* enum */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_SIENA_COMPAT  0x2 /* enum */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_VSWITCH  0x3 /* enum */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_SIENA_COMPAT_PM  0x4 /* enum */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_LOW_LATENCY  0x5 /* enum */
#define       MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_OFST 10
#define       MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_LEN 2
#define        MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_REV_LBN 0
#define        MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_REV_WIDTH 12
#define        MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_TYPE_LBN 12
#define        MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_TYPE_WIDTH 4
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_FIRST_PKT  0x1 /* enum */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_SIENA_COMPAT  0x2 /* enum */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_VSWITCH  0x3 /* enum */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_SIENA_COMPAT_PM  0x4 /* enum */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_LOW_LATENCY  0x5 /* enum */
/* Hardware capabilities of NIC */
#define       MC_CMD_GET_CAPABILITIES_OUT_HW_CAPABILITIES_OFST 12
/* Licensed capabilities */
#define       MC_CMD_GET_CAPABILITIES_OUT_LICENSE_CAPABILITIES_OFST 16


/***********************************/
/* MC_CMD_V2_EXTN
 * Encapsulation for a v2 extended command
 */
#define MC_CMD_V2_EXTN 0x7f

/* MC_CMD_V2_EXTN_IN msgrequest */
#define    MC_CMD_V2_EXTN_IN_LEN 4
/* the extended command number */
#define       MC_CMD_V2_EXTN_IN_EXTENDED_CMD_LBN 0
#define       MC_CMD_V2_EXTN_IN_EXTENDED_CMD_WIDTH 15
#define       MC_CMD_V2_EXTN_IN_UNUSED_LBN 15
#define       MC_CMD_V2_EXTN_IN_UNUSED_WIDTH 1
/* the actual length of the encapsulated command (which is not in the v1
 * header)
 */
#define       MC_CMD_V2_EXTN_IN_ACTUAL_LEN_LBN 16
#define       MC_CMD_V2_EXTN_IN_ACTUAL_LEN_WIDTH 10
#define       MC_CMD_V2_EXTN_IN_UNUSED2_LBN 26
#define       MC_CMD_V2_EXTN_IN_UNUSED2_WIDTH 6


/***********************************/
/* MC_CMD_TCM_BUCKET_ALLOC
 * Allocate a pacer bucket (for qau rp or a snapper test)
 */
#define MC_CMD_TCM_BUCKET_ALLOC 0xb2

/* MC_CMD_TCM_BUCKET_ALLOC_IN msgrequest */
#define    MC_CMD_TCM_BUCKET_ALLOC_IN_LEN 0

/* MC_CMD_TCM_BUCKET_ALLOC_OUT msgresponse */
#define    MC_CMD_TCM_BUCKET_ALLOC_OUT_LEN 4
/* the bucket id */
#define       MC_CMD_TCM_BUCKET_ALLOC_OUT_BUCKET_OFST 0


/***********************************/
/* MC_CMD_TCM_BUCKET_FREE
 * Free a pacer bucket
 */
#define MC_CMD_TCM_BUCKET_FREE 0xb3

/* MC_CMD_TCM_BUCKET_FREE_IN msgrequest */
#define    MC_CMD_TCM_BUCKET_FREE_IN_LEN 4
/* the bucket id */
#define       MC_CMD_TCM_BUCKET_FREE_IN_BUCKET_OFST 0

/* MC_CMD_TCM_BUCKET_FREE_OUT msgresponse */
#define    MC_CMD_TCM_BUCKET_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_TCM_BUCKET_INIT
 * Initialise pacer bucket with a given rate
 */
#define MC_CMD_TCM_BUCKET_INIT 0xb4

/* MC_CMD_TCM_BUCKET_INIT_IN msgrequest */
#define    MC_CMD_TCM_BUCKET_INIT_IN_LEN 8
/* the bucket id */
#define       MC_CMD_TCM_BUCKET_INIT_IN_BUCKET_OFST 0
/* the rate in mbps */
#define       MC_CMD_TCM_BUCKET_INIT_IN_RATE_OFST 4

/* MC_CMD_TCM_BUCKET_INIT_OUT msgresponse */
#define    MC_CMD_TCM_BUCKET_INIT_OUT_LEN 0


/***********************************/
/* MC_CMD_TCM_TXQ_INIT
 * Initialise txq in pacer with given options or set options
 */
#define MC_CMD_TCM_TXQ_INIT 0xb5

/* MC_CMD_TCM_TXQ_INIT_IN msgrequest */
#define    MC_CMD_TCM_TXQ_INIT_IN_LEN 28
/* the txq id */
#define       MC_CMD_TCM_TXQ_INIT_IN_QID_OFST 0
/* the static priority associated with the txq */
#define       MC_CMD_TCM_TXQ_INIT_IN_LABEL_OFST 4
/* bitmask of the priority queues this txq is inserted into */
#define       MC_CMD_TCM_TXQ_INIT_IN_PQ_FLAGS_OFST 8
/* the reaction point (RP) bucket */
#define       MC_CMD_TCM_TXQ_INIT_IN_RP_BKT_OFST 12
/* an already reserved bucket (typically set to bucket associated with outer
 * vswitch)
 */
#define       MC_CMD_TCM_TXQ_INIT_IN_MAX_BKT1_OFST 16
/* an already reserved bucket (typically set to bucket associated with inner
 * vswitch)
 */
#define       MC_CMD_TCM_TXQ_INIT_IN_MAX_BKT2_OFST 20
/* the min bucket (typically for ETS/minimum bandwidth) */
#define       MC_CMD_TCM_TXQ_INIT_IN_MIN_BKT_OFST 24

/* MC_CMD_TCM_TXQ_INIT_OUT msgresponse */
#define    MC_CMD_TCM_TXQ_INIT_OUT_LEN 0


/***********************************/
/* MC_CMD_LINK_PIOBUF
 * Link a push I/O buffer to a TxQ
 */
#define MC_CMD_LINK_PIOBUF 0x92

/* MC_CMD_LINK_PIOBUF_IN msgrequest */
#define    MC_CMD_LINK_PIOBUF_IN_LEN 8
/* Handle for allocated push I/O buffer. */
#define       MC_CMD_LINK_PIOBUF_IN_PIOBUF_HANDLE_OFST 0
/* Function Local Instance (VI) number. */
#define       MC_CMD_LINK_PIOBUF_IN_TXQ_INSTANCE_OFST 4

/* MC_CMD_LINK_PIOBUF_OUT msgresponse */
#define    MC_CMD_LINK_PIOBUF_OUT_LEN 0


/***********************************/
/* MC_CMD_UNLINK_PIOBUF
 * Unlink a push I/O buffer from a TxQ
 */
#define MC_CMD_UNLINK_PIOBUF 0x93

/* MC_CMD_UNLINK_PIOBUF_IN msgrequest */
#define    MC_CMD_UNLINK_PIOBUF_IN_LEN 4
/* Function Local Instance (VI) number. */
#define       MC_CMD_UNLINK_PIOBUF_IN_TXQ_INSTANCE_OFST 0

/* MC_CMD_UNLINK_PIOBUF_OUT msgresponse */
#define    MC_CMD_UNLINK_PIOBUF_OUT_LEN 0


/***********************************/
/* MC_CMD_VSWITCH_ALLOC
 * allocate and initialise a v-switch.
 */
#define MC_CMD_VSWITCH_ALLOC 0x94

/* MC_CMD_VSWITCH_ALLOC_IN msgrequest */
#define    MC_CMD_VSWITCH_ALLOC_IN_LEN 16
/* The port to connect to the v-switch's upstream port. */
#define       MC_CMD_VSWITCH_ALLOC_IN_UPSTREAM_PORT_ID_OFST 0
/* The type of v-switch to create. */
#define       MC_CMD_VSWITCH_ALLOC_IN_TYPE_OFST 4
/* enum: VLAN */
#define          MC_CMD_VSWITCH_ALLOC_IN_VSWITCH_TYPE_VLAN  0x1
/* enum: VEB */
#define          MC_CMD_VSWITCH_ALLOC_IN_VSWITCH_TYPE_VEB  0x2
/* enum: VEPA */
#define          MC_CMD_VSWITCH_ALLOC_IN_VSWITCH_TYPE_VEPA  0x3
/* Flags controlling v-port creation */
#define       MC_CMD_VSWITCH_ALLOC_IN_FLAGS_OFST 8
#define        MC_CMD_VSWITCH_ALLOC_IN_FLAG_AUTO_PORT_LBN 0
#define        MC_CMD_VSWITCH_ALLOC_IN_FLAG_AUTO_PORT_WIDTH 1
/* The number of VLAN tags to support. */
#define       MC_CMD_VSWITCH_ALLOC_IN_NUM_VLAN_TAGS_OFST 12

/* MC_CMD_VSWITCH_ALLOC_OUT msgresponse */
#define    MC_CMD_VSWITCH_ALLOC_OUT_LEN 0


/***********************************/
/* MC_CMD_VSWITCH_FREE
 * de-allocate a v-switch.
 */
#define MC_CMD_VSWITCH_FREE 0x95

/* MC_CMD_VSWITCH_FREE_IN msgrequest */
#define    MC_CMD_VSWITCH_FREE_IN_LEN 4
/* The port to which the v-switch is connected. */
#define       MC_CMD_VSWITCH_FREE_IN_UPSTREAM_PORT_ID_OFST 0

/* MC_CMD_VSWITCH_FREE_OUT msgresponse */
#define    MC_CMD_VSWITCH_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_VPORT_ALLOC
 * allocate a v-port.
 */
#define MC_CMD_VPORT_ALLOC 0x96

/* MC_CMD_VPORT_ALLOC_IN msgrequest */
#define    MC_CMD_VPORT_ALLOC_IN_LEN 20
/* The port to which the v-switch is connected. */
#define       MC_CMD_VPORT_ALLOC_IN_UPSTREAM_PORT_ID_OFST 0
/* The type of the new v-port. */
#define       MC_CMD_VPORT_ALLOC_IN_TYPE_OFST 4
/* enum: VLAN (obsolete) */
#define          MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_VLAN  0x1
/* enum: VEB (obsolete) */
#define          MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_VEB  0x2
/* enum: VEPA (obsolete) */
#define          MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_VEPA  0x3
/* enum: A normal v-port receives packets which match a specified MAC and/or
 * VLAN.
 */
#define          MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_NORMAL  0x4
/* enum: An expansion v-port packets traffic which don't match any other
 * v-port.
 */
#define          MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_EXPANSION  0x5
/* enum: An test v-port receives packets which match any filters installed by
 * its downstream components.
 */
#define          MC_CMD_VPORT_ALLOC_IN_VPORT_TYPE_TEST  0x6
/* Flags controlling v-port creation */
#define       MC_CMD_VPORT_ALLOC_IN_FLAGS_OFST 8
#define        MC_CMD_VPORT_ALLOC_IN_FLAG_AUTO_PORT_LBN 0
#define        MC_CMD_VPORT_ALLOC_IN_FLAG_AUTO_PORT_WIDTH 1
/* The number of VLAN tags to insert/remove. */
#define       MC_CMD_VPORT_ALLOC_IN_NUM_VLAN_TAGS_OFST 12
/* The actual VLAN tags to insert/remove */
#define       MC_CMD_VPORT_ALLOC_IN_VLAN_TAGS_OFST 16
#define        MC_CMD_VPORT_ALLOC_IN_VLAN_TAG_0_LBN 0
#define        MC_CMD_VPORT_ALLOC_IN_VLAN_TAG_0_WIDTH 16
#define        MC_CMD_VPORT_ALLOC_IN_VLAN_TAG_1_LBN 16
#define        MC_CMD_VPORT_ALLOC_IN_VLAN_TAG_1_WIDTH 16

/* MC_CMD_VPORT_ALLOC_OUT msgresponse */
#define    MC_CMD_VPORT_ALLOC_OUT_LEN 4
/* The handle of the new v-port */
#define       MC_CMD_VPORT_ALLOC_OUT_VPORT_ID_OFST 0


/***********************************/
/* MC_CMD_VPORT_FREE
 * de-allocate a v-port.
 */
#define MC_CMD_VPORT_FREE 0x97

/* MC_CMD_VPORT_FREE_IN msgrequest */
#define    MC_CMD_VPORT_FREE_IN_LEN 4
/* The handle of the v-port */
#define       MC_CMD_VPORT_FREE_IN_VPORT_ID_OFST 0

/* MC_CMD_VPORT_FREE_OUT msgresponse */
#define    MC_CMD_VPORT_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_VADAPTOR_ALLOC
 * allocate a v-adaptor.
 */
#define MC_CMD_VADAPTOR_ALLOC 0x98

/* MC_CMD_VADAPTOR_ALLOC_IN msgrequest */
#define    MC_CMD_VADAPTOR_ALLOC_IN_LEN 16
/* The port to connect to the v-adaptor's port. */
#define       MC_CMD_VADAPTOR_ALLOC_IN_UPSTREAM_PORT_ID_OFST 0
/* Flags controlling v-adaptor creation */
#define       MC_CMD_VADAPTOR_ALLOC_IN_FLAGS_OFST 8
#define        MC_CMD_VADAPTOR_ALLOC_IN_FLAG_AUTO_VADAPTOR_LBN 0
#define        MC_CMD_VADAPTOR_ALLOC_IN_FLAG_AUTO_VADAPTOR_WIDTH 1
/* The number of VLAN tags to strip on receive */
#define       MC_CMD_VADAPTOR_ALLOC_IN_NUM_VLANS_OFST 12

/* MC_CMD_VADAPTOR_ALLOC_OUT msgresponse */
#define    MC_CMD_VADAPTOR_ALLOC_OUT_LEN 0


/***********************************/
/* MC_CMD_VADAPTOR_FREE
 * de-allocate a v-adaptor.
 */
#define MC_CMD_VADAPTOR_FREE 0x99

/* MC_CMD_VADAPTOR_FREE_IN msgrequest */
#define    MC_CMD_VADAPTOR_FREE_IN_LEN 4
/* The port to which the v-adaptor is connected. */
#define       MC_CMD_VADAPTOR_FREE_IN_UPSTREAM_PORT_ID_OFST 0

/* MC_CMD_VADAPTOR_FREE_OUT msgresponse */
#define    MC_CMD_VADAPTOR_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_EVB_PORT_ASSIGN
 * assign a port to a PCI function.
 */
#define MC_CMD_EVB_PORT_ASSIGN 0x9a

/* MC_CMD_EVB_PORT_ASSIGN_IN msgrequest */
#define    MC_CMD_EVB_PORT_ASSIGN_IN_LEN 8
/* The port to assign. */
#define       MC_CMD_EVB_PORT_ASSIGN_IN_PORT_ID_OFST 0
/* The target function to modify. */
#define       MC_CMD_EVB_PORT_ASSIGN_IN_FUNCTION_OFST 4
#define        MC_CMD_EVB_PORT_ASSIGN_IN_PF_LBN 0
#define        MC_CMD_EVB_PORT_ASSIGN_IN_PF_WIDTH 16
#define        MC_CMD_EVB_PORT_ASSIGN_IN_VF_LBN 16
#define        MC_CMD_EVB_PORT_ASSIGN_IN_VF_WIDTH 16

/* MC_CMD_EVB_PORT_ASSIGN_OUT msgresponse */
#define    MC_CMD_EVB_PORT_ASSIGN_OUT_LEN 0


/***********************************/
/* MC_CMD_RDWR_A64_REGIONS
 * Assign the 64 bit region addresses.
 */
#define MC_CMD_RDWR_A64_REGIONS 0x9b

/* MC_CMD_RDWR_A64_REGIONS_IN msgrequest */
#define    MC_CMD_RDWR_A64_REGIONS_IN_LEN 17
#define       MC_CMD_RDWR_A64_REGIONS_IN_REGION0_OFST 0
#define       MC_CMD_RDWR_A64_REGIONS_IN_REGION1_OFST 4
#define       MC_CMD_RDWR_A64_REGIONS_IN_REGION2_OFST 8
#define       MC_CMD_RDWR_A64_REGIONS_IN_REGION3_OFST 12
/* Write enable bits 0-3, set to write, clear to read. */
#define       MC_CMD_RDWR_A64_REGIONS_IN_WRITE_MASK_LBN 128
#define       MC_CMD_RDWR_A64_REGIONS_IN_WRITE_MASK_WIDTH 4
#define       MC_CMD_RDWR_A64_REGIONS_IN_WRITE_MASK_BYTE_OFST 16
#define       MC_CMD_RDWR_A64_REGIONS_IN_WRITE_MASK_BYTE_LEN 1

/* MC_CMD_RDWR_A64_REGIONS_OUT msgresponse: This data always included
 * regardless of state of write bits in the request.
 */
#define    MC_CMD_RDWR_A64_REGIONS_OUT_LEN 16
#define       MC_CMD_RDWR_A64_REGIONS_OUT_REGION0_OFST 0
#define       MC_CMD_RDWR_A64_REGIONS_OUT_REGION1_OFST 4
#define       MC_CMD_RDWR_A64_REGIONS_OUT_REGION2_OFST 8
#define       MC_CMD_RDWR_A64_REGIONS_OUT_REGION3_OFST 12


/***********************************/
/* MC_CMD_ONLOAD_STACK_ALLOC
 * Allocate an Onload stack ID.
 */
#define MC_CMD_ONLOAD_STACK_ALLOC 0x9c

/* MC_CMD_ONLOAD_STACK_ALLOC_IN msgrequest */
#define    MC_CMD_ONLOAD_STACK_ALLOC_IN_LEN 4
/* The handle of the owning upstream port */
#define       MC_CMD_ONLOAD_STACK_ALLOC_IN_UPSTREAM_PORT_ID_OFST 0

/* MC_CMD_ONLOAD_STACK_ALLOC_OUT msgresponse */
#define    MC_CMD_ONLOAD_STACK_ALLOC_OUT_LEN 4
/* The handle of the new Onload stack */
#define       MC_CMD_ONLOAD_STACK_ALLOC_OUT_ONLOAD_STACK_ID_OFST 0


/***********************************/
/* MC_CMD_ONLOAD_STACK_FREE
 * Free an Onload stack ID.
 */
#define MC_CMD_ONLOAD_STACK_FREE 0x9d

/* MC_CMD_ONLOAD_STACK_FREE_IN msgrequest */
#define    MC_CMD_ONLOAD_STACK_FREE_IN_LEN 4
/* The handle of the Onload stack */
#define       MC_CMD_ONLOAD_STACK_FREE_IN_ONLOAD_STACK_ID_OFST 0

/* MC_CMD_ONLOAD_STACK_FREE_OUT msgresponse */
#define    MC_CMD_ONLOAD_STACK_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_RSS_CONTEXT_ALLOC
 * Allocate an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_ALLOC 0x9e

/* MC_CMD_RSS_CONTEXT_ALLOC_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_ALLOC_IN_LEN 12
/* The handle of the owning upstream port */
#define       MC_CMD_RSS_CONTEXT_ALLOC_IN_UPSTREAM_PORT_ID_OFST 0
/* The type of context to allocate */
#define       MC_CMD_RSS_CONTEXT_ALLOC_IN_TYPE_OFST 4
/* enum: Allocate a context for exclusive use. The key and indirection table
 * must be explicitly configured.
 */
#define          MC_CMD_RSS_CONTEXT_ALLOC_IN_TYPE_EXCLUSIVE  0x0
/* enum: Allocate a context for shared use; this will spread across a range of
 * queues, but the key and indirection table are pre-configured and may not be
 * changed. For this mode, NUM_QUEUES must 2, 4, 8, 16, 32 or 64.
 */
#define          MC_CMD_RSS_CONTEXT_ALLOC_IN_TYPE_SHARED  0x1
/* Number of queues spanned by this context, in the range 1-64; valid offsets
 * in the indirection table will be in the range 0 to NUM_QUEUES-1.
 */
#define       MC_CMD_RSS_CONTEXT_ALLOC_IN_NUM_QUEUES_OFST 8

/* MC_CMD_RSS_CONTEXT_ALLOC_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_ALLOC_OUT_LEN 4
/* The handle of the new RSS context */
#define       MC_CMD_RSS_CONTEXT_ALLOC_OUT_RSS_CONTEXT_ID_OFST 0


/***********************************/
/* MC_CMD_RSS_CONTEXT_FREE
 * Free an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_FREE 0x9f

/* MC_CMD_RSS_CONTEXT_FREE_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_FREE_IN_LEN 4
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_FREE_IN_RSS_CONTEXT_ID_OFST 0

/* MC_CMD_RSS_CONTEXT_FREE_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_RSS_CONTEXT_SET_KEY
 * Set the Toeplitz hash key for an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_SET_KEY 0xa0

/* MC_CMD_RSS_CONTEXT_SET_KEY_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_SET_KEY_IN_LEN 44
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_SET_KEY_IN_RSS_CONTEXT_ID_OFST 0
/* The 40-byte Toeplitz hash key (TBD endianness issues?) */
#define       MC_CMD_RSS_CONTEXT_SET_KEY_IN_TOEPLITZ_KEY_OFST 4
#define       MC_CMD_RSS_CONTEXT_SET_KEY_IN_TOEPLITZ_KEY_LEN 40

/* MC_CMD_RSS_CONTEXT_SET_KEY_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_SET_KEY_OUT_LEN 0


/***********************************/
/* MC_CMD_RSS_CONTEXT_GET_KEY
 * Get the Toeplitz hash key for an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_GET_KEY 0xa1

/* MC_CMD_RSS_CONTEXT_GET_KEY_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_GET_KEY_IN_LEN 4
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_GET_KEY_IN_RSS_CONTEXT_ID_OFST 0

/* MC_CMD_RSS_CONTEXT_GET_KEY_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_GET_KEY_OUT_LEN 44
/* The 40-byte Toeplitz hash key (TBD endianness issues?) */
#define       MC_CMD_RSS_CONTEXT_GET_KEY_OUT_TOEPLITZ_KEY_OFST 4
#define       MC_CMD_RSS_CONTEXT_GET_KEY_OUT_TOEPLITZ_KEY_LEN 40


/***********************************/
/* MC_CMD_RSS_CONTEXT_SET_TABLE
 * Set the indirection table for an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_SET_TABLE 0xa2

/* MC_CMD_RSS_CONTEXT_SET_TABLE_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_SET_TABLE_IN_LEN 132
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_SET_TABLE_IN_RSS_CONTEXT_ID_OFST 0
/* The 128-byte indirection table (1 byte per entry) */
#define       MC_CMD_RSS_CONTEXT_SET_TABLE_IN_INDIRECTION_TABLE_OFST 4
#define       MC_CMD_RSS_CONTEXT_SET_TABLE_IN_INDIRECTION_TABLE_LEN 128

/* MC_CMD_RSS_CONTEXT_SET_TABLE_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_SET_TABLE_OUT_LEN 0


/***********************************/
/* MC_CMD_RSS_CONTEXT_GET_TABLE
 * Get the indirection table for an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_GET_TABLE 0xa3

/* MC_CMD_RSS_CONTEXT_GET_TABLE_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_GET_TABLE_IN_LEN 4
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_GET_TABLE_IN_RSS_CONTEXT_ID_OFST 0

/* MC_CMD_RSS_CONTEXT_GET_TABLE_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_GET_TABLE_OUT_LEN 132
/* The 128-byte indirection table (1 byte per entry) */
#define       MC_CMD_RSS_CONTEXT_GET_TABLE_OUT_INDIRECTION_TABLE_OFST 4
#define       MC_CMD_RSS_CONTEXT_GET_TABLE_OUT_INDIRECTION_TABLE_LEN 128


/***********************************/
/* MC_CMD_RSS_CONTEXT_SET_FLAGS
 * Set various control flags for an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_SET_FLAGS 0xe1

/* MC_CMD_RSS_CONTEXT_SET_FLAGS_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_LEN 8
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_RSS_CONTEXT_ID_OFST 0
/* Hash control flags */
#define       MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_FLAGS_OFST 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_IPV4_EN_LBN 0
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_IPV4_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_TCPV4_EN_LBN 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_TCPV4_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_IPV6_EN_LBN 2
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_IPV6_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_TCPV6_EN_LBN 3
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_TCPV6_EN_WIDTH 1

/* MC_CMD_RSS_CONTEXT_SET_FLAGS_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_SET_FLAGS_OUT_LEN 0


/***********************************/
/* MC_CMD_RSS_CONTEXT_GET_FLAGS
 * Get various control flags for an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_GET_FLAGS 0xe2

/* MC_CMD_RSS_CONTEXT_GET_FLAGS_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_GET_FLAGS_IN_LEN 4
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_GET_FLAGS_IN_RSS_CONTEXT_ID_OFST 0

/* MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_LEN 8
/* Hash control flags */
#define       MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_FLAGS_OFST 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV4_EN_LBN 0
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV4_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV4_EN_LBN 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV4_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV6_EN_LBN 2
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV6_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV6_EN_LBN 3
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV6_EN_WIDTH 1


/***********************************/
/* MC_CMD_DOT1P_MAPPING_ALLOC
 * Allocate a .1p mapping.
 */
#define MC_CMD_DOT1P_MAPPING_ALLOC 0xa4

/* MC_CMD_DOT1P_MAPPING_ALLOC_IN msgrequest */
#define    MC_CMD_DOT1P_MAPPING_ALLOC_IN_LEN 8
/* The handle of the owning upstream port */
#define       MC_CMD_DOT1P_MAPPING_ALLOC_IN_UPSTREAM_PORT_ID_OFST 0
/* Number of queues spanned by this mapping, in the range 1-64; valid fixed
 * offsets in the mapping table will be in the range 0 to NUM_QUEUES-1, and
 * referenced RSS contexts must span no more than this number.
 */
#define       MC_CMD_DOT1P_MAPPING_ALLOC_IN_NUM_QUEUES_OFST 4

/* MC_CMD_DOT1P_MAPPING_ALLOC_OUT msgresponse */
#define    MC_CMD_DOT1P_MAPPING_ALLOC_OUT_LEN 4
/* The handle of the new .1p mapping */
#define       MC_CMD_DOT1P_MAPPING_ALLOC_OUT_DOT1P_MAPPING_ID_OFST 0


/***********************************/
/* MC_CMD_DOT1P_MAPPING_FREE
 * Free a .1p mapping.
 */
#define MC_CMD_DOT1P_MAPPING_FREE 0xa5

/* MC_CMD_DOT1P_MAPPING_FREE_IN msgrequest */
#define    MC_CMD_DOT1P_MAPPING_FREE_IN_LEN 4
/* The handle of the .1p mapping */
#define       MC_CMD_DOT1P_MAPPING_FREE_IN_DOT1P_MAPPING_ID_OFST 0

/* MC_CMD_DOT1P_MAPPING_FREE_OUT msgresponse */
#define    MC_CMD_DOT1P_MAPPING_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_DOT1P_MAPPING_SET_TABLE
 * Set the mapping table for a .1p mapping.
 */
#define MC_CMD_DOT1P_MAPPING_SET_TABLE 0xa6

/* MC_CMD_DOT1P_MAPPING_SET_TABLE_IN msgrequest */
#define    MC_CMD_DOT1P_MAPPING_SET_TABLE_IN_LEN 36
/* The handle of the .1p mapping */
#define       MC_CMD_DOT1P_MAPPING_SET_TABLE_IN_DOT1P_MAPPING_ID_OFST 0
/* Per-priority mappings (1 32-bit word per entry - an offset or RSS context
 * handle)
 */
#define       MC_CMD_DOT1P_MAPPING_SET_TABLE_IN_MAPPING_TABLE_OFST 4
#define       MC_CMD_DOT1P_MAPPING_SET_TABLE_IN_MAPPING_TABLE_LEN 32

/* MC_CMD_DOT1P_MAPPING_SET_TABLE_OUT msgresponse */
#define    MC_CMD_DOT1P_MAPPING_SET_TABLE_OUT_LEN 0


/***********************************/
/* MC_CMD_DOT1P_MAPPING_GET_TABLE
 * Get the mapping table for a .1p mapping.
 */
#define MC_CMD_DOT1P_MAPPING_GET_TABLE 0xa7

/* MC_CMD_DOT1P_MAPPING_GET_TABLE_IN msgrequest */
#define    MC_CMD_DOT1P_MAPPING_GET_TABLE_IN_LEN 4
/* The handle of the .1p mapping */
#define       MC_CMD_DOT1P_MAPPING_GET_TABLE_IN_DOT1P_MAPPING_ID_OFST 0

/* MC_CMD_DOT1P_MAPPING_GET_TABLE_OUT msgresponse */
#define    MC_CMD_DOT1P_MAPPING_GET_TABLE_OUT_LEN 36
/* Per-priority mappings (1 32-bit word per entry - an offset or RSS context
 * handle)
 */
#define       MC_CMD_DOT1P_MAPPING_GET_TABLE_OUT_MAPPING_TABLE_OFST 4
#define       MC_CMD_DOT1P_MAPPING_GET_TABLE_OUT_MAPPING_TABLE_LEN 32


/***********************************/
/* MC_CMD_GET_VECTOR_CFG
 * Get Interrupt Vector config for this PF.
 */
#define MC_CMD_GET_VECTOR_CFG 0xbf

/* MC_CMD_GET_VECTOR_CFG_IN msgrequest */
#define    MC_CMD_GET_VECTOR_CFG_IN_LEN 0

/* MC_CMD_GET_VECTOR_CFG_OUT msgresponse */
#define    MC_CMD_GET_VECTOR_CFG_OUT_LEN 12
/* Base absolute interrupt vector number. */
#define       MC_CMD_GET_VECTOR_CFG_OUT_VEC_BASE_OFST 0
/* Number of interrupt vectors allocate to this PF. */
#define       MC_CMD_GET_VECTOR_CFG_OUT_VECS_PER_PF_OFST 4
/* Number of interrupt vectors to allocate per VF. */
#define       MC_CMD_GET_VECTOR_CFG_OUT_VECS_PER_VF_OFST 8


/***********************************/
/* MC_CMD_SET_VECTOR_CFG
 * Set Interrupt Vector config for this PF.
 */
#define MC_CMD_SET_VECTOR_CFG 0xc0

/* MC_CMD_SET_VECTOR_CFG_IN msgrequest */
#define    MC_CMD_SET_VECTOR_CFG_IN_LEN 12
/* Base absolute interrupt vector number, or MC_CMD_RESOURCE_INSTANCE_ANY to
 * let the system find a suitable base.
 */
#define       MC_CMD_SET_VECTOR_CFG_IN_VEC_BASE_OFST 0
/* Number of interrupt vectors allocate to this PF. */
#define       MC_CMD_SET_VECTOR_CFG_IN_VECS_PER_PF_OFST 4
/* Number of interrupt vectors to allocate per VF. */
#define       MC_CMD_SET_VECTOR_CFG_IN_VECS_PER_VF_OFST 8

/* MC_CMD_SET_VECTOR_CFG_OUT msgresponse */
#define    MC_CMD_SET_VECTOR_CFG_OUT_LEN 0


/***********************************/
/* MC_CMD_RMON_RX_CLASS_STATS
 * Retrieve rmon rx class statistics
 */
#define MC_CMD_RMON_RX_CLASS_STATS 0xc3

/* MC_CMD_RMON_RX_CLASS_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_CLASS_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_CLASS_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_CLASS_STATS_IN_CLASS_LBN 0
#define        MC_CMD_RMON_RX_CLASS_STATS_IN_CLASS_WIDTH 8
#define        MC_CMD_RMON_RX_CLASS_STATS_IN_RST_LBN 8
#define        MC_CMD_RMON_RX_CLASS_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_CLASS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_CLASS_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_CLASS_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_CLASS_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_CLASS_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_CLASS_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_CLASS_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_CLASS_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_CLASS_STATS
 * Retrieve rmon tx class statistics
 */
#define MC_CMD_RMON_TX_CLASS_STATS 0xc4

/* MC_CMD_RMON_TX_CLASS_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_CLASS_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_CLASS_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_CLASS_STATS_IN_CLASS_LBN 0
#define        MC_CMD_RMON_TX_CLASS_STATS_IN_CLASS_WIDTH 8
#define        MC_CMD_RMON_TX_CLASS_STATS_IN_RST_LBN 8
#define        MC_CMD_RMON_TX_CLASS_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_CLASS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_CLASS_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_CLASS_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_CLASS_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_CLASS_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_CLASS_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_CLASS_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_CLASS_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_SUPER_CLASS_STATS
 * Retrieve rmon rx super_class statistics
 */
#define MC_CMD_RMON_RX_SUPER_CLASS_STATS 0xc5

/* MC_CMD_RMON_RX_SUPER_CLASS_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_SUPER_CLASS_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_SUPER_CLASS_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_SUPER_CLASS_STATS_IN_SUPER_CLASS_LBN 0
#define        MC_CMD_RMON_RX_SUPER_CLASS_STATS_IN_SUPER_CLASS_WIDTH 4
#define        MC_CMD_RMON_RX_SUPER_CLASS_STATS_IN_RST_LBN 4
#define        MC_CMD_RMON_RX_SUPER_CLASS_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_SUPER_CLASS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_SUPER_CLASS_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_SUPER_CLASS_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_SUPER_CLASS_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_SUPER_CLASS_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_SUPER_CLASS_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_SUPER_CLASS_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_SUPER_CLASS_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_SUPER_CLASS_STATS
 * Retrieve rmon tx super_class statistics
 */
#define MC_CMD_RMON_TX_SUPER_CLASS_STATS 0xc6

/* MC_CMD_RMON_TX_SUPER_CLASS_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_SUPER_CLASS_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_SUPER_CLASS_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_SUPER_CLASS_STATS_IN_SUPER_CLASS_LBN 0
#define        MC_CMD_RMON_TX_SUPER_CLASS_STATS_IN_SUPER_CLASS_WIDTH 4
#define        MC_CMD_RMON_TX_SUPER_CLASS_STATS_IN_RST_LBN 4
#define        MC_CMD_RMON_TX_SUPER_CLASS_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_SUPER_CLASS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_SUPER_CLASS_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_SUPER_CLASS_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_SUPER_CLASS_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_SUPER_CLASS_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_SUPER_CLASS_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_SUPER_CLASS_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_SUPER_CLASS_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_ADD_QID_TO_CLASS
 * Add qid to class for statistics collection
 */
#define MC_CMD_RMON_RX_ADD_QID_TO_CLASS 0xc7

/* MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN msgrequest */
#define    MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_LEN 12
/* class */
#define       MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_CLASS_OFST 0
/* qid */
#define       MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_QID_OFST 4
/* flags */
#define       MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_FLAGS_OFST 8
#define        MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_SUPER_CLASS_LBN 0
#define        MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_SUPER_CLASS_WIDTH 4
#define        MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_PE_DELTA_LBN 4
#define        MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_PE_DELTA_WIDTH 4
#define        MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_MTU_LBN 8
#define        MC_CMD_RMON_RX_ADD_QID_TO_CLASS_IN_MTU_WIDTH 14

/* MC_CMD_RMON_RX_ADD_QID_TO_CLASS_OUT msgresponse */
#define    MC_CMD_RMON_RX_ADD_QID_TO_CLASS_OUT_LEN 0


/***********************************/
/* MC_CMD_RMON_TX_ADD_QID_TO_CLASS
 * Add qid to class for statistics collection
 */
#define MC_CMD_RMON_TX_ADD_QID_TO_CLASS 0xc8

/* MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN msgrequest */
#define    MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_LEN 12
/* class */
#define       MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_CLASS_OFST 0
/* qid */
#define       MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_QID_OFST 4
/* flags */
#define       MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_FLAGS_OFST 8
#define        MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_SUPER_CLASS_LBN 0
#define        MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_SUPER_CLASS_WIDTH 4
#define        MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_PE_DELTA_LBN 4
#define        MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_PE_DELTA_WIDTH 4
#define        MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_MTU_LBN 8
#define        MC_CMD_RMON_TX_ADD_QID_TO_CLASS_IN_MTU_WIDTH 14

/* MC_CMD_RMON_TX_ADD_QID_TO_CLASS_OUT msgresponse */
#define    MC_CMD_RMON_TX_ADD_QID_TO_CLASS_OUT_LEN 0


/***********************************/
/* MC_CMD_RMON_MC_ADD_QID_TO_CLASS
 * Add qid to class for statistics collection
 */
#define MC_CMD_RMON_MC_ADD_QID_TO_CLASS 0xc9

/* MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN msgrequest */
#define    MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_LEN 12
/* class */
#define       MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_CLASS_OFST 0
/* qid */
#define       MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_QID_OFST 4
/* flags */
#define       MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_FLAGS_OFST 8
#define        MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_SUPER_CLASS_LBN 0
#define        MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_SUPER_CLASS_WIDTH 4
#define        MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_PE_DELTA_LBN 4
#define        MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_PE_DELTA_WIDTH 4
#define        MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_MTU_LBN 8
#define        MC_CMD_RMON_MC_ADD_QID_TO_CLASS_IN_MTU_WIDTH 14

/* MC_CMD_RMON_MC_ADD_QID_TO_CLASS_OUT msgresponse */
#define    MC_CMD_RMON_MC_ADD_QID_TO_CLASS_OUT_LEN 0


/***********************************/
/* MC_CMD_RMON_ALLOC_CLASS
 * Allocate an rmon class
 */
#define MC_CMD_RMON_ALLOC_CLASS 0xca

/* MC_CMD_RMON_ALLOC_CLASS_IN msgrequest */
#define    MC_CMD_RMON_ALLOC_CLASS_IN_LEN 0

/* MC_CMD_RMON_ALLOC_CLASS_OUT msgresponse */
#define    MC_CMD_RMON_ALLOC_CLASS_OUT_LEN 4
/* class */
#define       MC_CMD_RMON_ALLOC_CLASS_OUT_CLASS_OFST 0


/***********************************/
/* MC_CMD_RMON_DEALLOC_CLASS
 * Deallocate an rmon class
 */
#define MC_CMD_RMON_DEALLOC_CLASS 0xcb

/* MC_CMD_RMON_DEALLOC_CLASS_IN msgrequest */
#define    MC_CMD_RMON_DEALLOC_CLASS_IN_LEN 4
/* class */
#define       MC_CMD_RMON_DEALLOC_CLASS_IN_CLASS_OFST 0

/* MC_CMD_RMON_DEALLOC_CLASS_OUT msgresponse */
#define    MC_CMD_RMON_DEALLOC_CLASS_OUT_LEN 0


/***********************************/
/* MC_CMD_RMON_ALLOC_SUPER_CLASS
 * Allocate an rmon super_class
 */
#define MC_CMD_RMON_ALLOC_SUPER_CLASS 0xcc

/* MC_CMD_RMON_ALLOC_SUPER_CLASS_IN msgrequest */
#define    MC_CMD_RMON_ALLOC_SUPER_CLASS_IN_LEN 0

/* MC_CMD_RMON_ALLOC_SUPER_CLASS_OUT msgresponse */
#define    MC_CMD_RMON_ALLOC_SUPER_CLASS_OUT_LEN 4
/* super_class */
#define       MC_CMD_RMON_ALLOC_SUPER_CLASS_OUT_SUPER_CLASS_OFST 0


/***********************************/
/* MC_CMD_RMON_DEALLOC_SUPER_CLASS
 * Deallocate an rmon tx super_class
 */
#define MC_CMD_RMON_DEALLOC_SUPER_CLASS 0xcd

/* MC_CMD_RMON_DEALLOC_SUPER_CLASS_IN msgrequest */
#define    MC_CMD_RMON_DEALLOC_SUPER_CLASS_IN_LEN 4
/* super_class */
#define       MC_CMD_RMON_DEALLOC_SUPER_CLASS_IN_SUPER_CLASS_OFST 0

/* MC_CMD_RMON_DEALLOC_SUPER_CLASS_OUT msgresponse */
#define    MC_CMD_RMON_DEALLOC_SUPER_CLASS_OUT_LEN 0


/***********************************/
/* MC_CMD_RMON_RX_UP_CONV_STATS
 * Retrieve up converter statistics
 */
#define MC_CMD_RMON_RX_UP_CONV_STATS 0xce

/* MC_CMD_RMON_RX_UP_CONV_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_UP_CONV_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_UP_CONV_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_UP_CONV_STATS_IN_PORT_LBN 0
#define        MC_CMD_RMON_RX_UP_CONV_STATS_IN_PORT_WIDTH 2
#define        MC_CMD_RMON_RX_UP_CONV_STATS_IN_RST_LBN 2
#define        MC_CMD_RMON_RX_UP_CONV_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_UP_CONV_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_UP_CONV_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_UP_CONV_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_UP_CONV_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_UP_CONV_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_UP_CONV_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_UP_CONV_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_UP_CONV_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_IPI_STATS
 * Retrieve rx ipi stats
 */
#define MC_CMD_RMON_RX_IPI_STATS 0xcf

/* MC_CMD_RMON_RX_IPI_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_IPI_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_IPI_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_IPI_STATS_IN_VFIFO_LBN 0
#define        MC_CMD_RMON_RX_IPI_STATS_IN_VFIFO_WIDTH 5
#define        MC_CMD_RMON_RX_IPI_STATS_IN_RST_LBN 5
#define        MC_CMD_RMON_RX_IPI_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_IPI_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_IPI_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_IPI_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_IPI_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_IPI_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_IPI_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_IPI_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_IPI_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS
 * Retrieve rx ipsec cntxt_ptr indexed stats
 */
#define MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS 0xd0

/* MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_IN_CNTXT_PTR_LBN 0
#define        MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_IN_CNTXT_PTR_WIDTH 9
#define        MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_IN_RST_LBN 9
#define        MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_IPSEC_CNTXT_PTR_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_IPSEC_PORT_STATS
 * Retrieve rx ipsec port indexed stats
 */
#define MC_CMD_RMON_RX_IPSEC_PORT_STATS 0xd1

/* MC_CMD_RMON_RX_IPSEC_PORT_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_IPSEC_PORT_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_IPSEC_PORT_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_IPSEC_PORT_STATS_IN_PORT_LBN 0
#define        MC_CMD_RMON_RX_IPSEC_PORT_STATS_IN_PORT_WIDTH 2
#define        MC_CMD_RMON_RX_IPSEC_PORT_STATS_IN_RST_LBN 2
#define        MC_CMD_RMON_RX_IPSEC_PORT_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_IPSEC_PORT_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_IPSEC_PORT_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_IPSEC_PORT_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_IPSEC_PORT_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_IPSEC_PORT_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_IPSEC_PORT_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_IPSEC_PORT_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_IPSEC_PORT_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_IPSEC_OFLOW_STATS
 * Retrieve tx ipsec overflow
 */
#define MC_CMD_RMON_RX_IPSEC_OFLOW_STATS 0xd2

/* MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_IN_PORT_LBN 0
#define        MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_IN_PORT_WIDTH 2
#define        MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_IN_RST_LBN 2
#define        MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_IPSEC_OFLOW_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_VPORT_ADD_MAC_ADDRESS
 * Add a MAC address to a v-port
 */
#define MC_CMD_VPORT_ADD_MAC_ADDRESS 0xa8

/* MC_CMD_VPORT_ADD_MAC_ADDRESS_IN msgrequest */
#define    MC_CMD_VPORT_ADD_MAC_ADDRESS_IN_LEN 10
/* The handle of the v-port */
#define       MC_CMD_VPORT_ADD_MAC_ADDRESS_IN_VPORT_ID_OFST 0
/* MAC address to add */
#define       MC_CMD_VPORT_ADD_MAC_ADDRESS_IN_MACADDR_OFST 4
#define       MC_CMD_VPORT_ADD_MAC_ADDRESS_IN_MACADDR_LEN 6

/* MC_CMD_VPORT_ADD_MAC_ADDRESS_OUT msgresponse */
#define    MC_CMD_VPORT_ADD_MAC_ADDRESS_OUT_LEN 0


/***********************************/
/* MC_CMD_VPORT_DEL_MAC_ADDRESS
 * Delete a MAC address from a v-port
 */
#define MC_CMD_VPORT_DEL_MAC_ADDRESS 0xa9

/* MC_CMD_VPORT_DEL_MAC_ADDRESS_IN msgrequest */
#define    MC_CMD_VPORT_DEL_MAC_ADDRESS_IN_LEN 10
/* The handle of the v-port */
#define       MC_CMD_VPORT_DEL_MAC_ADDRESS_IN_VPORT_ID_OFST 0
/* MAC address to add */
#define       MC_CMD_VPORT_DEL_MAC_ADDRESS_IN_MACADDR_OFST 4
#define       MC_CMD_VPORT_DEL_MAC_ADDRESS_IN_MACADDR_LEN 6

/* MC_CMD_VPORT_DEL_MAC_ADDRESS_OUT msgresponse */
#define    MC_CMD_VPORT_DEL_MAC_ADDRESS_OUT_LEN 0


/***********************************/
/* MC_CMD_VPORT_GET_MAC_ADDRESSES
 * Delete a MAC address from a v-port
 */
#define MC_CMD_VPORT_GET_MAC_ADDRESSES 0xaa

/* MC_CMD_VPORT_GET_MAC_ADDRESSES_IN msgrequest */
#define    MC_CMD_VPORT_GET_MAC_ADDRESSES_IN_LEN 4
/* The handle of the v-port */
#define       MC_CMD_VPORT_GET_MAC_ADDRESSES_IN_VPORT_ID_OFST 0

/* MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT msgresponse */
#define    MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_LENMIN 4
#define    MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_LENMAX 250
#define    MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_LEN(num) (4+6*(num))
/* The number of MAC addresses returned */
#define       MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_MACADDR_COUNT_OFST 0
/* Array of MAC addresses */
#define       MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_MACADDR_OFST 4
#define       MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_MACADDR_LEN 6
#define       MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_MACADDR_MINNUM 0
#define       MC_CMD_VPORT_GET_MAC_ADDRESSES_OUT_MACADDR_MAXNUM 41


/***********************************/
/* MC_CMD_DUMP_BUFTBL_ENTRIES
 * Dump buffer table entries, mainly for command client debug use. Dumps
 * absolute entries, and does not use chunk handles. All entries must be in
 * range, and used for q page mapping, Although the latter restriction may be
 * lifted in future.
 */
#define MC_CMD_DUMP_BUFTBL_ENTRIES 0xab

/* MC_CMD_DUMP_BUFTBL_ENTRIES_IN msgrequest */
#define    MC_CMD_DUMP_BUFTBL_ENTRIES_IN_LEN 8
/* Index of the first buffer table entry. */
#define       MC_CMD_DUMP_BUFTBL_ENTRIES_IN_FIRSTID_OFST 0
/* Number of buffer table entries to dump. */
#define       MC_CMD_DUMP_BUFTBL_ENTRIES_IN_NUMENTRIES_OFST 4

/* MC_CMD_DUMP_BUFTBL_ENTRIES_OUT msgresponse */
#define    MC_CMD_DUMP_BUFTBL_ENTRIES_OUT_LENMIN 12
#define    MC_CMD_DUMP_BUFTBL_ENTRIES_OUT_LENMAX 252
#define    MC_CMD_DUMP_BUFTBL_ENTRIES_OUT_LEN(num) (0+12*(num))
/* Raw buffer table entries, layed out as BUFTBL_ENTRY. */
#define       MC_CMD_DUMP_BUFTBL_ENTRIES_OUT_ENTRY_OFST 0
#define       MC_CMD_DUMP_BUFTBL_ENTRIES_OUT_ENTRY_LEN 12
#define       MC_CMD_DUMP_BUFTBL_ENTRIES_OUT_ENTRY_MINNUM 1
#define       MC_CMD_DUMP_BUFTBL_ENTRIES_OUT_ENTRY_MAXNUM 21


/***********************************/
/* MC_CMD_SET_RXDP_CONFIG
 * Set global RXDP configuration settings
 */
#define MC_CMD_SET_RXDP_CONFIG 0xc1

/* MC_CMD_SET_RXDP_CONFIG_IN msgrequest */
#define    MC_CMD_SET_RXDP_CONFIG_IN_LEN 4
#define       MC_CMD_SET_RXDP_CONFIG_IN_DATA_OFST 0
#define        MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_DMA_LBN 0
#define        MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_DMA_WIDTH 1

/* MC_CMD_SET_RXDP_CONFIG_OUT msgresponse */
#define    MC_CMD_SET_RXDP_CONFIG_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_RXDP_CONFIG
 * Get global RXDP configuration settings
 */
#define MC_CMD_GET_RXDP_CONFIG 0xc2

/* MC_CMD_GET_RXDP_CONFIG_IN msgrequest */
#define    MC_CMD_GET_RXDP_CONFIG_IN_LEN 0

/* MC_CMD_GET_RXDP_CONFIG_OUT msgresponse */
#define    MC_CMD_GET_RXDP_CONFIG_OUT_LEN 4
#define       MC_CMD_GET_RXDP_CONFIG_OUT_DATA_OFST 0
#define        MC_CMD_GET_RXDP_CONFIG_OUT_PAD_HOST_DMA_LBN 0
#define        MC_CMD_GET_RXDP_CONFIG_OUT_PAD_HOST_DMA_WIDTH 1


/***********************************/
/* MC_CMD_RMON_RX_CLASS_DROPS_STATS
 * Retrieve rx class drop stats
 */
#define MC_CMD_RMON_RX_CLASS_DROPS_STATS 0xd3

/* MC_CMD_RMON_RX_CLASS_DROPS_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_CLASS_DROPS_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_CLASS_DROPS_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_CLASS_DROPS_STATS_IN_CLASS_LBN 0
#define        MC_CMD_RMON_RX_CLASS_DROPS_STATS_IN_CLASS_WIDTH 8
#define        MC_CMD_RMON_RX_CLASS_DROPS_STATS_IN_RST_LBN 8
#define        MC_CMD_RMON_RX_CLASS_DROPS_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_CLASS_DROPS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_CLASS_DROPS_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_CLASS_DROPS_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_CLASS_DROPS_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_CLASS_DROPS_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_CLASS_DROPS_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_CLASS_DROPS_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_CLASS_DROPS_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS
 * Retrieve rx super class drop stats
 */
#define MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS 0xd4

/* MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_IN_SUPER_CLASS_LBN 0
#define        MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_IN_SUPER_CLASS_WIDTH 4
#define        MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_IN_RST_LBN 4
#define        MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_SUPER_CLASS_DROPS_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_ERRORS_STATS
 * Retrieve rxdp errors
 */
#define MC_CMD_RMON_RX_ERRORS_STATS 0xd5

/* MC_CMD_RMON_RX_ERRORS_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_ERRORS_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_ERRORS_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_ERRORS_STATS_IN_QID_LBN 0
#define        MC_CMD_RMON_RX_ERRORS_STATS_IN_QID_WIDTH 11
#define        MC_CMD_RMON_RX_ERRORS_STATS_IN_RST_LBN 11
#define        MC_CMD_RMON_RX_ERRORS_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_ERRORS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_ERRORS_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_ERRORS_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_ERRORS_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_ERRORS_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_ERRORS_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_ERRORS_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_ERRORS_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_RX_OVERFLOW_STATS
 * Retrieve rxdp overflow
 */
#define MC_CMD_RMON_RX_OVERFLOW_STATS 0xd6

/* MC_CMD_RMON_RX_OVERFLOW_STATS_IN msgrequest */
#define    MC_CMD_RMON_RX_OVERFLOW_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_RX_OVERFLOW_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_RX_OVERFLOW_STATS_IN_CLASS_LBN 0
#define        MC_CMD_RMON_RX_OVERFLOW_STATS_IN_CLASS_WIDTH 8
#define        MC_CMD_RMON_RX_OVERFLOW_STATS_IN_RST_LBN 8
#define        MC_CMD_RMON_RX_OVERFLOW_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_RX_OVERFLOW_STATS_OUT msgresponse */
#define    MC_CMD_RMON_RX_OVERFLOW_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_RX_OVERFLOW_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_RX_OVERFLOW_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_RX_OVERFLOW_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_RX_OVERFLOW_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_RX_OVERFLOW_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_RX_OVERFLOW_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_IPI_STATS
 * Retrieve tx ipi stats
 */
#define MC_CMD_RMON_TX_IPI_STATS 0xd7

/* MC_CMD_RMON_TX_IPI_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_IPI_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_IPI_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_IPI_STATS_IN_VFIFO_LBN 0
#define        MC_CMD_RMON_TX_IPI_STATS_IN_VFIFO_WIDTH 5
#define        MC_CMD_RMON_TX_IPI_STATS_IN_RST_LBN 5
#define        MC_CMD_RMON_TX_IPI_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_IPI_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_IPI_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_IPI_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_IPI_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_IPI_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_IPI_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_IPI_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_IPI_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS
 * Retrieve tx ipsec counters by cntxt_ptr
 */
#define MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS 0xd8

/* MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_IN_CNTXT_PTR_LBN 0
#define        MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_IN_CNTXT_PTR_WIDTH 9
#define        MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_IN_RST_LBN 9
#define        MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_IPSEC_CNTXT_PTR_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_IPSEC_PORT_STATS
 * Retrieve tx ipsec counters by port
 */
#define MC_CMD_RMON_TX_IPSEC_PORT_STATS 0xd9

/* MC_CMD_RMON_TX_IPSEC_PORT_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_IPSEC_PORT_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_IPSEC_PORT_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_IPSEC_PORT_STATS_IN_PORT_LBN 0
#define        MC_CMD_RMON_TX_IPSEC_PORT_STATS_IN_PORT_WIDTH 2
#define        MC_CMD_RMON_TX_IPSEC_PORT_STATS_IN_RST_LBN 2
#define        MC_CMD_RMON_TX_IPSEC_PORT_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_IPSEC_PORT_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_IPSEC_PORT_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_IPSEC_PORT_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_IPSEC_PORT_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_IPSEC_PORT_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_IPSEC_PORT_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_IPSEC_PORT_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_IPSEC_PORT_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_IPSEC_OFLOW_STATS
 * Retrieve tx ipsec overflow
 */
#define MC_CMD_RMON_TX_IPSEC_OFLOW_STATS 0xda

/* MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_IN_PORT_LBN 0
#define        MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_IN_PORT_WIDTH 2
#define        MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_IN_RST_LBN 2
#define        MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_IPSEC_OFLOW_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_NOWHERE_STATS
 * Retrieve tx nowhere stats
 */
#define MC_CMD_RMON_TX_NOWHERE_STATS 0xdb

/* MC_CMD_RMON_TX_NOWHERE_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_NOWHERE_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_NOWHERE_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_NOWHERE_STATS_IN_CLASS_LBN 0
#define        MC_CMD_RMON_TX_NOWHERE_STATS_IN_CLASS_WIDTH 8
#define        MC_CMD_RMON_TX_NOWHERE_STATS_IN_RST_LBN 8
#define        MC_CMD_RMON_TX_NOWHERE_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_NOWHERE_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_NOWHERE_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_NOWHERE_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_NOWHERE_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_NOWHERE_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_NOWHERE_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_NOWHERE_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_NOWHERE_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_NOWHERE_QBB_STATS
 * Retrieve tx nowhere qbb stats
 */
#define MC_CMD_RMON_TX_NOWHERE_QBB_STATS 0xdc

/* MC_CMD_RMON_TX_NOWHERE_QBB_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_NOWHERE_QBB_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_NOWHERE_QBB_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_NOWHERE_QBB_STATS_IN_PRIORITY_LBN 0
#define        MC_CMD_RMON_TX_NOWHERE_QBB_STATS_IN_PRIORITY_WIDTH 3
#define        MC_CMD_RMON_TX_NOWHERE_QBB_STATS_IN_RST_LBN 3
#define        MC_CMD_RMON_TX_NOWHERE_QBB_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_NOWHERE_QBB_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_NOWHERE_QBB_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_NOWHERE_QBB_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_NOWHERE_QBB_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_NOWHERE_QBB_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_NOWHERE_QBB_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_NOWHERE_QBB_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_NOWHERE_QBB_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_ERRORS_STATS
 * Retrieve rxdp errors
 */
#define MC_CMD_RMON_TX_ERRORS_STATS 0xdd

/* MC_CMD_RMON_TX_ERRORS_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_ERRORS_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_ERRORS_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_ERRORS_STATS_IN_QID_LBN 0
#define        MC_CMD_RMON_TX_ERRORS_STATS_IN_QID_WIDTH 11
#define        MC_CMD_RMON_TX_ERRORS_STATS_IN_RST_LBN 11
#define        MC_CMD_RMON_TX_ERRORS_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_ERRORS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_ERRORS_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_ERRORS_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_ERRORS_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_ERRORS_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_ERRORS_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_ERRORS_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_ERRORS_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_TX_OVERFLOW_STATS
 * Retrieve rxdp overflow
 */
#define MC_CMD_RMON_TX_OVERFLOW_STATS 0xde

/* MC_CMD_RMON_TX_OVERFLOW_STATS_IN msgrequest */
#define    MC_CMD_RMON_TX_OVERFLOW_STATS_IN_LEN 4
/* flags */
#define       MC_CMD_RMON_TX_OVERFLOW_STATS_IN_FLAGS_OFST 0
#define        MC_CMD_RMON_TX_OVERFLOW_STATS_IN_CLASS_LBN 0
#define        MC_CMD_RMON_TX_OVERFLOW_STATS_IN_CLASS_WIDTH 8
#define        MC_CMD_RMON_TX_OVERFLOW_STATS_IN_RST_LBN 8
#define        MC_CMD_RMON_TX_OVERFLOW_STATS_IN_RST_WIDTH 1

/* MC_CMD_RMON_TX_OVERFLOW_STATS_OUT msgresponse */
#define    MC_CMD_RMON_TX_OVERFLOW_STATS_OUT_LENMIN 4
#define    MC_CMD_RMON_TX_OVERFLOW_STATS_OUT_LENMAX 252
#define    MC_CMD_RMON_TX_OVERFLOW_STATS_OUT_LEN(num) (0+4*(num))
/* Array of stats */
#define       MC_CMD_RMON_TX_OVERFLOW_STATS_OUT_BUFFER_OFST 0
#define       MC_CMD_RMON_TX_OVERFLOW_STATS_OUT_BUFFER_LEN 4
#define       MC_CMD_RMON_TX_OVERFLOW_STATS_OUT_BUFFER_MINNUM 1
#define       MC_CMD_RMON_TX_OVERFLOW_STATS_OUT_BUFFER_MAXNUM 63


/***********************************/
/* MC_CMD_RMON_COLLECT_CLASS_STATS
 * Explicitly collect class stats at the specified evb port
 */
#define MC_CMD_RMON_COLLECT_CLASS_STATS 0xdf

/* MC_CMD_RMON_COLLECT_CLASS_STATS_IN msgrequest */
#define    MC_CMD_RMON_COLLECT_CLASS_STATS_IN_LEN 4
/* The port id associated with the vport/pport at which to collect class stats
 */
#define       MC_CMD_RMON_COLLECT_CLASS_STATS_IN_PORT_ID_OFST 0

/* MC_CMD_RMON_COLLECT_CLASS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_COLLECT_CLASS_STATS_OUT_LEN 4
/* class */
#define       MC_CMD_RMON_COLLECT_CLASS_STATS_OUT_CLASS_OFST 0


/***********************************/
/* MC_CMD_RMON_COLLECT_SUPER_CLASS_STATS
 * Explicitly collect class stats at the specified evb port
 */
#define MC_CMD_RMON_COLLECT_SUPER_CLASS_STATS 0xe0

/* MC_CMD_RMON_COLLECT_SUPER_CLASS_STATS_IN msgrequest */
#define    MC_CMD_RMON_COLLECT_SUPER_CLASS_STATS_IN_LEN 4
/* The port id associated with the vport/pport at which to collect class stats
 */
#define       MC_CMD_RMON_COLLECT_SUPER_CLASS_STATS_IN_PORT_ID_OFST 0

/* MC_CMD_RMON_COLLECT_SUPER_CLASS_STATS_OUT msgresponse */
#define    MC_CMD_RMON_COLLECT_SUPER_CLASS_STATS_OUT_LEN 4
/* super_class */
#define       MC_CMD_RMON_COLLECT_SUPER_CLASS_STATS_OUT_SUPER_CLASS_OFST 0


/***********************************/
/* MC_CMD_GET_CLOCK
 * Return the system and PDCPU clock frequencies.
 */
#define MC_CMD_GET_CLOCK 0xac

/* MC_CMD_GET_CLOCK_IN msgrequest */
#define    MC_CMD_GET_CLOCK_IN_LEN 0

/* MC_CMD_GET_CLOCK_OUT msgresponse */
#define    MC_CMD_GET_CLOCK_OUT_LEN 8
/* System frequency, MHz */
#define       MC_CMD_GET_CLOCK_OUT_SYS_FREQ_OFST 0
/* DPCPU frequency, MHz */
#define       MC_CMD_GET_CLOCK_OUT_DPCPU_FREQ_OFST 4


/***********************************/
/* MC_CMD_SET_CLOCK
 * Control the system and DPCPU clock frequencies. Changes are lost reboot.
 */
#define MC_CMD_SET_CLOCK 0xad

/* MC_CMD_SET_CLOCK_IN msgrequest */
#define    MC_CMD_SET_CLOCK_IN_LEN 12
/* Requested system frequency in MHz; 0 leaves unchanged. */
#define       MC_CMD_SET_CLOCK_IN_SYS_FREQ_OFST 0
/* Requested inter-core frequency in MHz; 0 leaves unchanged. */
#define       MC_CMD_SET_CLOCK_IN_ICORE_FREQ_OFST 4
/* Request DPCPU frequency in MHz; 0 leaves unchanged. */
#define       MC_CMD_SET_CLOCK_IN_DPCPU_FREQ_OFST 8

/* MC_CMD_SET_CLOCK_OUT msgresponse */
#define    MC_CMD_SET_CLOCK_OUT_LEN 12
/* Resulting system frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_SYS_FREQ_OFST 0
/* Resulting inter-core frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_ICORE_FREQ_OFST 4
/* Resulting DPCPU frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_DPCPU_FREQ_OFST 8


/***********************************/
/* MC_CMD_DPCPU_RPC
 * Send an arbitrary DPCPU message.
 */
#define MC_CMD_DPCPU_RPC 0xae

/* MC_CMD_DPCPU_RPC_IN msgrequest */
#define    MC_CMD_DPCPU_RPC_IN_LEN 36
#define       MC_CMD_DPCPU_RPC_IN_CPU_OFST 0
/* enum: RxDPCPU */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_RX   0x0
/* enum: TxDPCPU0 */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_TX0  0x1
/* enum: TxDPCPU1 */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_TX1  0x2
/* First 8 bits [39:32] of DATA are consumed by MC-DPCPU protocol and must be
 * initialised to zero
 */
#define       MC_CMD_DPCPU_RPC_IN_DATA_OFST 4
#define       MC_CMD_DPCPU_RPC_IN_DATA_LEN 32
#define        MC_CMD_DPCPU_RPC_IN_HDR_CMD_CMDNUM_LBN 8
#define        MC_CMD_DPCPU_RPC_IN_HDR_CMD_CMDNUM_WIDTH 8
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_TXDPCPU_READ  0x6 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_TXDPCPU_WRITE  0x7 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_TXDPCPU_SELF_TEST  0xc /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_TXDPCPU_CSR_ACCESS  0xe /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_RXDPCPU_READ  0x46 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_RXDPCPU_WRITE  0x47 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_RXDPCPU_SELF_TEST  0x4a /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_RXDPCPU_CSR_ACCESS  0x4c /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CMDNUM_RXDPCPU_SET_MC_REPLAY_CNTXT  0x4d /* enum */
#define        MC_CMD_DPCPU_RPC_IN_HDR_CMD_REQ_OBJID_LBN 16
#define        MC_CMD_DPCPU_RPC_IN_HDR_CMD_REQ_OBJID_WIDTH 16
#define        MC_CMD_DPCPU_RPC_IN_HDR_CMD_REQ_ADDR_LBN 16
#define        MC_CMD_DPCPU_RPC_IN_HDR_CMD_REQ_ADDR_WIDTH 16
#define        MC_CMD_DPCPU_RPC_IN_HDR_CMD_REQ_COUNT_LBN 48
#define        MC_CMD_DPCPU_RPC_IN_HDR_CMD_REQ_COUNT_WIDTH 16
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_INFO_LBN 16
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_INFO_WIDTH 240
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_CMD_LBN 16
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_CMD_WIDTH 16
#define          MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_CMD_STOP_RETURN_RESULT  0x0 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_CMD_START_READ  0x1 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_CMD_START_WRITE  0x2 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_CMD_START_WRITE_READ  0x3 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_CMD_START_PIPELINED_READ  0x4 /* enum */
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_START_DELAY_LBN 48
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_START_DELAY_WIDTH 16
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_RPT_COUNT_LBN 64
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_RPT_COUNT_WIDTH 16
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_GAP_DELAY_LBN 80
#define        MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_GAP_DELAY_WIDTH 16
#define        MC_CMD_DPCPU_RPC_IN_MC_REPLAY_MODE_LBN 16
#define        MC_CMD_DPCPU_RPC_IN_MC_REPLAY_MODE_WIDTH 16
#define          MC_CMD_DPCPU_RPC_IN_MC_REPLAY_MODE_CUT_THROUGH  0x1 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_MC_REPLAY_MODE_STORE_FORWARD  0x2 /* enum */
#define          MC_CMD_DPCPU_RPC_IN_MC_REPLAY_MODE_STORE_FORWARD_FIRST  0x3 /* enum */
#define        MC_CMD_DPCPU_RPC_IN_MC_REPLAY_CNTXT_LBN 64
#define        MC_CMD_DPCPU_RPC_IN_MC_REPLAY_CNTXT_WIDTH 16
#define       MC_CMD_DPCPU_RPC_IN_WDATA_OFST 12
#define       MC_CMD_DPCPU_RPC_IN_WDATA_LEN 24
/* Register data to write. Only valid in write/write-read. */
#define       MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_DATA_OFST 16
/* Register address. */
#define       MC_CMD_DPCPU_RPC_IN_CSR_ACCESS_ADDRESS_OFST 20

/* MC_CMD_DPCPU_RPC_OUT msgresponse */
#define    MC_CMD_DPCPU_RPC_OUT_LEN 36
#define       MC_CMD_DPCPU_RPC_OUT_RC_OFST 0
/* DATA */
#define       MC_CMD_DPCPU_RPC_OUT_DATA_OFST 4
#define       MC_CMD_DPCPU_RPC_OUT_DATA_LEN 32
#define        MC_CMD_DPCPU_RPC_OUT_HDR_CMD_RESP_ERRCODE_LBN 32
#define        MC_CMD_DPCPU_RPC_OUT_HDR_CMD_RESP_ERRCODE_WIDTH 16
#define        MC_CMD_DPCPU_RPC_OUT_CSR_ACCESS_READ_COUNT_LBN 48
#define        MC_CMD_DPCPU_RPC_OUT_CSR_ACCESS_READ_COUNT_WIDTH 16
#define       MC_CMD_DPCPU_RPC_OUT_RDATA_OFST 12
#define       MC_CMD_DPCPU_RPC_OUT_RDATA_LEN 24
#define       MC_CMD_DPCPU_RPC_OUT_CSR_ACCESS_READ_VAL_1_OFST 12
#define       MC_CMD_DPCPU_RPC_OUT_CSR_ACCESS_READ_VAL_2_OFST 16
#define       MC_CMD_DPCPU_RPC_OUT_CSR_ACCESS_READ_VAL_3_OFST 20
#define       MC_CMD_DPCPU_RPC_OUT_CSR_ACCESS_READ_VAL_4_OFST 24


/***********************************/
/* MC_CMD_TRIGGER_INTERRUPT
 * Trigger an interrupt by prodding the BIU.
 */
#define MC_CMD_TRIGGER_INTERRUPT 0xe3

/* MC_CMD_TRIGGER_INTERRUPT_IN msgrequest */
#define    MC_CMD_TRIGGER_INTERRUPT_IN_LEN 4
/* Interrupt level relative to base for function. */
#define       MC_CMD_TRIGGER_INTERRUPT_IN_INTR_LEVEL_OFST 0

/* MC_CMD_TRIGGER_INTERRUPT_OUT msgresponse */
#define    MC_CMD_TRIGGER_INTERRUPT_OUT_LEN 0


/***********************************/
/* MC_CMD_DUMP_DO
 * Take a dump of the DUT state
 */
#define MC_CMD_DUMP_DO 0xe8

/* MC_CMD_DUMP_DO_IN msgrequest */
#define    MC_CMD_DUMP_DO_IN_LEN 52
#define       MC_CMD_DUMP_DO_IN_PADDING_OFST 0
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_OFST 4
#define          MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM  0x0 /* enum */
#define          MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_DEFAULT  0x1 /* enum */
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_TYPE_OFST 8
#define          MC_CMD_DUMP_DO_IN_DUMP_LOCATION_NVRAM  0x1 /* enum */
#define          MC_CMD_DUMP_DO_IN_DUMP_LOCATION_HOST_MEMORY  0x2 /* enum */
#define          MC_CMD_DUMP_DO_IN_DUMP_LOCATION_HOST_MEMORY_MLI  0x3 /* enum */
#define          MC_CMD_DUMP_DO_IN_DUMP_LOCATION_UART  0x4 /* enum */
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_NVRAM_PARTITION_TYPE_ID_OFST 12
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_NVRAM_OFFSET_OFST 16
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_ADDR_LO_OFST 12
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_ADDR_HI_OFST 16
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_MLI_ROOT_ADDR_LO_OFST 12
#define          MC_CMD_DUMP_DO_IN_HOST_MEMORY_MLI_PAGE_SIZE  0x1000 /* enum */
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_MLI_ROOT_ADDR_HI_OFST 16
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_MLI_DEPTH_OFST 20
#define          MC_CMD_DUMP_DO_IN_HOST_MEMORY_MLI_MAX_DEPTH  0x2 /* enum */
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_UART_PORT_OFST 12
#define       MC_CMD_DUMP_DO_IN_DUMPSPEC_SRC_CUSTOM_SIZE_OFST 24
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_OFST 28
#define          MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM  0x0 /* enum */
#define          MC_CMD_DUMP_DO_IN_DUMPFILE_DST_NVRAM_DUMP_PARTITION  0x1 /* enum */
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_TYPE_OFST 32
/*            Enum values, see field(s): */
/*               MC_CMD_DUMP_DO_IN/DUMPSPEC_SRC_CUSTOM_TYPE */
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_NVRAM_PARTITION_TYPE_ID_OFST 36
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_NVRAM_OFFSET_OFST 40
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_ADDR_LO_OFST 36
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_ADDR_HI_OFST 40
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_MLI_ROOT_ADDR_LO_OFST 36
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_MLI_ROOT_ADDR_HI_OFST 40
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_MLI_DEPTH_OFST 44
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_UART_PORT_OFST 36
#define       MC_CMD_DUMP_DO_IN_DUMPFILE_DST_CUSTOM_SIZE_OFST 48

/* MC_CMD_DUMP_DO_OUT msgresponse */
#define    MC_CMD_DUMP_DO_OUT_LEN 4
#define       MC_CMD_DUMP_DO_OUT_DUMPFILE_SIZE_OFST 0


/***********************************/
/* MC_CMD_DUMP_CONFIGURE_UNSOLICITED
 * Configure unsolicited dumps
 */
#define MC_CMD_DUMP_CONFIGURE_UNSOLICITED 0xe9

/* MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN msgrequest */
#define    MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_LEN 52
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_ENABLE_OFST 0
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_OFST 4
/*            Enum values, see field(s): */
/*               MC_CMD_DUMP_DO/MC_CMD_DUMP_DO_IN/DUMPSPEC_SRC */
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_TYPE_OFST 8
/*            Enum values, see field(s): */
/*               MC_CMD_DUMP_DO/MC_CMD_DUMP_DO_IN/DUMPSPEC_SRC_CUSTOM_TYPE */
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_NVRAM_PARTITION_TYPE_ID_OFST 12
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_NVRAM_OFFSET_OFST 16
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_ADDR_LO_OFST 12
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_ADDR_HI_OFST 16
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_MLI_ROOT_ADDR_LO_OFST 12
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_MLI_ROOT_ADDR_HI_OFST 16
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_HOST_MEMORY_MLI_DEPTH_OFST 20
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_UART_PORT_OFST 12
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPSPEC_SRC_CUSTOM_SIZE_OFST 24
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_OFST 28
/*            Enum values, see field(s): */
/*               MC_CMD_DUMP_DO/MC_CMD_DUMP_DO_IN/DUMPFILE_DST */
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_TYPE_OFST 32
/*            Enum values, see field(s): */
/*               MC_CMD_DUMP_DO/MC_CMD_DUMP_DO_IN/DUMPSPEC_SRC_CUSTOM_TYPE */
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_NVRAM_PARTITION_TYPE_ID_OFST 36
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_NVRAM_OFFSET_OFST 40
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_ADDR_LO_OFST 36
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_ADDR_HI_OFST 40
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_MLI_ROOT_ADDR_LO_OFST 36
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_MLI_ROOT_ADDR_HI_OFST 40
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_HOST_MEMORY_MLI_DEPTH_OFST 44
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_UART_PORT_OFST 36
#define       MC_CMD_DUMP_CONFIGURE_UNSOLICITED_IN_DUMPFILE_DST_CUSTOM_SIZE_OFST 48


/***********************************/
/* MC_CMD_SET_PSU
 * Adjusts power supply parameters. This is a warranty-voiding operation.
 * Returns: ENOENT if the parameter or rail specified does not exist, EINVAL if
 * the parameter is out of range.
 */
#define MC_CMD_SET_PSU 0xea

/* MC_CMD_SET_PSU_IN msgrequest */
#define    MC_CMD_SET_PSU_IN_LEN 12
#define       MC_CMD_SET_PSU_IN_PARAM_OFST 0
#define          MC_CMD_SET_PSU_IN_PARAM_SUPPLY_VOLTAGE  0x0 /* enum */
#define       MC_CMD_SET_PSU_IN_RAIL_OFST 4
#define          MC_CMD_SET_PSU_IN_RAIL_0V9  0x0 /* enum */
#define          MC_CMD_SET_PSU_IN_RAIL_1V2  0x1 /* enum */
/* desired value, eg voltage in mV */
#define       MC_CMD_SET_PSU_IN_VALUE_OFST 8

/* MC_CMD_SET_PSU_OUT msgresponse */
#define    MC_CMD_SET_PSU_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_FUNCTION_INFO
 * Get function information. PF and VF number.
 */
#define MC_CMD_GET_FUNCTION_INFO 0xec

/* MC_CMD_GET_FUNCTION_INFO_IN msgrequest */
#define    MC_CMD_GET_FUNCTION_INFO_IN_LEN 0

/* MC_CMD_GET_FUNCTION_INFO_OUT msgresponse */
#define    MC_CMD_GET_FUNCTION_INFO_OUT_LEN 8
#define       MC_CMD_GET_FUNCTION_INFO_OUT_PF_OFST 0
#define       MC_CMD_GET_FUNCTION_INFO_OUT_VF_OFST 4


/***********************************/
/* MC_CMD_ENABLE_OFFLINE_BIST
 * Enters offline BIST mode. All queues are torn down, chip enters quiescent
 * mode, calling function gets exclusive MCDI ownership. The only way out is
 * reboot.
 */
#define MC_CMD_ENABLE_OFFLINE_BIST 0xed

/* MC_CMD_ENABLE_OFFLINE_BIST_IN msgrequest */
#define    MC_CMD_ENABLE_OFFLINE_BIST_IN_LEN 0

/* MC_CMD_ENABLE_OFFLINE_BIST_OUT msgresponse */
#define    MC_CMD_ENABLE_OFFLINE_BIST_OUT_LEN 0


/***********************************/
/* MC_CMD_START_KR_EYE_PLOT
 * Start KR Serdes Eye diagram plot on a given lane. Lane must have valid
 * signal.
 */
#define MC_CMD_START_KR_EYE_PLOT 0xee

/* MC_CMD_START_KR_EYE_PLOT_IN msgrequest */
#define    MC_CMD_START_KR_EYE_PLOT_IN_LEN 4
#define       MC_CMD_START_KR_EYE_PLOT_IN_LANE_OFST 0

/* MC_CMD_START_KR_EYE_PLOT_OUT msgresponse */
#define    MC_CMD_START_KR_EYE_PLOT_OUT_LEN 0


/***********************************/
/* MC_CMD_POLL_KR_EYE_PLOT
 * Poll KR Serdes Eye diagram plot. Returns one row of BER data. The caller
 * should call this command repeatedly after starting eye plot, until no more
 * data is returned.
 */
#define MC_CMD_POLL_KR_EYE_PLOT 0xef

/* MC_CMD_POLL_KR_EYE_PLOT_IN msgrequest */
#define    MC_CMD_POLL_KR_EYE_PLOT_IN_LEN 0

/* MC_CMD_POLL_KR_EYE_PLOT_OUT msgresponse */
#define    MC_CMD_POLL_KR_EYE_PLOT_OUT_LENMIN 0
#define    MC_CMD_POLL_KR_EYE_PLOT_OUT_LENMAX 252
#define    MC_CMD_POLL_KR_EYE_PLOT_OUT_LEN(num) (0+2*(num))
#define       MC_CMD_POLL_KR_EYE_PLOT_OUT_SAMPLES_OFST 0
#define       MC_CMD_POLL_KR_EYE_PLOT_OUT_SAMPLES_LEN 2
#define       MC_CMD_POLL_KR_EYE_PLOT_OUT_SAMPLES_MINNUM 0
#define       MC_CMD_POLL_KR_EYE_PLOT_OUT_SAMPLES_MAXNUM 126


/***********************************/
/* MC_CMD_READ_FUSES
 * Read data programmed into the device One-Time-Programmable (OTP) Fuses
 */
#define MC_CMD_READ_FUSES 0xf0

/* MC_CMD_READ_FUSES_IN msgrequest */
#define    MC_CMD_READ_FUSES_IN_LEN 8
/* Offset in OTP to read */
#define       MC_CMD_READ_FUSES_IN_OFFSET_OFST 0
/* Length of data to read in bytes */
#define       MC_CMD_READ_FUSES_IN_LENGTH_OFST 4

/* MC_CMD_READ_FUSES_OUT msgresponse */
#define    MC_CMD_READ_FUSES_OUT_LENMIN 4
#define    MC_CMD_READ_FUSES_OUT_LENMAX 252
#define    MC_CMD_READ_FUSES_OUT_LEN(num) (4+1*(num))
/* Length of returned OTP data in bytes */
#define       MC_CMD_READ_FUSES_OUT_LENGTH_OFST 0
/* Returned data */
#define       MC_CMD_READ_FUSES_OUT_DATA_OFST 4
#define       MC_CMD_READ_FUSES_OUT_DATA_LEN 1
#define       MC_CMD_READ_FUSES_OUT_DATA_MINNUM 0
#define       MC_CMD_READ_FUSES_OUT_DATA_MAXNUM 248


/***********************************/
/* MC_CMD_KR_TUNE
 * Get or set KR Serdes RXEQ and TX Driver settings
 */
#define MC_CMD_KR_TUNE 0xf1

/* MC_CMD_KR_TUNE_IN msgrequest */
#define    MC_CMD_KR_TUNE_IN_LENMIN 4
#define    MC_CMD_KR_TUNE_IN_LENMAX 252
#define    MC_CMD_KR_TUNE_IN_LEN(num) (4+4*(num))
/* Requested operation */
#define       MC_CMD_KR_TUNE_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_IN_KR_TUNE_OP_LEN 1
/* enum: Get current RXEQ settings */
#define          MC_CMD_KR_TUNE_IN_RXEQ_GET  0x0
/* enum: Override RXEQ settings */
#define          MC_CMD_KR_TUNE_IN_RXEQ_SET  0x1
/* enum: Get current TX Driver settings */
#define          MC_CMD_KR_TUNE_IN_TXEQ_GET  0x2
/* enum: Override TX Driver settings */
#define          MC_CMD_KR_TUNE_IN_TXEQ_SET  0x3
/* enum: Force KR Serdes reset / recalibration */
#define          MC_CMD_KR_TUNE_IN_RECAL  0x4
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_IN_KR_TUNE_RSVD_LEN 3
/* Arguments specific to the operation */
#define       MC_CMD_KR_TUNE_IN_KR_TUNE_ARGS_OFST 4
#define       MC_CMD_KR_TUNE_IN_KR_TUNE_ARGS_LEN 4
#define       MC_CMD_KR_TUNE_IN_KR_TUNE_ARGS_MINNUM 0
#define       MC_CMD_KR_TUNE_IN_KR_TUNE_ARGS_MAXNUM 62

/* MC_CMD_KR_TUNE_OUT msgresponse */
#define    MC_CMD_KR_TUNE_OUT_LEN 0

/* MC_CMD_KR_TUNE_RXEQ_GET_IN msgrequest */
#define    MC_CMD_KR_TUNE_RXEQ_GET_IN_LEN 4
/* Requested operation */
#define       MC_CMD_KR_TUNE_RXEQ_GET_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_RXEQ_GET_IN_KR_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_RXEQ_GET_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_RXEQ_GET_IN_KR_TUNE_RSVD_LEN 3

/* MC_CMD_KR_TUNE_RXEQ_GET_OUT msgresponse */
#define    MC_CMD_KR_TUNE_RXEQ_GET_OUT_LENMIN 4
#define    MC_CMD_KR_TUNE_RXEQ_GET_OUT_LENMAX 252
#define    MC_CMD_KR_TUNE_RXEQ_GET_OUT_LEN(num) (0+4*(num))
/* RXEQ Parameter */
#define       MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_OFST 0
#define       MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_LEN 4
#define       MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_MINNUM 1
#define       MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_MAXNUM 63
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_ID_LBN 0
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_ID_WIDTH 8
/* enum: Attenuation (0-15) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_ATT  0x0
/* enum: CTLE Boost (0-15) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_BOOST  0x1
/* enum: Edge DFE Tap1 (0 - max negative, 64 - zero, 127 - max positive) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP1  0x2
/* enum: Edge DFE Tap2 (0 - max negative, 32 - zero, 63 - max positive) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP2  0x3
/* enum: Edge DFE Tap3 (0 - max negative, 32 - zero, 63 - max positive) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP3  0x4
/* enum: Edge DFE Tap4 (0 - max negative, 32 - zero, 63 - max positive) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP4  0x5
/* enum: Edge DFE Tap5 (0 - max negative, 32 - zero, 63 - max positive) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP5  0x6
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_LANE_LBN 8
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_LANE_WIDTH 3
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_LANE_0  0x0 /* enum */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_LANE_1  0x1 /* enum */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_LANE_2  0x2 /* enum */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_LANE_3  0x3 /* enum */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_LANE_ALL  0x4 /* enum */
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_AUTOCAL_LBN 11
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_AUTOCAL_WIDTH 1
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_RESERVED_LBN 12
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_RESERVED_WIDTH 4
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_INITIAL_LBN 16
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_INITIAL_WIDTH 8
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_CURRENT_LBN 24
#define        MC_CMD_KR_TUNE_RXEQ_GET_OUT_PARAM_CURRENT_WIDTH 8

/* MC_CMD_KR_TUNE_RXEQ_SET_IN msgrequest */
#define    MC_CMD_KR_TUNE_RXEQ_SET_IN_LENMIN 8
#define    MC_CMD_KR_TUNE_RXEQ_SET_IN_LENMAX 252
#define    MC_CMD_KR_TUNE_RXEQ_SET_IN_LEN(num) (4+4*(num))
/* Requested operation */
#define       MC_CMD_KR_TUNE_RXEQ_SET_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_RXEQ_SET_IN_KR_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_RXEQ_SET_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_RXEQ_SET_IN_KR_TUNE_RSVD_LEN 3
/* RXEQ Parameter */
#define       MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_OFST 4
#define       MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_LEN 4
#define       MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_MINNUM 1
#define       MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_MAXNUM 62
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_ID_LBN 0
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_ID_WIDTH 8
/*             Enum values, see field(s): */
/*                MC_CMD_KR_TUNE_RXEQ_GET_OUT/PARAM_ID */
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_LANE_LBN 8
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_LANE_WIDTH 3
/*             Enum values, see field(s): */
/*                MC_CMD_KR_TUNE_RXEQ_GET_OUT/PARAM_LANE */
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_AUTOCAL_LBN 11
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_AUTOCAL_WIDTH 1
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_RESERVED_LBN 12
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_RESERVED_WIDTH 4
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_INITIAL_LBN 16
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_PARAM_INITIAL_WIDTH 8
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_RESERVED2_LBN 24
#define        MC_CMD_KR_TUNE_RXEQ_SET_IN_RESERVED2_WIDTH 8

/* MC_CMD_KR_TUNE_RXEQ_SET_OUT msgresponse */
#define    MC_CMD_KR_TUNE_RXEQ_SET_OUT_LEN 0

/* MC_CMD_KR_TUNE_RECAL_IN msgrequest */
#define    MC_CMD_KR_TUNE_RECAL_IN_LEN 4
/* Requested operation */
#define       MC_CMD_KR_TUNE_RECAL_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_RECAL_IN_KR_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_RECAL_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_RECAL_IN_KR_TUNE_RSVD_LEN 3

/* MC_CMD_KR_TUNE_RECAL_OUT msgresponse */
#define    MC_CMD_KR_TUNE_RECAL_OUT_LEN 0


/***********************************/
/* MC_CMD_PCIE_TUNE
 * Get or set PCIE Serdes RXEQ and TX Driver settings
 */
#define MC_CMD_PCIE_TUNE 0xf2

/* MC_CMD_PCIE_TUNE_IN msgrequest */
#define    MC_CMD_PCIE_TUNE_IN_LENMIN 4
#define    MC_CMD_PCIE_TUNE_IN_LENMAX 252
#define    MC_CMD_PCIE_TUNE_IN_LEN(num) (4+4*(num))
/* Requested operation */
#define       MC_CMD_PCIE_TUNE_IN_PCIE_TUNE_OP_OFST 0
#define       MC_CMD_PCIE_TUNE_IN_PCIE_TUNE_OP_LEN 1
/* enum: Get current RXEQ settings */
#define          MC_CMD_PCIE_TUNE_IN_RXEQ_GET  0x0
/* enum: Override RXEQ settings */
#define          MC_CMD_PCIE_TUNE_IN_RXEQ_SET  0x1
/* enum: Get current TX Driver settings */
#define          MC_CMD_PCIE_TUNE_IN_TXEQ_GET  0x2
/* enum: Override TX Driver settings */
#define          MC_CMD_PCIE_TUNE_IN_TXEQ_SET  0x3
/* Align the arguments to 32 bits */
#define       MC_CMD_PCIE_TUNE_IN_PCIE_TUNE_RSVD_OFST 1
#define       MC_CMD_PCIE_TUNE_IN_PCIE_TUNE_RSVD_LEN 3
/* Arguments specific to the operation */
#define       MC_CMD_PCIE_TUNE_IN_PCIE_TUNE_ARGS_OFST 4
#define       MC_CMD_PCIE_TUNE_IN_PCIE_TUNE_ARGS_LEN 4
#define       MC_CMD_PCIE_TUNE_IN_PCIE_TUNE_ARGS_MINNUM 0
#define       MC_CMD_PCIE_TUNE_IN_PCIE_TUNE_ARGS_MAXNUM 62

/* MC_CMD_PCIE_TUNE_OUT msgresponse */
#define    MC_CMD_PCIE_TUNE_OUT_LEN 0

/* MC_CMD_PCIE_TUNE_RXEQ_GET_IN msgrequest */
#define    MC_CMD_PCIE_TUNE_RXEQ_GET_IN_LEN 4
/* Requested operation */
#define       MC_CMD_PCIE_TUNE_RXEQ_GET_IN_PCIE_TUNE_OP_OFST 0
#define       MC_CMD_PCIE_TUNE_RXEQ_GET_IN_PCIE_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_PCIE_TUNE_RXEQ_GET_IN_PCIE_TUNE_RSVD_OFST 1
#define       MC_CMD_PCIE_TUNE_RXEQ_GET_IN_PCIE_TUNE_RSVD_LEN 3

/* MC_CMD_PCIE_TUNE_RXEQ_GET_OUT msgresponse */
#define    MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LENMIN 4
#define    MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LENMAX 252
#define    MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LEN(num) (0+4*(num))
/* RXEQ Parameter */
#define       MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_OFST 0
#define       MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_LEN 4
#define       MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_MINNUM 1
#define       MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_MAXNUM 63
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_ID_LBN 0
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_ID_WIDTH 8
/* enum: Attenuation (0-15) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_ATT  0x0
/* enum: CTLE Boost (0-15) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_BOOST  0x1
/* enum: DFE Tap1 (0 - max negative, 64 - zero, 127 - max positive) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_DFE_TAP1  0x2
/* enum: DFE Tap2 (0 - max negative, 32 - zero, 63 - max positive) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_DFE_TAP2  0x3
/* enum: DFE Tap3 (0 - max negative, 32 - zero, 63 - max positive) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_DFE_TAP3  0x4
/* enum: DFE Tap4 (0 - max negative, 32 - zero, 63 - max positive) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_DFE_TAP4  0x5
/* enum: DFE Tap5 (0 - max negative, 32 - zero, 63 - max positive) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_DFE_TAP5  0x6
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_LANE_LBN 8
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_LANE_WIDTH 4
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_0  0x0 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_1  0x1 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_2  0x2 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_3  0x3 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_4  0x4 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_5  0x5 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_6  0x6 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_7  0x7 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_ALL  0x8 /* enum */
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_RESERVED_LBN 12
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_RESERVED_WIDTH 12
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_CURRENT_LBN 24
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_CURRENT_WIDTH 8

/* MC_CMD_PCIE_TUNE_TXEQ_GET_IN msgrequest */
#define    MC_CMD_PCIE_TUNE_TXEQ_GET_IN_LEN 4
/* Requested operation */
#define       MC_CMD_PCIE_TUNE_TXEQ_GET_IN_PCIE_TUNE_OP_OFST 0
#define       MC_CMD_PCIE_TUNE_TXEQ_GET_IN_PCIE_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_PCIE_TUNE_TXEQ_GET_IN_PCIE_TUNE_RSVD_OFST 1
#define       MC_CMD_PCIE_TUNE_TXEQ_GET_IN_PCIE_TUNE_RSVD_LEN 3

/* MC_CMD_PCIE_TUNE_TXEQ_GET_OUT msgresponse */
#define    MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_LENMIN 4
#define    MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_LENMAX 252
#define    MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_LEN(num) (0+4*(num))
/* RXEQ Parameter */
#define       MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_OFST 0
#define       MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_LEN 4
#define       MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_MINNUM 1
#define       MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_MAXNUM 63
#define        MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_ID_LBN 0
#define        MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_ID_WIDTH 8
/* enum: TxMargin (PIPE) */
#define          MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_TXMARGIN  0x0
/* enum: TxSwing (PIPE) */
#define          MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_TXSWING  0x1
/* enum: De-emphasis coefficient C(-1) (PIPE) */
#define          MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_CM1  0x2
/* enum: De-emphasis coefficient C(0) (PIPE) */
#define          MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_C0  0x3
/* enum: De-emphasis coefficient C(+1) (PIPE) */
#define          MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_CP1  0x4
#define        MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_LANE_LBN 8
#define        MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_LANE_WIDTH 4
/*             Enum values, see field(s): */
/*                MC_CMD_PCIE_TUNE_RXEQ_GET_OUT/PARAM_LANE */
#define        MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_RESERVED_LBN 12
#define        MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_RESERVED_WIDTH 12
#define        MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_CURRENT_LBN 24
#define        MC_CMD_PCIE_TUNE_TXEQ_GET_OUT_PARAM_CURRENT_WIDTH 8


/***********************************/
/* MC_CMD_LICENSING
 * Operations on the NVRAM_PARTITION_TYPE_LICENSE application license partition
 */
#define MC_CMD_LICENSING 0xf3

/* MC_CMD_LICENSING_IN msgrequest */
#define    MC_CMD_LICENSING_IN_LEN 4
/* identifies the type of operation requested */
#define       MC_CMD_LICENSING_IN_OP_OFST 0
/* enum: re-read and apply licenses after a license key partition update; note
 * that this operation returns a zero-length response
 */
#define          MC_CMD_LICENSING_IN_OP_UPDATE_LICENSE  0x0
/* enum: report counts of installed licenses */
#define          MC_CMD_LICENSING_IN_OP_GET_KEY_STATS  0x1

/* MC_CMD_LICENSING_OUT msgresponse */
#define    MC_CMD_LICENSING_OUT_LEN 28
/* count of application keys which are valid */
#define       MC_CMD_LICENSING_OUT_VALID_APP_KEYS_OFST 0
/* sum of UNVERIFIABLE_APP_KEYS + WRONG_NODE_APP_KEYS (for compatibility with
 * MC_CMD_FC_OP_LICENSE)
 */
#define       MC_CMD_LICENSING_OUT_INVALID_APP_KEYS_OFST 4
/* count of application keys which are invalid due to being blacklisted */
#define       MC_CMD_LICENSING_OUT_BLACKLISTED_APP_KEYS_OFST 8
/* count of application keys which are invalid due to being unverifiable */
#define       MC_CMD_LICENSING_OUT_UNVERIFIABLE_APP_KEYS_OFST 12
/* count of application keys which are invalid due to being for the wrong node
 */
#define       MC_CMD_LICENSING_OUT_WRONG_NODE_APP_KEYS_OFST 16
/* licensing state (for diagnostics; the exact meaning of the bits in this
 * field are private to the firmware)
 */
#define       MC_CMD_LICENSING_OUT_LICENSING_STATE_OFST 20
/* licensing subsystem self-test report (for manftest) */
#define       MC_CMD_LICENSING_OUT_LICENSING_SELF_TEST_OFST 24
/* enum: licensing subsystem self-test failed */
#define          MC_CMD_LICENSING_OUT_SELF_TEST_FAIL  0x0
/* enum: licensing subsystem self-test passed */
#define          MC_CMD_LICENSING_OUT_SELF_TEST_PASS  0x1


/***********************************/
/* MC_CMD_MC2MC_PROXY
 * Execute an arbitrary MCDI command on the slave MC of a dual-core device.
 * This will fail on a single-core system.
 */
#define MC_CMD_MC2MC_PROXY 0xf4


#endif /* MCDI_PCOL_H */
