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
/* We have entered the main firmware via recovery mode.  This
 * means that MC persistent data must be reinitialised, but that
 * we shouldn't touch PCIe config. */
#define MC_FW_RECOVERY_MODE_PCIE_INIT_OK (32)
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
/* Already exists */
#define MC_CMD_ERR_EEXIST 6
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
/* Broken pipe */
#define MC_CMD_ERR_EPIPE 32
/* Read-only */
#define MC_CMD_ERR_EROFS 30
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
/* The datapath is disabled. */
#define MC_CMD_ERR_DATAPATH_DISABLED 0x100b
/* The requesting client is not a function */
#define MC_CMD_ERR_CLIENT_NOT_FN  0x100c
/* The requested operation might require the
   command to be passed between MCs, and the
   transport doesn't support that.  Should
   only ever been seen over the UART. */
#define MC_CMD_ERR_TRANSPORT_NOPROXY 0x100d
/* VLAN tag(s) exists */
#define MC_CMD_ERR_VLAN_EXIST 0x100e
/* No MAC address assigned to an EVB port */
#define MC_CMD_ERR_NO_MAC_ADDR 0x100f
/* Notifies the driver that the request has been relayed
 * to an admin function for authorization. The driver should
 * wait for a PROXY_RESPONSE event and then resend its request.
 * This error code is followed by a 32-bit handle that
 * helps matching it with the respective PROXY_RESPONSE event. */
#define MC_CMD_ERR_PROXY_PENDING 0x1010
#define MC_CMD_ERR_PROXY_PENDING_HANDLE_OFST 4
/* The request cannot be passed for authorization because
 * another request from the same function is currently being
 * authorized. The drvier should try again later. */
#define MC_CMD_ERR_PROXY_INPROGRESS 0x1011
/* Returned by MC_CMD_PROXY_COMPLETE if the caller is not the function
 * that has enabled proxying or BLOCK_INDEX points to a function that
 * doesn't await an authorization. */
#define MC_CMD_ERR_PROXY_UNEXPECTED 0x1012
/* This code is currently only used internally in FW. Its meaning is that
 * an operation failed due to lack of SR-IOV privilege.
 * Normally it is translated to EPERM by send_cmd_err(),
 * but it may also be used to trigger some special mechanism
 * for handling such case, e.g. to relay the failed request
 * to a designated admin function for authorization. */
#define MC_CMD_ERR_NO_PRIVILEGE 0x1013
/* Workaround 26807 could not be turned on/off because some functions
 * have already installed filters. See the comment at
 * MC_CMD_WORKAROUND_BUG26807. */
#define MC_CMD_ERR_FILTERS_PRESENT 0x1014
/* The clock whose frequency you've attempted to set set
 * doesn't exist on this NIC */
#define MC_CMD_ERR_NO_CLOCK 0x1015
/* Returned by MC_CMD_TESTASSERT if the action that should
 * have caused an assertion failed to do so.  */
#define MC_CMD_ERR_UNREACHABLE 0x1016

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
#define MEDFORD_MC_BOOTROM_COPYCODE_VEC (0x10000 - 3 * 0x4)
/* Points to the recovery mode entry point. */
#define SIENA_MC_BOOTROM_NOFLASH_VEC (0x800 - 2 * 0x4)
#define HUNT_MC_BOOTROM_NOFLASH_VEC (0x8000 - 2 * 0x4)
#define MEDFORD_MC_BOOTROM_NOFLASH_VEC (0x10000 - 2 * 0x4)

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

/* This may be ORed with an EVB_PORT_ID_xxx constant to pass a non-default
 * stack ID (which must be in the range 1-255) along with an EVB port ID.
 */
#define EVB_STACK_ID(n)  (((n) & 0xff) << 16)


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
/* enum: DDR ECC status update */
#define          MCDI_EVENT_AOE_DDR_ECC_STATUS 0xa
/* enum: PTP status update */
#define          MCDI_EVENT_AOE_PTP_STATUS 0xb
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
#define        MCDI_EVENT_MUM_ERR_TYPE_LBN 0
#define        MCDI_EVENT_MUM_ERR_TYPE_WIDTH 8
/* enum: MUM failed to load - no valid image? */
#define          MCDI_EVENT_MUM_NO_LOAD 0x1
/* enum: MUM f/w reported an exception */
#define          MCDI_EVENT_MUM_ASSERT 0x2
/* enum: MUM not kicking watchdog */
#define          MCDI_EVENT_MUM_WATCHDOG 0x3
#define        MCDI_EVENT_MUM_ERR_DATA_LBN 8
#define        MCDI_EVENT_MUM_ERR_DATA_WIDTH 8
#define       MCDI_EVENT_DATA_LBN 0
#define       MCDI_EVENT_DATA_WIDTH 32
#define       MCDI_EVENT_SRC_LBN 36
#define       MCDI_EVENT_SRC_WIDTH 8
#define       MCDI_EVENT_EV_CODE_LBN 60
#define       MCDI_EVENT_EV_CODE_WIDTH 4
#define       MCDI_EVENT_CODE_LBN 44
#define       MCDI_EVENT_CODE_WIDTH 8
/* enum: Event generated by host software */
#define          MCDI_EVENT_SW_EVENT 0x0
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
/* enum: The MC has entered offline BIST mode */
#define          MCDI_EVENT_CODE_MC_BIST 0x19
/* enum: PTP tick event providing current NIC time */
#define          MCDI_EVENT_CODE_PTP_TIME 0x1a
/* enum: MUM fault */
#define          MCDI_EVENT_CODE_MUM 0x1b
/* enum: notify the designated PF of a new authorization request */
#define          MCDI_EVENT_CODE_PROXY_REQUEST 0x1c
/* enum: notify a function that awaits an authorization that its request has
 * been processed and it may now resend the command
 */
#define          MCDI_EVENT_CODE_PROXY_RESPONSE 0x1d
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
/* For CODE_PTP_RX, CODE_PTP_PPS and CODE_HW_PPS events the seconds field of
 * timestamp
 */
#define       MCDI_EVENT_PTP_SECONDS_OFST 0
#define       MCDI_EVENT_PTP_SECONDS_LBN 0
#define       MCDI_EVENT_PTP_SECONDS_WIDTH 32
/* For CODE_PTP_RX, CODE_PTP_PPS and CODE_HW_PPS events the major field of
 * timestamp
 */
#define       MCDI_EVENT_PTP_MAJOR_OFST 0
#define       MCDI_EVENT_PTP_MAJOR_LBN 0
#define       MCDI_EVENT_PTP_MAJOR_WIDTH 32
/* For CODE_PTP_RX, CODE_PTP_PPS and CODE_HW_PPS events the nanoseconds field
 * of timestamp
 */
#define       MCDI_EVENT_PTP_NANOSECONDS_OFST 0
#define       MCDI_EVENT_PTP_NANOSECONDS_LBN 0
#define       MCDI_EVENT_PTP_NANOSECONDS_WIDTH 32
/* For CODE_PTP_RX, CODE_PTP_PPS and CODE_HW_PPS events the minor field of
 * timestamp
 */
#define       MCDI_EVENT_PTP_MINOR_OFST 0
#define       MCDI_EVENT_PTP_MINOR_LBN 0
#define       MCDI_EVENT_PTP_MINOR_WIDTH 32
/* For CODE_PTP_RX events, the lowest four bytes of sourceUUID from PTP packet
 */
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
/* For CODE_PTP_TIME events, the major value of the PTP clock */
#define       MCDI_EVENT_PTP_TIME_MAJOR_OFST 0
#define       MCDI_EVENT_PTP_TIME_MAJOR_LBN 0
#define       MCDI_EVENT_PTP_TIME_MAJOR_WIDTH 32
/* For CODE_PTP_TIME events, bits 19-26 of the minor value of the PTP clock */
#define       MCDI_EVENT_PTP_TIME_MINOR_26_19_LBN 36
#define       MCDI_EVENT_PTP_TIME_MINOR_26_19_WIDTH 8
/* For CODE_PTP_TIME events where report sync status is enabled, indicates
 * whether the NIC clock has ever been set
 */
#define       MCDI_EVENT_PTP_TIME_NIC_CLOCK_VALID_LBN 36
#define       MCDI_EVENT_PTP_TIME_NIC_CLOCK_VALID_WIDTH 1
/* For CODE_PTP_TIME events where report sync status is enabled, indicates
 * whether the NIC and System clocks are in sync
 */
#define       MCDI_EVENT_PTP_TIME_HOST_NIC_IN_SYNC_LBN 37
#define       MCDI_EVENT_PTP_TIME_HOST_NIC_IN_SYNC_WIDTH 1
/* For CODE_PTP_TIME events where report sync status is enabled, bits 21-26 of
 * the minor value of the PTP clock
 */
#define       MCDI_EVENT_PTP_TIME_MINOR_26_21_LBN 38
#define       MCDI_EVENT_PTP_TIME_MINOR_26_21_WIDTH 6
#define       MCDI_EVENT_PROXY_REQUEST_BUFF_INDEX_OFST 0
#define       MCDI_EVENT_PROXY_REQUEST_BUFF_INDEX_LBN 0
#define       MCDI_EVENT_PROXY_REQUEST_BUFF_INDEX_WIDTH 32
#define       MCDI_EVENT_PROXY_RESPONSE_HANDLE_OFST 0
#define       MCDI_EVENT_PROXY_RESPONSE_HANDLE_LBN 0
#define       MCDI_EVENT_PROXY_RESPONSE_HANDLE_WIDTH 32
/* Zero means that the request has been completed or authorized, and the driver
 * should resend it. A non-zero value means that the authorization has been
 * denied, and gives the reason. Typically it will be EPERM.
 */
#define       MCDI_EVENT_PROXY_RESPONSE_RC_LBN 36
#define       MCDI_EVENT_PROXY_RESPONSE_RC_WIDTH 8

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
/* enum: Tick event from PTP clock */
#define          FCDI_EVENT_CODE_PTP_TICK 0x7
/* enum: ECC error counters */
#define          FCDI_EVENT_CODE_DDR_ECC_STATUS 0x8
/* enum: Current status of PTP */
#define          FCDI_EVENT_CODE_PTP_STATUS 0x9
/* enum: Port id config to map MC-FC port idx */
#define          FCDI_EVENT_CODE_PORT_CONFIG 0xa
/* enum: Boot result or error code */
#define          FCDI_EVENT_CODE_BOOT_RESULT 0xb
#define       FCDI_EVENT_REBOOT_SRC_LBN 36
#define       FCDI_EVENT_REBOOT_SRC_WIDTH 8
#define          FCDI_EVENT_REBOOT_FC_FW 0x0 /* enum */
#define          FCDI_EVENT_REBOOT_FC_BOOTLOADER 0x1 /* enum */
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
#define       FCDI_EVENT_PTP_STATE_OFST 0
#define          FCDI_EVENT_PTP_UNDEFINED 0x0 /* enum */
#define          FCDI_EVENT_PTP_SETUP_FAILED 0x1 /* enum */
#define          FCDI_EVENT_PTP_OPERATIONAL 0x2 /* enum */
#define       FCDI_EVENT_PTP_STATE_LBN 0
#define       FCDI_EVENT_PTP_STATE_WIDTH 32
#define       FCDI_EVENT_DDR_ECC_STATUS_BANK_ID_LBN 36
#define       FCDI_EVENT_DDR_ECC_STATUS_BANK_ID_WIDTH 8
#define       FCDI_EVENT_DDR_ECC_STATUS_STATUS_OFST 0
#define       FCDI_EVENT_DDR_ECC_STATUS_STATUS_LBN 0
#define       FCDI_EVENT_DDR_ECC_STATUS_STATUS_WIDTH 32
/* Index of MC port being referred to */
#define       FCDI_EVENT_PORT_CONFIG_SRC_LBN 36
#define       FCDI_EVENT_PORT_CONFIG_SRC_WIDTH 8
/* FC Port index that matches the MC port index in SRC */
#define       FCDI_EVENT_PORT_CONFIG_DATA_OFST 0
#define       FCDI_EVENT_PORT_CONFIG_DATA_LBN 0
#define       FCDI_EVENT_PORT_CONFIG_DATA_WIDTH 32
#define       FCDI_EVENT_BOOT_RESULT_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_AOE/MC_CMD_AOE_OUT_INFO/FC_BOOT_RESULT */
#define       FCDI_EVENT_BOOT_RESULT_LBN 0
#define       FCDI_EVENT_BOOT_RESULT_WIDTH 32

/* FCDI_EXTENDED_EVENT_PPS structuredef: Extended FCDI event to send PPS events
 * to the MC. Note that this structure | is overlayed over a normal FCDI event
 * such that bits 32-63 containing | event code, level, source etc remain the
 * same. In this case the data | field of the header is defined to be the
 * number of timestamps
 */
#define    FCDI_EXTENDED_EVENT_PPS_LENMIN 16
#define    FCDI_EXTENDED_EVENT_PPS_LENMAX 248
#define    FCDI_EXTENDED_EVENT_PPS_LEN(num) (8+8*(num))
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
#define       FCDI_EXTENDED_EVENT_PPS_TIMESTAMPS_OFST 8
#define       FCDI_EXTENDED_EVENT_PPS_TIMESTAMPS_LEN 8
#define       FCDI_EXTENDED_EVENT_PPS_TIMESTAMPS_LO_OFST 8
#define       FCDI_EXTENDED_EVENT_PPS_TIMESTAMPS_HI_OFST 12
#define       FCDI_EXTENDED_EVENT_PPS_TIMESTAMPS_MINNUM 1
#define       FCDI_EXTENDED_EVENT_PPS_TIMESTAMPS_MAXNUM 30
#define       FCDI_EXTENDED_EVENT_PPS_TIMESTAMPS_LBN 64
#define       FCDI_EXTENDED_EVENT_PPS_TIMESTAMPS_WIDTH 64

/* MUM_EVENT structuredef */
#define    MUM_EVENT_LEN 8
#define       MUM_EVENT_CONT_LBN 32
#define       MUM_EVENT_CONT_WIDTH 1
#define       MUM_EVENT_LEVEL_LBN 33
#define       MUM_EVENT_LEVEL_WIDTH 3
/* enum: Info. */
#define          MUM_EVENT_LEVEL_INFO  0x0
/* enum: Warning. */
#define          MUM_EVENT_LEVEL_WARN 0x1
/* enum: Error. */
#define          MUM_EVENT_LEVEL_ERR 0x2
/* enum: Fatal. */
#define          MUM_EVENT_LEVEL_FATAL 0x3
#define       MUM_EVENT_DATA_OFST 0
#define        MUM_EVENT_SENSOR_ID_LBN 0
#define        MUM_EVENT_SENSOR_ID_WIDTH 8
/*             Enum values, see field(s): */
/*                MC_CMD_SENSOR_INFO/MC_CMD_SENSOR_INFO_OUT/MASK */
#define        MUM_EVENT_SENSOR_STATE_LBN 8
#define        MUM_EVENT_SENSOR_STATE_WIDTH 8
#define        MUM_EVENT_PORT_PHY_READY_LBN 0
#define        MUM_EVENT_PORT_PHY_READY_WIDTH 1
#define        MUM_EVENT_PORT_PHY_LINK_UP_LBN 1
#define        MUM_EVENT_PORT_PHY_LINK_UP_WIDTH 1
#define        MUM_EVENT_PORT_PHY_TX_LOL_LBN 2
#define        MUM_EVENT_PORT_PHY_TX_LOL_WIDTH 1
#define        MUM_EVENT_PORT_PHY_RX_LOL_LBN 3
#define        MUM_EVENT_PORT_PHY_RX_LOL_WIDTH 1
#define        MUM_EVENT_PORT_PHY_TX_LOS_LBN 4
#define        MUM_EVENT_PORT_PHY_TX_LOS_WIDTH 1
#define        MUM_EVENT_PORT_PHY_RX_LOS_LBN 5
#define        MUM_EVENT_PORT_PHY_RX_LOS_WIDTH 1
#define        MUM_EVENT_PORT_PHY_TX_FAULT_LBN 6
#define        MUM_EVENT_PORT_PHY_TX_FAULT_WIDTH 1
#define       MUM_EVENT_DATA_LBN 0
#define       MUM_EVENT_DATA_WIDTH 32
#define       MUM_EVENT_SRC_LBN 36
#define       MUM_EVENT_SRC_WIDTH 8
#define       MUM_EVENT_EV_CODE_LBN 60
#define       MUM_EVENT_EV_CODE_WIDTH 4
#define       MUM_EVENT_CODE_LBN 44
#define       MUM_EVENT_CODE_WIDTH 8
/* enum: The MUM was rebooted. */
#define          MUM_EVENT_CODE_REBOOT 0x1
/* enum: Bad assert. */
#define          MUM_EVENT_CODE_ASSERT 0x2
/* enum: Sensor failure. */
#define          MUM_EVENT_CODE_SENSOR 0x3
/* enum: Link fault has been asserted, or has cleared. */
#define          MUM_EVENT_CODE_QSFP_LASI_INTERRUPT 0x4
#define       MUM_EVENT_SENSOR_DATA_OFST 0
#define       MUM_EVENT_SENSOR_DATA_LBN 0
#define       MUM_EVENT_SENSOR_DATA_WIDTH 32
#define       MUM_EVENT_PORT_PHY_FLAGS_OFST 0
#define       MUM_EVENT_PORT_PHY_FLAGS_LBN 0
#define       MUM_EVENT_PORT_PHY_FLAGS_WIDTH 32
#define       MUM_EVENT_PORT_PHY_COPPER_LEN_OFST 0
#define       MUM_EVENT_PORT_PHY_COPPER_LEN_LBN 0
#define       MUM_EVENT_PORT_PHY_COPPER_LEN_WIDTH 32
#define       MUM_EVENT_PORT_PHY_CAPS_OFST 0
#define       MUM_EVENT_PORT_PHY_CAPS_LBN 0
#define       MUM_EVENT_PORT_PHY_CAPS_WIDTH 32
#define       MUM_EVENT_PORT_PHY_TECH_OFST 0
#define          MUM_EVENT_PORT_PHY_STATE_QSFP_MODULE_TECH_UNKNOWN 0x0 /* enum */
#define          MUM_EVENT_PORT_PHY_STATE_QSFP_MODULE_TECH_OPTICAL 0x1 /* enum */
#define          MUM_EVENT_PORT_PHY_STATE_QSFP_MODULE_TECH_COPPER_PASSIVE 0x2 /* enum */
#define          MUM_EVENT_PORT_PHY_STATE_QSFP_MODULE_TECH_COPPER_PASSIVE_EQUALIZED 0x3 /* enum */
#define          MUM_EVENT_PORT_PHY_STATE_QSFP_MODULE_TECH_COPPER_ACTIVE_LIMITING 0x4 /* enum */
#define          MUM_EVENT_PORT_PHY_STATE_QSFP_MODULE_TECH_COPPER_ACTIVE_LINEAR 0x5 /* enum */
#define          MUM_EVENT_PORT_PHY_STATE_QSFP_MODULE_TECH_BASE_T 0x6 /* enum */
#define          MUM_EVENT_PORT_PHY_STATE_QSFP_MODULE_TECH_LOOPBACK_PASSIVE 0x7 /* enum */
#define       MUM_EVENT_PORT_PHY_TECH_LBN 0
#define       MUM_EVENT_PORT_PHY_TECH_WIDTH 32
#define       MUM_EVENT_PORT_PHY_SRC_DATA_ID_LBN 36
#define       MUM_EVENT_PORT_PHY_SRC_DATA_ID_WIDTH 4
#define          MUM_EVENT_PORT_PHY_SRC_DATA_ID_FLAGS 0x0 /* enum */
#define          MUM_EVENT_PORT_PHY_SRC_DATA_ID_COPPER_LEN 0x1 /* enum */
#define          MUM_EVENT_PORT_PHY_SRC_DATA_ID_CAPS 0x2 /* enum */
#define          MUM_EVENT_PORT_PHY_SRC_DATA_ID_TECH 0x3 /* enum */
#define          MUM_EVENT_PORT_PHY_SRC_DATA_ID_MAX 0x4 /* enum */
#define       MUM_EVENT_PORT_PHY_SRC_PORT_NO_LBN 40
#define       MUM_EVENT_PORT_PHY_SRC_PORT_NO_WIDTH 4


/***********************************/
/* MC_CMD_READ32
 * Read multiple 32byte words from MC memory.
 */
#define MC_CMD_READ32 0x1

#define MC_CMD_0x1_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x2_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x3_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_COPYCODE_IN msgrequest */
#define    MC_CMD_COPYCODE_IN_LEN 16
/* Source address
 *
 * The main image should be entered via a copy of a single word from and to a
 * magic address, which controls various aspects of the boot. The magic address
 * is a bitfield, with each bit as documented below.
 */
#define       MC_CMD_COPYCODE_IN_SRC_ADDR_OFST 0
/* enum: Deprecated; equivalent to setting BOOT_MAGIC_PRESENT (see below) */
#define          MC_CMD_COPYCODE_HUNT_NO_MAGIC_ADDR 0x10000
/* enum: Deprecated; equivalent to setting BOOT_MAGIC_PRESENT and
 * BOOT_MAGIC_SATELLITE_CPUS_NOT_LOADED (see below)
 */
#define          MC_CMD_COPYCODE_HUNT_NO_DATAPATH_MAGIC_ADDR 0x1d0d0
/* enum: Deprecated; equivalent to setting BOOT_MAGIC_PRESENT,
 * BOOT_MAGIC_SATELLITE_CPUS_NOT_LOADED and BOOT_MAGIC_IGNORE_CONFIG (see
 * below)
 */
#define          MC_CMD_COPYCODE_HUNT_IGNORE_CONFIG_MAGIC_ADDR 0x1badc
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_PRESENT_LBN 17
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_PRESENT_WIDTH 1
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_SATELLITE_CPUS_NOT_LOADED_LBN 2
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_SATELLITE_CPUS_NOT_LOADED_WIDTH 1
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_IGNORE_CONFIG_LBN 3
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_IGNORE_CONFIG_WIDTH 1
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_SKIP_BOOT_ICORE_SYNC_LBN 4
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_SKIP_BOOT_ICORE_SYNC_WIDTH 1
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_FORCE_STANDALONE_LBN 5
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_FORCE_STANDALONE_WIDTH 1
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_DISABLE_XIP_LBN 6
#define        MC_CMD_COPYCODE_IN_BOOT_MAGIC_DISABLE_XIP_WIDTH 1
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

#define MC_CMD_0x4_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x5_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x6_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
/* enum: A magic value hinting that the value in this register at the time of
 * the failure has likely been lost.
 */
#define          MC_CMD_GET_ASSERTS_REG_NO_DATA 0xda7a1057
/* Failing thread address */
#define       MC_CMD_GET_ASSERTS_OUT_THREAD_OFFS_OFST 132
#define       MC_CMD_GET_ASSERTS_OUT_RESERVED_OFST 136


/***********************************/
/* MC_CMD_LOG_CTRL
 * Configure the output stream for log events such as link state changes,
 * sensor notifications and MCDI completions
 */
#define MC_CMD_LOG_CTRL 0x7

#define MC_CMD_0x7_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_LOG_CTRL_IN msgrequest */
#define    MC_CMD_LOG_CTRL_IN_LEN 8
/* Log destination */
#define       MC_CMD_LOG_CTRL_IN_LOG_DEST_OFST 0
/* enum: UART. */
#define          MC_CMD_LOG_CTRL_IN_LOG_DEST_UART 0x1
/* enum: Event queue. */
#define          MC_CMD_LOG_CTRL_IN_LOG_DEST_EVQ 0x2
/* Legacy argument. Must be zero. */
#define       MC_CMD_LOG_CTRL_IN_LOG_DEST_EVQ_OFST 4

/* MC_CMD_LOG_CTRL_OUT msgresponse */
#define    MC_CMD_LOG_CTRL_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_VERSION
 * Get version information about the MC firmware.
 */
#define MC_CMD_GET_VERSION 0x8

#define MC_CMD_0x8_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xb_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* enum: Get the time format used by this NIC for PTP operations */
#define          MC_CMD_PTP_OP_GET_TIME_FORMAT 0x16
/* enum: Get the clock attributes. NOTE- extended version of
 * MC_CMD_PTP_OP_GET_TIME_FORMAT
 */
#define          MC_CMD_PTP_OP_GET_ATTRIBUTES 0x16
/* enum: Get corrections that should be applied to the various different
 * timestamps
 */
#define          MC_CMD_PTP_OP_GET_TIMESTAMP_CORRECTIONS 0x17
/* enum: Subscribe to receive periodic time events indicating the current NIC
 * time
 */
#define          MC_CMD_PTP_OP_TIME_EVENT_SUBSCRIBE 0x18
/* enum: Unsubscribe to stop receiving time events */
#define          MC_CMD_PTP_OP_TIME_EVENT_UNSUBSCRIBE 0x19
/* enum: PPS based manfacturing tests. Requires PPS output to be looped to PPS
 * input on the same NIC.
 */
#define          MC_CMD_PTP_OP_MANFTEST_PPS 0x1a
/* enum: Set the PTP sync status. Status is used by firmware to report to event
 * subscribers.
 */
#define          MC_CMD_PTP_OP_SET_SYNC_STATUS 0x1b
/* enum: Above this for future use. */
#define          MC_CMD_PTP_OP_MAX 0x1c

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
/* Time adjustment major value */
#define       MC_CMD_PTP_IN_ADJUST_MAJOR_OFST 16
/* Time adjustment in nanoseconds */
#define       MC_CMD_PTP_IN_ADJUST_NANOSECONDS_OFST 20
/* Time adjustment minor value */
#define       MC_CMD_PTP_IN_ADJUST_MINOR_OFST 20

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
/* Time adjustment major value */
#define       MC_CMD_PTP_IN_CLOCK_OFFSET_ADJUST_MAJOR_OFST 8
/* Time adjustment in nanoseconds */
#define       MC_CMD_PTP_IN_CLOCK_OFFSET_ADJUST_NANOSECONDS_OFST 12
/* Time adjustment minor value */
#define       MC_CMD_PTP_IN_CLOCK_OFFSET_ADJUST_MINOR_OFST 12

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
/* Queue id to send events back */
#define       MC_CMD_PTP_IN_PPS_ENABLE_QUEUE_ID_OFST 8

/* MC_CMD_PTP_IN_GET_TIME_FORMAT msgrequest */
#define    MC_CMD_PTP_IN_GET_TIME_FORMAT_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_GET_ATTRIBUTES msgrequest */
#define    MC_CMD_PTP_IN_GET_ATTRIBUTES_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_GET_TIMESTAMP_CORRECTIONS msgrequest */
#define    MC_CMD_PTP_IN_GET_TIMESTAMP_CORRECTIONS_LEN 8
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */

/* MC_CMD_PTP_IN_TIME_EVENT_SUBSCRIBE msgrequest */
#define    MC_CMD_PTP_IN_TIME_EVENT_SUBSCRIBE_LEN 12
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* Original field containing queue ID. Now extended to include flags. */
#define       MC_CMD_PTP_IN_TIME_EVENT_SUBSCRIBE_QUEUE_OFST 8
#define        MC_CMD_PTP_IN_TIME_EVENT_SUBSCRIBE_QUEUE_ID_LBN 0
#define        MC_CMD_PTP_IN_TIME_EVENT_SUBSCRIBE_QUEUE_ID_WIDTH 16
#define        MC_CMD_PTP_IN_TIME_EVENT_SUBSCRIBE_REPORT_SYNC_STATUS_LBN 31
#define        MC_CMD_PTP_IN_TIME_EVENT_SUBSCRIBE_REPORT_SYNC_STATUS_WIDTH 1

/* MC_CMD_PTP_IN_TIME_EVENT_UNSUBSCRIBE msgrequest */
#define    MC_CMD_PTP_IN_TIME_EVENT_UNSUBSCRIBE_LEN 16
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* Unsubscribe options */
#define       MC_CMD_PTP_IN_TIME_EVENT_UNSUBSCRIBE_CONTROL_OFST 8
/* enum: Unsubscribe a single queue */
#define          MC_CMD_PTP_IN_TIME_EVENT_UNSUBSCRIBE_SINGLE 0x0
/* enum: Unsubscribe all queues */
#define          MC_CMD_PTP_IN_TIME_EVENT_UNSUBSCRIBE_ALL 0x1
/* Event queue ID */
#define       MC_CMD_PTP_IN_TIME_EVENT_UNSUBSCRIBE_QUEUE_OFST 12

/* MC_CMD_PTP_IN_MANFTEST_PPS msgrequest */
#define    MC_CMD_PTP_IN_MANFTEST_PPS_LEN 12
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* 1 to enable PPS test mode, 0 to disable and return result. */
#define       MC_CMD_PTP_IN_MANFTEST_PPS_TEST_ENABLE_OFST 8

/* MC_CMD_PTP_IN_SET_SYNC_STATUS msgrequest */
#define    MC_CMD_PTP_IN_SET_SYNC_STATUS_LEN 24
/*            MC_CMD_PTP_IN_CMD_OFST 0 */
/*            MC_CMD_PTP_IN_PERIPH_ID_OFST 4 */
/* NIC - Host System Clock Synchronization status */
#define       MC_CMD_PTP_IN_SET_SYNC_STATUS_STATUS_OFST 8
/* enum: Host System clock and NIC clock are not in sync */
#define          MC_CMD_PTP_IN_SET_SYNC_STATUS_NOT_IN_SYNC 0x0
/* enum: Host System clock and NIC clock are synchronized */
#define          MC_CMD_PTP_IN_SET_SYNC_STATUS_IN_SYNC 0x1
/* If synchronized, number of seconds until clocks should be considered to be
 * no longer in sync.
 */
#define       MC_CMD_PTP_IN_SET_SYNC_STATUS_TIMEOUT_OFST 12
#define       MC_CMD_PTP_IN_SET_SYNC_STATUS_RESERVED0_OFST 16
#define       MC_CMD_PTP_IN_SET_SYNC_STATUS_RESERVED1_OFST 20

/* MC_CMD_PTP_OUT msgresponse */
#define    MC_CMD_PTP_OUT_LEN 0

/* MC_CMD_PTP_OUT_TRANSMIT msgresponse */
#define    MC_CMD_PTP_OUT_TRANSMIT_LEN 8
/* Value of seconds timestamp */
#define       MC_CMD_PTP_OUT_TRANSMIT_SECONDS_OFST 0
/* Timestamp major value */
#define       MC_CMD_PTP_OUT_TRANSMIT_MAJOR_OFST 0
/* Value of nanoseconds timestamp */
#define       MC_CMD_PTP_OUT_TRANSMIT_NANOSECONDS_OFST 4
/* Timestamp minor value */
#define       MC_CMD_PTP_OUT_TRANSMIT_MINOR_OFST 4

/* MC_CMD_PTP_OUT_TIME_EVENT_SUBSCRIBE msgresponse */
#define    MC_CMD_PTP_OUT_TIME_EVENT_SUBSCRIBE_LEN 0

/* MC_CMD_PTP_OUT_TIME_EVENT_UNSUBSCRIBE msgresponse */
#define    MC_CMD_PTP_OUT_TIME_EVENT_UNSUBSCRIBE_LEN 0

/* MC_CMD_PTP_OUT_READ_NIC_TIME msgresponse */
#define    MC_CMD_PTP_OUT_READ_NIC_TIME_LEN 8
/* Value of seconds timestamp */
#define       MC_CMD_PTP_OUT_READ_NIC_TIME_SECONDS_OFST 0
/* Timestamp major value */
#define       MC_CMD_PTP_OUT_READ_NIC_TIME_MAJOR_OFST 0
/* Value of nanoseconds timestamp */
#define       MC_CMD_PTP_OUT_READ_NIC_TIME_NANOSECONDS_OFST 4
/* Timestamp minor value */
#define       MC_CMD_PTP_OUT_READ_NIC_TIME_MINOR_OFST 4

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
/* Minimum period of PPS pulse in nanoseconds */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MIN_OFST 32
/* Maximum period of PPS pulse in nanoseconds */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MAX_OFST 36
/* Last period of PPS pulse in nanoseconds */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_LAST_OFST 40
/* Mean period of PPS pulse in nanoseconds */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_PER_MEAN_OFST 44
/* Minimum offset of PPS pulse in nanoseconds (signed) */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_MIN_OFST 48
/* Maximum offset of PPS pulse in nanoseconds (signed) */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_MAX_OFST 52
/* Last offset of PPS pulse in nanoseconds (signed) */
#define       MC_CMD_PTP_OUT_STATUS_STATS_PPS_OFF_LAST_OFST 56
/* Mean offset of PPS pulse in nanoseconds (signed) */
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
/* Timestamp major value */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_MAJOR_OFST 4
/* Value of nanoseconds timestamp */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_NANOSECONDS_OFST 8
/* Timestamp minor value */
#define       MC_CMD_PTP_OUT_SYNCHRONIZE_MINOR_OFST 8
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
/* enum: Insufficient PPS events to perform checks */
#define          MC_CMD_PTP_MANF_PPS_ENOUGH 0xa
/* enum: PPS time event period not sufficiently close to 1s. */
#define          MC_CMD_PTP_MANF_PPS_PERIOD 0xb
/* enum: PPS time event nS reading not sufficiently close to zero. */
#define          MC_CMD_PTP_MANF_PPS_NS 0xc
/* enum: PTP peripheral registers incorrect */
#define          MC_CMD_PTP_MANF_REGISTERS 0xd
/* enum: Failed to read time from PTP peripheral */
#define          MC_CMD_PTP_MANF_CLOCK_READ 0xe
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

/* MC_CMD_PTP_OUT_GET_TIME_FORMAT msgresponse */
#define    MC_CMD_PTP_OUT_GET_TIME_FORMAT_LEN 4
/* Time format required/used by for this NIC. Applies to all PTP MCDI
 * operations that pass times between the host and firmware. If this operation
 * is not supported (older firmware) a format of seconds and nanoseconds should
 * be assumed.
 */
#define       MC_CMD_PTP_OUT_GET_TIME_FORMAT_FORMAT_OFST 0
/* enum: Times are in seconds and nanoseconds */
#define          MC_CMD_PTP_OUT_GET_TIME_FORMAT_SECONDS_NANOSECONDS 0x0
/* enum: Major register has units of 16 second per tick, minor 8 ns per tick */
#define          MC_CMD_PTP_OUT_GET_TIME_FORMAT_16SECONDS_8NANOSECONDS 0x1
/* enum: Major register has units of seconds, minor 2^-27s per tick */
#define          MC_CMD_PTP_OUT_GET_TIME_FORMAT_SECONDS_27FRACTION 0x2

/* MC_CMD_PTP_OUT_GET_ATTRIBUTES msgresponse */
#define    MC_CMD_PTP_OUT_GET_ATTRIBUTES_LEN 24
/* Time format required/used by for this NIC. Applies to all PTP MCDI
 * operations that pass times between the host and firmware. If this operation
 * is not supported (older firmware) a format of seconds and nanoseconds should
 * be assumed.
 */
#define       MC_CMD_PTP_OUT_GET_ATTRIBUTES_TIME_FORMAT_OFST 0
/* enum: Times are in seconds and nanoseconds */
#define          MC_CMD_PTP_OUT_GET_ATTRIBUTES_SECONDS_NANOSECONDS 0x0
/* enum: Major register has units of 16 second per tick, minor 8 ns per tick */
#define          MC_CMD_PTP_OUT_GET_ATTRIBUTES_16SECONDS_8NANOSECONDS 0x1
/* enum: Major register has units of seconds, minor 2^-27s per tick */
#define          MC_CMD_PTP_OUT_GET_ATTRIBUTES_SECONDS_27FRACTION 0x2
/* Minimum acceptable value for a corrected synchronization timeset. When
 * comparing host and NIC clock times, the MC returns a set of samples that
 * contain the host start and end time, the MC time when the host start was
 * detected and the time the MC waited between reading the time and detecting
 * the host end. The corrected sync window is the difference between the host
 * end and start times minus the time that the MC waited for host end.
 */
#define       MC_CMD_PTP_OUT_GET_ATTRIBUTES_SYNC_WINDOW_MIN_OFST 4
/* Various PTP capabilities */
#define       MC_CMD_PTP_OUT_GET_ATTRIBUTES_CAPABILITIES_OFST 8
#define        MC_CMD_PTP_OUT_GET_ATTRIBUTES_REPORT_SYNC_STATUS_LBN 0
#define        MC_CMD_PTP_OUT_GET_ATTRIBUTES_REPORT_SYNC_STATUS_WIDTH 1
#define        MC_CMD_PTP_OUT_GET_ATTRIBUTES_RX_TSTAMP_OOB_LBN 1
#define        MC_CMD_PTP_OUT_GET_ATTRIBUTES_RX_TSTAMP_OOB_WIDTH 1
#define       MC_CMD_PTP_OUT_GET_ATTRIBUTES_RESERVED0_OFST 12
#define       MC_CMD_PTP_OUT_GET_ATTRIBUTES_RESERVED1_OFST 16
#define       MC_CMD_PTP_OUT_GET_ATTRIBUTES_RESERVED2_OFST 20

/* MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS msgresponse */
#define    MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_LEN 16
/* Uncorrected error on PTP transmit timestamps in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_TRANSMIT_OFST 0
/* Uncorrected error on PTP receive timestamps in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_RECEIVE_OFST 4
/* Uncorrected error on PPS output in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_PPS_OUT_OFST 8
/* Uncorrected error on PPS input in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_PPS_IN_OFST 12

/* MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2 msgresponse */
#define    MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_LEN 24
/* Uncorrected error on PTP transmit timestamps in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_PTP_TX_OFST 0
/* Uncorrected error on PTP receive timestamps in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_PTP_RX_OFST 4
/* Uncorrected error on PPS output in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_PPS_OUT_OFST 8
/* Uncorrected error on PPS input in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_PPS_IN_OFST 12
/* Uncorrected error on non-PTP transmit timestamps in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_GENERAL_TX_OFST 16
/* Uncorrected error on non-PTP receive timestamps in NIC clock format */
#define       MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_GENERAL_RX_OFST 20

/* MC_CMD_PTP_OUT_MANFTEST_PPS msgresponse */
#define    MC_CMD_PTP_OUT_MANFTEST_PPS_LEN 4
/* Results of testing */
#define       MC_CMD_PTP_OUT_MANFTEST_PPS_TEST_RESULT_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_PTP_OUT_MANFTEST_BASIC/TEST_RESULT */

/* MC_CMD_PTP_OUT_SET_SYNC_STATUS msgresponse */
#define    MC_CMD_PTP_OUT_SET_SYNC_STATUS_LEN 0


/***********************************/
/* MC_CMD_CSR_READ32
 * Read 32bit words from the indirect memory map.
 */
#define MC_CMD_CSR_READ32 0xc

#define MC_CMD_0xc_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xd_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x54_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xf_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x10_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x11_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x12_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x18_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x19_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x1a_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x1c_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_DRV_ATTACH_IN msgrequest */
#define    MC_CMD_DRV_ATTACH_IN_LEN 12
/* new state to set if UPDATE=1 */
#define       MC_CMD_DRV_ATTACH_IN_NEW_STATE_OFST 0
#define        MC_CMD_DRV_ATTACH_LBN 0
#define        MC_CMD_DRV_ATTACH_WIDTH 1
#define        MC_CMD_DRV_PREBOOT_LBN 1
#define        MC_CMD_DRV_PREBOOT_WIDTH 1
/* 1 to set new state, or 0 to just report the existing state */
#define       MC_CMD_DRV_ATTACH_IN_UPDATE_OFST 4
/* preferred datapath firmware (for Huntington; ignored for Siena) */
#define       MC_CMD_DRV_ATTACH_IN_FIRMWARE_ID_OFST 8
/* enum: Prefer to use full featured firmware */
#define          MC_CMD_FW_FULL_FEATURED 0x0
/* enum: Prefer to use firmware with fewer features but lower latency */
#define          MC_CMD_FW_LOW_LATENCY 0x1
/* enum: Prefer to use firmware for SolarCapture packed stream mode */
#define          MC_CMD_FW_PACKED_STREAM 0x2
/* enum: Prefer to use firmware with fewer features and simpler TX event
 * batching but higher TX packet rate
 */
#define          MC_CMD_FW_HIGH_TX_RATE 0x3
/* enum: Reserved value */
#define          MC_CMD_FW_PACKED_STREAM_HASH_MODE_1 0x4
/* enum: Prefer to use firmware with additional "rules engine" filtering
 * support
 */
#define          MC_CMD_FW_RULES_ENGINE 0x5
/* enum: Only this option is allowed for non-admin functions */
#define          MC_CMD_FW_DONT_CARE  0xffffffff

/* MC_CMD_DRV_ATTACH_OUT msgresponse */
#define    MC_CMD_DRV_ATTACH_OUT_LEN 4
/* previous or existing state, see the bitmask at NEW_STATE */
#define       MC_CMD_DRV_ATTACH_OUT_OLD_STATE_OFST 0

/* MC_CMD_DRV_ATTACH_EXT_OUT msgresponse */
#define    MC_CMD_DRV_ATTACH_EXT_OUT_LEN 8
/* previous or existing state, see the bitmask at NEW_STATE */
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
/* enum: The function does not have an active port associated with it. The port
 * refers to the Sorrento external FPGA port.
 */
#define          MC_CMD_DRV_ATTACH_EXT_OUT_FLAG_NO_ACTIVE_PORT 0x3


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

#define MC_CMD_0x20_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/*      MC_CMD_0x20_PRIVILEGE_CTG SRIOV_CTG_GENERAL */

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

#define MC_CMD_0x23_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x24_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* enum: QSFP+. */
#define          MC_CMD_MEDIA_QSFP_PLUS 0x7
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

#define MC_CMD_0x25_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x26_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
/* enum: RX0 DPCPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DPCPU_RX 0x2
/* enum: TX0 DPCPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DPCPU_TX0 0x3
/* enum: TX1 DPCPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DPCPU_TX1 0x4
/* enum: RX0 DICPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DICPU_RX 0x5
/* enum: TX DICPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DICPU_TX 0x6
/* enum: RX1 DPCPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DPCPU_RX1 0x7
/* enum: RX1 DICPU bus. */
#define          MC_CMD_POLL_BIST_MEM_BUS_DICPU_RX1 0x8
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

#define MC_CMD_0x28_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* enum: Near side of AOE Siena side port */
#define          MC_CMD_LOOPBACK_AOE_INT_NEAR  0x23
/* enum: Medford Wireside datapath loopback */
#define          MC_CMD_LOOPBACK_DATA_WS  0x24
/* enum: Force link up without setting up any physical loopback (snapper use
 * only)
 */
#define          MC_CMD_LOOPBACK_FORCE_EXT_LINK  0x25
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

#define MC_CMD_0x29_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
#define        MC_CMD_GET_LINK_OUT_LINK_FAULT_RX_LBN 6
#define        MC_CMD_GET_LINK_OUT_LINK_FAULT_RX_WIDTH 1
#define        MC_CMD_GET_LINK_OUT_LINK_FAULT_TX_LBN 7
#define        MC_CMD_GET_LINK_OUT_LINK_FAULT_TX_WIDTH 1
/* This returns the negotiated flow control value. */
#define       MC_CMD_GET_LINK_OUT_FCNTL_OFST 20
/*            Enum values, see field(s): */
/*               MC_CMD_SET_MAC/MC_CMD_SET_MAC_IN/FCNTL */
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

#define MC_CMD_0x2a_PRIVILEGE_CTG SRIOV_CTG_LINK

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

#define MC_CMD_0x2b_PRIVILEGE_CTG SRIOV_CTG_LINK

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

#define MC_CMD_0x2c_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_SET_MAC_IN msgrequest */
#define    MC_CMD_SET_MAC_IN_LEN 28
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
#define          MC_CMD_FCNTL_OFF 0x0
/* enum: Respond to flow control. */
#define          MC_CMD_FCNTL_RESPOND 0x1
/* enum: Respond to and Issue flow control. */
#define          MC_CMD_FCNTL_BIDIR 0x2
/* enum: Auto neg flow control. */
#define          MC_CMD_FCNTL_AUTO 0x3
/* enum: Priority flow control (eftest builds only). */
#define          MC_CMD_FCNTL_QBB 0x4
/* enum: Issue flow control. */
#define          MC_CMD_FCNTL_GENERATE 0x5
#define       MC_CMD_SET_MAC_IN_FLAGS_OFST 24
#define        MC_CMD_SET_MAC_IN_FLAG_INCLUDE_FCS_LBN 0
#define        MC_CMD_SET_MAC_IN_FLAG_INCLUDE_FCS_WIDTH 1

/* MC_CMD_SET_MAC_EXT_IN msgrequest */
#define    MC_CMD_SET_MAC_EXT_IN_LEN 32
/* The MTU is the MTU programmed directly into the XMAC/GMAC (inclusive of
 * EtherII, VLAN, bug16011 padding).
 */
#define       MC_CMD_SET_MAC_EXT_IN_MTU_OFST 0
#define       MC_CMD_SET_MAC_EXT_IN_DRAIN_OFST 4
#define       MC_CMD_SET_MAC_EXT_IN_ADDR_OFST 8
#define       MC_CMD_SET_MAC_EXT_IN_ADDR_LEN 8
#define       MC_CMD_SET_MAC_EXT_IN_ADDR_LO_OFST 8
#define       MC_CMD_SET_MAC_EXT_IN_ADDR_HI_OFST 12
#define       MC_CMD_SET_MAC_EXT_IN_REJECT_OFST 16
#define        MC_CMD_SET_MAC_EXT_IN_REJECT_UNCST_LBN 0
#define        MC_CMD_SET_MAC_EXT_IN_REJECT_UNCST_WIDTH 1
#define        MC_CMD_SET_MAC_EXT_IN_REJECT_BRDCST_LBN 1
#define        MC_CMD_SET_MAC_EXT_IN_REJECT_BRDCST_WIDTH 1
#define       MC_CMD_SET_MAC_EXT_IN_FCNTL_OFST 20
/* enum: Flow control is off. */
/*               MC_CMD_FCNTL_OFF 0x0 */
/* enum: Respond to flow control. */
/*               MC_CMD_FCNTL_RESPOND 0x1 */
/* enum: Respond to and Issue flow control. */
/*               MC_CMD_FCNTL_BIDIR 0x2 */
/* enum: Auto neg flow control. */
/*               MC_CMD_FCNTL_AUTO 0x3 */
/* enum: Priority flow control (eftest builds only). */
/*               MC_CMD_FCNTL_QBB 0x4 */
/* enum: Issue flow control. */
/*               MC_CMD_FCNTL_GENERATE 0x5 */
#define       MC_CMD_SET_MAC_EXT_IN_FLAGS_OFST 24
#define        MC_CMD_SET_MAC_EXT_IN_FLAG_INCLUDE_FCS_LBN 0
#define        MC_CMD_SET_MAC_EXT_IN_FLAG_INCLUDE_FCS_WIDTH 1
/* Select which parameters to configure. A parameter will only be modified if
 * the corresponding control flag is set. If SET_MAC_ENHANCED is not set in
 * capabilities then this field is ignored (and all flags are assumed to be
 * set).
 */
#define       MC_CMD_SET_MAC_EXT_IN_CONTROL_OFST 28
#define        MC_CMD_SET_MAC_EXT_IN_CFG_MTU_LBN 0
#define        MC_CMD_SET_MAC_EXT_IN_CFG_MTU_WIDTH 1
#define        MC_CMD_SET_MAC_EXT_IN_CFG_DRAIN_LBN 1
#define        MC_CMD_SET_MAC_EXT_IN_CFG_DRAIN_WIDTH 1
#define        MC_CMD_SET_MAC_EXT_IN_CFG_REJECT_LBN 2
#define        MC_CMD_SET_MAC_EXT_IN_CFG_REJECT_WIDTH 1
#define        MC_CMD_SET_MAC_EXT_IN_CFG_FCNTL_LBN 3
#define        MC_CMD_SET_MAC_EXT_IN_CFG_FCNTL_WIDTH 1
#define        MC_CMD_SET_MAC_EXT_IN_CFG_FCS_LBN 4
#define        MC_CMD_SET_MAC_EXT_IN_CFG_FCS_WIDTH 1

/* MC_CMD_SET_MAC_OUT msgresponse */
#define    MC_CMD_SET_MAC_OUT_LEN 0

/* MC_CMD_SET_MAC_V2_OUT msgresponse */
#define    MC_CMD_SET_MAC_V2_OUT_LEN 4
/* MTU as configured after processing the request. See comment at
 * MC_CMD_SET_MAC_IN/MTU. To query MTU without doing any changes, set CONTROL
 * to 0.
 */
#define       MC_CMD_SET_MAC_V2_OUT_MTU_OFST 0


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

#define MC_CMD_0x2d_PRIVILEGE_CTG SRIOV_CTG_LINK

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
 * Locks required: None. The PERIODIC_CLEAR option is not used and now has no
 * effect. Returns: 0, ETIME
 */
#define MC_CMD_MAC_STATS 0x2e

#define MC_CMD_0x2e_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_MAC_STATS_IN msgrequest */
#define    MC_CMD_MAC_STATS_IN_LEN 20
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
/* port id so vadapter stats can be provided */
#define       MC_CMD_MAC_STATS_IN_PORT_ID_OFST 16

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
#define          MC_CMD_MAC_DMABUF_START  0x1 /* enum */
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
/* enum: PM trunc_bb_overflow counter. Valid for EF10 with PM_AND_RXDP_COUNTERS
 * capability only.
 */
#define          MC_CMD_MAC_PM_TRUNC_BB_OVERFLOW  0x3c
/* enum: PM discard_bb_overflow counter. Valid for EF10 with
 * PM_AND_RXDP_COUNTERS capability only.
 */
#define          MC_CMD_MAC_PM_DISCARD_BB_OVERFLOW  0x3d
/* enum: PM trunc_vfifo_full counter. Valid for EF10 with PM_AND_RXDP_COUNTERS
 * capability only.
 */
#define          MC_CMD_MAC_PM_TRUNC_VFIFO_FULL  0x3e
/* enum: PM discard_vfifo_full counter. Valid for EF10 with
 * PM_AND_RXDP_COUNTERS capability only.
 */
#define          MC_CMD_MAC_PM_DISCARD_VFIFO_FULL  0x3f
/* enum: PM trunc_qbb counter. Valid for EF10 with PM_AND_RXDP_COUNTERS
 * capability only.
 */
#define          MC_CMD_MAC_PM_TRUNC_QBB  0x40
/* enum: PM discard_qbb counter. Valid for EF10 with PM_AND_RXDP_COUNTERS
 * capability only.
 */
#define          MC_CMD_MAC_PM_DISCARD_QBB  0x41
/* enum: PM discard_mapping counter. Valid for EF10 with PM_AND_RXDP_COUNTERS
 * capability only.
 */
#define          MC_CMD_MAC_PM_DISCARD_MAPPING  0x42
/* enum: RXDP counter: Number of packets dropped due to the queue being
 * disabled. Valid for EF10 with PM_AND_RXDP_COUNTERS capability only.
 */
#define          MC_CMD_MAC_RXDP_Q_DISABLED_PKTS  0x43
/* enum: RXDP counter: Number of packets dropped by the DICPU. Valid for EF10
 * with PM_AND_RXDP_COUNTERS capability only.
 */
#define          MC_CMD_MAC_RXDP_DI_DROPPED_PKTS  0x45
/* enum: RXDP counter: Number of non-host packets. Valid for EF10 with
 * PM_AND_RXDP_COUNTERS capability only.
 */
#define          MC_CMD_MAC_RXDP_STREAMING_PKTS  0x46
/* enum: RXDP counter: Number of times an hlb descriptor fetch was performed.
 * Valid for EF10 with PM_AND_RXDP_COUNTERS capability only.
 */
#define          MC_CMD_MAC_RXDP_HLB_FETCH_CONDITIONS  0x47
/* enum: RXDP counter: Number of times the DPCPU waited for an existing
 * descriptor fetch. Valid for EF10 with PM_AND_RXDP_COUNTERS capability only.
 */
#define          MC_CMD_MAC_RXDP_HLB_WAIT_CONDITIONS  0x48
#define          MC_CMD_MAC_VADAPTER_RX_DMABUF_START  0x4c /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_UNICAST_PACKETS  0x4c /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_UNICAST_BYTES  0x4d /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_MULTICAST_PACKETS  0x4e /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_MULTICAST_BYTES  0x4f /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_BROADCAST_PACKETS  0x50 /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_BROADCAST_BYTES  0x51 /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_BAD_PACKETS  0x52 /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_BAD_BYTES  0x53 /* enum */
#define          MC_CMD_MAC_VADAPTER_RX_OVERFLOW  0x54 /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_DMABUF_START  0x57 /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_UNICAST_PACKETS  0x57 /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_UNICAST_BYTES  0x58 /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_MULTICAST_PACKETS  0x59 /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_MULTICAST_BYTES  0x5a /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_BROADCAST_PACKETS  0x5b /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_BROADCAST_BYTES  0x5c /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_BAD_PACKETS  0x5d /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_BAD_BYTES  0x5e /* enum */
#define          MC_CMD_MAC_VADAPTER_TX_OVERFLOW  0x5f /* enum */
/* enum: Start of GMAC stats buffer space, for Siena only. */
#define          MC_CMD_GMAC_DMABUF_START  0x40
/* enum: End of GMAC stats buffer space, for Siena only. */
#define          MC_CMD_GMAC_DMABUF_END    0x5f
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

#define MC_CMD_0x32_PRIVILEGE_CTG SRIOV_CTG_LINK

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

#define MC_CMD_0x33_PRIVILEGE_CTG SRIOV_CTG_LINK

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

#define MC_CMD_0x34_PRIVILEGE_CTG SRIOV_CTG_LINK

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

#define MC_CMD_0x36_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
/* enum: Additional flash on FPGA. */
#define          MC_CMD_NVRAM_TYPE_FC_EXTRA 0x14


/***********************************/
/* MC_CMD_NVRAM_INFO
 * Read info about a virtual NVRAM partition. Locks required: none. Returns: 0,
 * EINVAL (bad type).
 */
#define MC_CMD_NVRAM_INFO 0x37

#define MC_CMD_0x37_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
#define        MC_CMD_NVRAM_INFO_OUT_CMAC_LBN 6
#define        MC_CMD_NVRAM_INFO_OUT_CMAC_WIDTH 1
#define        MC_CMD_NVRAM_INFO_OUT_A_B_LBN 7
#define        MC_CMD_NVRAM_INFO_OUT_A_B_WIDTH 1
#define       MC_CMD_NVRAM_INFO_OUT_PHYSDEV_OFST 16
#define       MC_CMD_NVRAM_INFO_OUT_PHYSADDR_OFST 20

/* MC_CMD_NVRAM_INFO_V2_OUT msgresponse */
#define    MC_CMD_NVRAM_INFO_V2_OUT_LEN 28
#define       MC_CMD_NVRAM_INFO_V2_OUT_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_INFO_V2_OUT_SIZE_OFST 4
#define       MC_CMD_NVRAM_INFO_V2_OUT_ERASESIZE_OFST 8
#define       MC_CMD_NVRAM_INFO_V2_OUT_FLAGS_OFST 12
#define        MC_CMD_NVRAM_INFO_V2_OUT_PROTECTED_LBN 0
#define        MC_CMD_NVRAM_INFO_V2_OUT_PROTECTED_WIDTH 1
#define        MC_CMD_NVRAM_INFO_V2_OUT_TLV_LBN 1
#define        MC_CMD_NVRAM_INFO_V2_OUT_TLV_WIDTH 1
#define        MC_CMD_NVRAM_INFO_V2_OUT_A_B_LBN 7
#define        MC_CMD_NVRAM_INFO_V2_OUT_A_B_WIDTH 1
#define       MC_CMD_NVRAM_INFO_V2_OUT_PHYSDEV_OFST 16
#define       MC_CMD_NVRAM_INFO_V2_OUT_PHYSADDR_OFST 20
/* Writes must be multiples of this size. Added to support the MUM on Sorrento.
 */
#define       MC_CMD_NVRAM_INFO_V2_OUT_WRITESIZE_OFST 24


/***********************************/
/* MC_CMD_NVRAM_UPDATE_START
 * Start a group of update operations on a virtual NVRAM partition. Locks
 * required: PHY_LOCK if type==*PHY*. Returns: 0, EINVAL (bad type), EACCES (if
 * PHY_LOCK required and not held).
 */
#define MC_CMD_NVRAM_UPDATE_START 0x38

#define MC_CMD_0x38_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_NVRAM_UPDATE_START_IN msgrequest: Legacy NVRAM_UPDATE_START request.
 * Use NVRAM_UPDATE_START_V2_IN in new code
 */
#define    MC_CMD_NVRAM_UPDATE_START_IN_LEN 4
#define       MC_CMD_NVRAM_UPDATE_START_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */

/* MC_CMD_NVRAM_UPDATE_START_V2_IN msgrequest: Extended NVRAM_UPDATE_START
 * request with additional flags indicating version of command in use. See
 * MC_CMD_NVRAM_UPDATE_FINISH_V2_OUT for details of extended functionality. Use
 * paired up with NVRAM_UPDATE_FINISH_V2_IN.
 */
#define    MC_CMD_NVRAM_UPDATE_START_V2_IN_LEN 8
#define       MC_CMD_NVRAM_UPDATE_START_V2_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_UPDATE_START_V2_IN_FLAGS_OFST 4
#define        MC_CMD_NVRAM_UPDATE_START_V2_IN_FLAG_REPORT_VERIFY_RESULT_LBN 0
#define        MC_CMD_NVRAM_UPDATE_START_V2_IN_FLAG_REPORT_VERIFY_RESULT_WIDTH 1

/* MC_CMD_NVRAM_UPDATE_START_OUT msgresponse */
#define    MC_CMD_NVRAM_UPDATE_START_OUT_LEN 0


/***********************************/
/* MC_CMD_NVRAM_READ
 * Read data from a virtual NVRAM partition. Locks required: PHY_LOCK if
 * type==*PHY*. Returns: 0, EINVAL (bad type/offset/length), EACCES (if
 * PHY_LOCK required and not held)
 */
#define MC_CMD_NVRAM_READ 0x39

#define MC_CMD_0x39_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_NVRAM_READ_IN msgrequest */
#define    MC_CMD_NVRAM_READ_IN_LEN 12
#define       MC_CMD_NVRAM_READ_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_READ_IN_OFFSET_OFST 4
/* amount to read in bytes */
#define       MC_CMD_NVRAM_READ_IN_LENGTH_OFST 8

/* MC_CMD_NVRAM_READ_IN_V2 msgrequest */
#define    MC_CMD_NVRAM_READ_IN_V2_LEN 16
#define       MC_CMD_NVRAM_READ_IN_V2_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_READ_IN_V2_OFFSET_OFST 4
/* amount to read in bytes */
#define       MC_CMD_NVRAM_READ_IN_V2_LENGTH_OFST 8
/* Optional control info. If a partition is stored with an A/B versioning
 * scheme (i.e. in more than one physical partition in NVRAM) the host can set
 * this to control which underlying physical partition is used to read data
 * from. This allows it to perform a read-modify-write-verify with the write
 * lock continuously held by calling NVRAM_UPDATE_START, reading the old
 * contents using MODE=TARGET_CURRENT, overwriting the old partition and then
 * verifying by reading with MODE=TARGET_BACKUP.
 */
#define       MC_CMD_NVRAM_READ_IN_V2_MODE_OFST 12
/* enum: Same as omitting MODE: caller sees data in current partition unless it
 * holds the write lock in which case it sees data in the partition it is
 * updating.
 */
#define          MC_CMD_NVRAM_READ_IN_V2_DEFAULT 0x0
/* enum: Read from the current partition of an A/B pair, even if holding the
 * write lock.
 */
#define          MC_CMD_NVRAM_READ_IN_V2_TARGET_CURRENT 0x1
/* enum: Read from the non-current (i.e. to be updated) partition of an A/B
 * pair
 */
#define          MC_CMD_NVRAM_READ_IN_V2_TARGET_BACKUP 0x2

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

#define MC_CMD_0x3a_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x3b_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x3c_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_NVRAM_UPDATE_FINISH_IN msgrequest: Legacy NVRAM_UPDATE_FINISH
 * request. Use NVRAM_UPDATE_FINISH_V2_IN in new code
 */
#define    MC_CMD_NVRAM_UPDATE_FINISH_IN_LEN 8
#define       MC_CMD_NVRAM_UPDATE_FINISH_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_UPDATE_FINISH_IN_REBOOT_OFST 4

/* MC_CMD_NVRAM_UPDATE_FINISH_V2_IN msgrequest: Extended NVRAM_UPDATE_FINISH
 * request with additional flags indicating version of NVRAM_UPDATE commands in
 * use. See MC_CMD_NVRAM_UPDATE_FINISH_V2_OUT for details of extended
 * functionality. Use paired up with NVRAM_UPDATE_START_V2_IN.
 */
#define    MC_CMD_NVRAM_UPDATE_FINISH_V2_IN_LEN 12
#define       MC_CMD_NVRAM_UPDATE_FINISH_V2_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_NVRAM_TYPES/MC_CMD_NVRAM_TYPES_OUT/TYPES */
#define       MC_CMD_NVRAM_UPDATE_FINISH_V2_IN_REBOOT_OFST 4
#define       MC_CMD_NVRAM_UPDATE_FINISH_V2_IN_FLAGS_OFST 8
#define        MC_CMD_NVRAM_UPDATE_FINISH_V2_IN_FLAG_REPORT_VERIFY_RESULT_LBN 0
#define        MC_CMD_NVRAM_UPDATE_FINISH_V2_IN_FLAG_REPORT_VERIFY_RESULT_WIDTH 1

/* MC_CMD_NVRAM_UPDATE_FINISH_OUT msgresponse: Legacy NVRAM_UPDATE_FINISH
 * response. Use NVRAM_UPDATE_FINISH_V2_OUT in new code
 */
#define    MC_CMD_NVRAM_UPDATE_FINISH_OUT_LEN 0

/* MC_CMD_NVRAM_UPDATE_FINISH_V2_OUT msgresponse:
 *
 * Extended NVRAM_UPDATE_FINISH response that communicates the result of secure
 * firmware validation where applicable back to the host.
 *
 * Medford only: For signed firmware images, such as those for medford, the MC
 * firmware verifies the signature before marking the firmware image as valid.
 * This process takes a few seconds to complete. So is likely to take more than
 * the MCDI timeout. Hence signature verification is initiated when
 * MC_CMD_NVRAM_UPDATE_FINISH_V2_IN is received by the firmware, however, the
 * MCDI command returns immediately with error code EAGAIN. Subsequent
 * NVRAM_UPDATE_FINISH_V2_IN requests also return EAGAIN if the verification is
 * in progress. Once the verification has completed, this response payload
 * includes the results of the signature verification. Note that the nvram lock
 * in firmware is only released after the verification has completed and the
 * host has read back the result code from firmware.
 */
#define    MC_CMD_NVRAM_UPDATE_FINISH_V2_OUT_LEN 4
/* Result of nvram update completion processing */
#define       MC_CMD_NVRAM_UPDATE_FINISH_V2_OUT_RESULT_CODE_OFST 0
/* enum: Verify succeeded without any errors. */
#define          MC_CMD_NVRAM_VERIFY_RC_SUCCESS 0x1
/* enum: CMS format verification failed due to an internal error. */
#define          MC_CMD_NVRAM_VERIFY_RC_CMS_CHECK_FAILED 0x2
/* enum: Invalid CMS format in image metadata. */
#define          MC_CMD_NVRAM_VERIFY_RC_INVALID_CMS_FORMAT 0x3
/* enum: Message digest verification failed due to an internal error. */
#define          MC_CMD_NVRAM_VERIFY_RC_MESSAGE_DIGEST_CHECK_FAILED 0x4
/* enum: Error in message digest calculated over the reflash-header, payload
 * and reflash-trailer.
 */
#define          MC_CMD_NVRAM_VERIFY_RC_BAD_MESSAGE_DIGEST 0x5
/* enum: Signature verification failed due to an internal error. */
#define          MC_CMD_NVRAM_VERIFY_RC_SIGNATURE_CHECK_FAILED 0x6
/* enum: There are no valid signatures in the image. */
#define          MC_CMD_NVRAM_VERIFY_RC_NO_VALID_SIGNATURES 0x7
/* enum: Trusted approvers verification failed due to an internal error. */
#define          MC_CMD_NVRAM_VERIFY_RC_TRUSTED_APPROVERS_CHECK_FAILED 0x8
/* enum: The Trusted approver's list is empty. */
#define          MC_CMD_NVRAM_VERIFY_RC_NO_TRUSTED_APPROVERS 0x9
/* enum: Signature chain verification failed due to an internal error. */
#define          MC_CMD_NVRAM_VERIFY_RC_SIGNATURE_CHAIN_CHECK_FAILED 0xa
/* enum: The signers of the signatures in the image are not listed in the
 * Trusted approver's list.
 */
#define          MC_CMD_NVRAM_VERIFY_RC_NO_SIGNATURE_MATCH 0xb


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

#define MC_CMD_0x3d_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x3e_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x3f_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x41_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
#define    MC_CMD_SENSOR_INFO_OUT_LENMIN 4
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
/* enum: Hotpoint temperature: degC */
#define          MC_CMD_SENSOR_HOTPOINT_TEMP  0x2d
/* enum: Port 0 PHY power switch over-current: bool */
#define          MC_CMD_SENSOR_PHY_POWER_PORT0  0x2e
/* enum: Port 1 PHY power switch over-current: bool */
#define          MC_CMD_SENSOR_PHY_POWER_PORT1  0x2f
/* enum: Mop-up microcontroller reference voltage (millivolts) */
#define          MC_CMD_SENSOR_MUM_VCC  0x30
/* enum: 0.9v power phase A voltage: mV */
#define          MC_CMD_SENSOR_IN_0V9_A  0x31
/* enum: 0.9v power phase A current: mA */
#define          MC_CMD_SENSOR_IN_I0V9_A  0x32
/* enum: 0.9V voltage regulator phase A temperature: degC */
#define          MC_CMD_SENSOR_VREG_0V9_A_TEMP  0x33
/* enum: 0.9v power phase B voltage: mV */
#define          MC_CMD_SENSOR_IN_0V9_B  0x34
/* enum: 0.9v power phase B current: mA */
#define          MC_CMD_SENSOR_IN_I0V9_B  0x35
/* enum: 0.9V voltage regulator phase B temperature: degC */
#define          MC_CMD_SENSOR_VREG_0V9_B_TEMP  0x36
/* enum: CCOM AVREG 1v2 supply (interval ADC): mV */
#define          MC_CMD_SENSOR_CCOM_AVREG_1V2_SUPPLY  0x37
/* enum: CCOM AVREG 1v2 supply (external ADC): mV */
#define          MC_CMD_SENSOR_CCOM_AVREG_1V2_SUPPLY_EXTADC  0x38
/* enum: CCOM AVREG 1v8 supply (interval ADC): mV */
#define          MC_CMD_SENSOR_CCOM_AVREG_1V8_SUPPLY  0x39
/* enum: CCOM AVREG 1v8 supply (external ADC): mV */
#define          MC_CMD_SENSOR_CCOM_AVREG_1V8_SUPPLY_EXTADC  0x3a
/* enum: CCOM RTS temperature: degC */
#define          MC_CMD_SENSOR_CONTROLLER_RTS  0x3b
/* enum: Not a sensor: reserved for the next page flag */
#define          MC_CMD_SENSOR_PAGE1_NEXT  0x3f
/* enum: controller internal temperature sensor voltage on master core
 * (internal ADC): mV
 */
#define          MC_CMD_SENSOR_CONTROLLER_MASTER_VPTAT  0x40
/* enum: controller internal temperature on master core (internal ADC): degC */
#define          MC_CMD_SENSOR_CONTROLLER_MASTER_INTERNAL_TEMP  0x41
/* enum: controller internal temperature sensor voltage on master core
 * (external ADC): mV
 */
#define          MC_CMD_SENSOR_CONTROLLER_MASTER_VPTAT_EXTADC  0x42
/* enum: controller internal temperature on master core (external ADC): degC */
#define          MC_CMD_SENSOR_CONTROLLER_MASTER_INTERNAL_TEMP_EXTADC  0x43
/* enum: controller internal temperature on slave core sensor voltage (internal
 * ADC): mV
 */
#define          MC_CMD_SENSOR_CONTROLLER_SLAVE_VPTAT  0x44
/* enum: controller internal temperature on slave core (internal ADC): degC */
#define          MC_CMD_SENSOR_CONTROLLER_SLAVE_INTERNAL_TEMP  0x45
/* enum: controller internal temperature on slave core sensor voltage (external
 * ADC): mV
 */
#define          MC_CMD_SENSOR_CONTROLLER_SLAVE_VPTAT_EXTADC  0x46
/* enum: controller internal temperature on slave core (external ADC): degC */
#define          MC_CMD_SENSOR_CONTROLLER_SLAVE_INTERNAL_TEMP_EXTADC  0x47
/* enum: Voltage supplied to the SODIMMs from their power supply: mV */
#define          MC_CMD_SENSOR_SODIMM_VOUT  0x49
/* enum: Temperature of SODIMM 0 (if installed): degC */
#define          MC_CMD_SENSOR_SODIMM_0_TEMP  0x4a
/* enum: Temperature of SODIMM 1 (if installed): degC */
#define          MC_CMD_SENSOR_SODIMM_1_TEMP  0x4b
/* enum: Voltage supplied to the QSFP #0 from their power supply: mV */
#define          MC_CMD_SENSOR_PHY0_VCC  0x4c
/* enum: Voltage supplied to the QSFP #1 from their power supply: mV */
#define          MC_CMD_SENSOR_PHY1_VCC  0x4d
/* enum: Controller die temperature (TDIODE): degC */
#define          MC_CMD_SENSOR_CONTROLLER_TDIODE_TEMP  0x4e
/* enum: Board temperature (front): degC */
#define          MC_CMD_SENSOR_BOARD_FRONT_TEMP  0x4f
/* enum: Board temperature (back): degC */
#define          MC_CMD_SENSOR_BOARD_BACK_TEMP  0x50
/* MC_CMD_SENSOR_INFO_ENTRY_TYPEDEF */
#define       MC_CMD_SENSOR_ENTRY_OFST 4
#define       MC_CMD_SENSOR_ENTRY_LEN 8
#define       MC_CMD_SENSOR_ENTRY_LO_OFST 4
#define       MC_CMD_SENSOR_ENTRY_HI_OFST 8
#define       MC_CMD_SENSOR_ENTRY_MINNUM 0
#define       MC_CMD_SENSOR_ENTRY_MAXNUM 31

/* MC_CMD_SENSOR_INFO_EXT_OUT msgresponse */
#define    MC_CMD_SENSOR_INFO_EXT_OUT_LENMIN 4
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
/*            MC_CMD_SENSOR_ENTRY_MINNUM 0 */
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

#define MC_CMD_0x42_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_READ_SENSORS_IN msgrequest */
#define    MC_CMD_READ_SENSORS_IN_LEN 8
/* DMA address of host buffer for sensor readings (must be 4Kbyte aligned). */
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_OFST 0
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_LEN 8
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_LO_OFST 0
#define       MC_CMD_READ_SENSORS_IN_DMA_ADDR_HI_OFST 4

/* MC_CMD_READ_SENSORS_EXT_IN msgrequest */
#define    MC_CMD_READ_SENSORS_EXT_IN_LEN 12
/* DMA address of host buffer for sensor readings (must be 4Kbyte aligned). */
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
/* enum: Sensor initialisation failed. */
#define          MC_CMD_SENSOR_STATE_INIT_FAILED  0x5
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

#define MC_CMD_0x43_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x45_PRIVILEGE_CTG SRIOV_CTG_LINK

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

#define MC_CMD_0x46_PRIVILEGE_CTG SRIOV_CTG_LINK

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

#define MC_CMD_0x47_PRIVILEGE_CTG SRIOV_CTG_LINK

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

#define MC_CMD_0x49_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_TESTASSERT_IN msgrequest */
#define    MC_CMD_TESTASSERT_IN_LEN 0

/* MC_CMD_TESTASSERT_OUT msgresponse */
#define    MC_CMD_TESTASSERT_OUT_LEN 0

/* MC_CMD_TESTASSERT_V2_IN msgrequest */
#define    MC_CMD_TESTASSERT_V2_IN_LEN 4
/* How to provoke the assertion */
#define       MC_CMD_TESTASSERT_V2_IN_TYPE_OFST 0
/* enum: Assert using the FAIL_ASSERTION_WITH_USEFUL_VALUES macro. Unless
 * you're testing firmware, this is what you want.
 */
#define          MC_CMD_TESTASSERT_V2_IN_FAIL_ASSERTION_WITH_USEFUL_VALUES  0x0
/* enum: Assert using assert(0); */
#define          MC_CMD_TESTASSERT_V2_IN_ASSERT_FALSE  0x1
/* enum: Deliberately trigger a watchdog */
#define          MC_CMD_TESTASSERT_V2_IN_WATCHDOG  0x2
/* enum: Deliberately trigger a trap by loading from an invalid address */
#define          MC_CMD_TESTASSERT_V2_IN_LOAD_TRAP  0x3
/* enum: Deliberately trigger a trap by storing to an invalid address */
#define          MC_CMD_TESTASSERT_V2_IN_STORE_TRAP  0x4
/* enum: Jump to an invalid address */
#define          MC_CMD_TESTASSERT_V2_IN_JUMP_TRAP  0x5

/* MC_CMD_TESTASSERT_V2_OUT msgresponse */
#define    MC_CMD_TESTASSERT_V2_OUT_LEN 0


/***********************************/
/* MC_CMD_WORKAROUND
 * Enable/Disable a given workaround. The mcfw will return EINVAL if it doesn't
 * understand the given workaround number - which should not be treated as a
 * hard error by client code. This op does not imply any semantics about each
 * workaround, that's between the driver and the mcfw on a per-workaround
 * basis. Locks required: None. Returns: 0, EINVAL .
 */
#define MC_CMD_WORKAROUND 0x4a

#define MC_CMD_0x4a_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_WORKAROUND_IN msgrequest */
#define    MC_CMD_WORKAROUND_IN_LEN 8
/* The enums here must correspond with those in MC_CMD_GET_WORKAROUND. */
#define       MC_CMD_WORKAROUND_IN_TYPE_OFST 0
/* enum: Bug 17230 work around. */
#define          MC_CMD_WORKAROUND_BUG17230 0x1
/* enum: Bug 35388 work around (unsafe EVQ writes). */
#define          MC_CMD_WORKAROUND_BUG35388 0x2
/* enum: Bug35017 workaround (A64 tables must be identity map) */
#define          MC_CMD_WORKAROUND_BUG35017 0x3
/* enum: Bug 41750 present (MC_CMD_TRIGGER_INTERRUPT won't work) */
#define          MC_CMD_WORKAROUND_BUG41750 0x4
/* enum: Bug 42008 present (Interrupts can overtake associated events). Caution
 * - before adding code that queries this workaround, remember that there's
 * released Monza firmware that doesn't understand MC_CMD_WORKAROUND_BUG42008,
 * and will hence (incorrectly) report that the bug doesn't exist.
 */
#define          MC_CMD_WORKAROUND_BUG42008 0x5
/* enum: Bug 26807 features present in firmware (multicast filter chaining)
 * This feature cannot be turned on/off while there are any filters already
 * present. The behaviour in such case depends on the acting client's privilege
 * level. If the client has the admin privilege, then all functions that have
 * filters installed will be FLRed and the FLR_DONE flag will be set. Otherwise
 * the command will fail with MC_CMD_ERR_FILTERS_PRESENT.
 */
#define          MC_CMD_WORKAROUND_BUG26807 0x6
/* enum: Bug 61265 work around (broken EVQ TMR writes). */
#define          MC_CMD_WORKAROUND_BUG61265 0x7
/* 0 = disable the workaround indicated by TYPE; any non-zero value = enable
 * the workaround
 */
#define       MC_CMD_WORKAROUND_IN_ENABLED_OFST 4

/* MC_CMD_WORKAROUND_OUT msgresponse */
#define    MC_CMD_WORKAROUND_OUT_LEN 0

/* MC_CMD_WORKAROUND_EXT_OUT msgresponse: This response format will be used
 * when (TYPE == MC_CMD_WORKAROUND_BUG26807)
 */
#define    MC_CMD_WORKAROUND_EXT_OUT_LEN 4
#define       MC_CMD_WORKAROUND_EXT_OUT_FLAGS_OFST 0
#define        MC_CMD_WORKAROUND_EXT_OUT_FLR_DONE_LBN 0
#define        MC_CMD_WORKAROUND_EXT_OUT_FLR_DONE_WIDTH 1


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

#define MC_CMD_0x4b_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x4c_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x4e_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x51_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x52_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
 * Returns the base MAC, count and stride for the requesting function
 */
#define MC_CMD_GET_MAC_ADDRESSES 0x55

#define MC_CMD_0x55_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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


/***********************************/
/* MC_CMD_CLP
 * Perform a CLP related operation
 */
#define MC_CMD_CLP 0x56

#define MC_CMD_0x56_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CLP_IN msgrequest */
#define    MC_CMD_CLP_IN_LEN 4
/* Sub operation */
#define       MC_CMD_CLP_IN_OP_OFST 0
/* enum: Return to factory default settings */
#define          MC_CMD_CLP_OP_DEFAULT 0x1
/* enum: Set MAC address */
#define          MC_CMD_CLP_OP_SET_MAC 0x2
/* enum: Get MAC address */
#define          MC_CMD_CLP_OP_GET_MAC 0x3
/* enum: Set UEFI/GPXE boot mode */
#define          MC_CMD_CLP_OP_SET_BOOT 0x4
/* enum: Get UEFI/GPXE boot mode */
#define          MC_CMD_CLP_OP_GET_BOOT 0x5

/* MC_CMD_CLP_OUT msgresponse */
#define    MC_CMD_CLP_OUT_LEN 0

/* MC_CMD_CLP_IN_DEFAULT msgrequest */
#define    MC_CMD_CLP_IN_DEFAULT_LEN 4
/*            MC_CMD_CLP_IN_OP_OFST 0 */

/* MC_CMD_CLP_OUT_DEFAULT msgresponse */
#define    MC_CMD_CLP_OUT_DEFAULT_LEN 0

/* MC_CMD_CLP_IN_SET_MAC msgrequest */
#define    MC_CMD_CLP_IN_SET_MAC_LEN 12
/*            MC_CMD_CLP_IN_OP_OFST 0 */
/* MAC address assigned to port */
#define       MC_CMD_CLP_IN_SET_MAC_ADDR_OFST 4
#define       MC_CMD_CLP_IN_SET_MAC_ADDR_LEN 6
/* Padding */
#define       MC_CMD_CLP_IN_SET_MAC_RESERVED_OFST 10
#define       MC_CMD_CLP_IN_SET_MAC_RESERVED_LEN 2

/* MC_CMD_CLP_OUT_SET_MAC msgresponse */
#define    MC_CMD_CLP_OUT_SET_MAC_LEN 0

/* MC_CMD_CLP_IN_GET_MAC msgrequest */
#define    MC_CMD_CLP_IN_GET_MAC_LEN 4
/*            MC_CMD_CLP_IN_OP_OFST 0 */

/* MC_CMD_CLP_OUT_GET_MAC msgresponse */
#define    MC_CMD_CLP_OUT_GET_MAC_LEN 8
/* MAC address assigned to port */
#define       MC_CMD_CLP_OUT_GET_MAC_ADDR_OFST 0
#define       MC_CMD_CLP_OUT_GET_MAC_ADDR_LEN 6
/* Padding */
#define       MC_CMD_CLP_OUT_GET_MAC_RESERVED_OFST 6
#define       MC_CMD_CLP_OUT_GET_MAC_RESERVED_LEN 2

/* MC_CMD_CLP_IN_SET_BOOT msgrequest */
#define    MC_CMD_CLP_IN_SET_BOOT_LEN 5
/*            MC_CMD_CLP_IN_OP_OFST 0 */
/* Boot flag */
#define       MC_CMD_CLP_IN_SET_BOOT_FLAG_OFST 4
#define       MC_CMD_CLP_IN_SET_BOOT_FLAG_LEN 1

/* MC_CMD_CLP_OUT_SET_BOOT msgresponse */
#define    MC_CMD_CLP_OUT_SET_BOOT_LEN 0

/* MC_CMD_CLP_IN_GET_BOOT msgrequest */
#define    MC_CMD_CLP_IN_GET_BOOT_LEN 4
/*            MC_CMD_CLP_IN_OP_OFST 0 */

/* MC_CMD_CLP_OUT_GET_BOOT msgresponse */
#define    MC_CMD_CLP_OUT_GET_BOOT_LEN 4
/* Boot flag */
#define       MC_CMD_CLP_OUT_GET_BOOT_FLAG_OFST 0
#define       MC_CMD_CLP_OUT_GET_BOOT_FLAG_LEN 1
/* Padding */
#define       MC_CMD_CLP_OUT_GET_BOOT_RESERVED_OFST 1
#define       MC_CMD_CLP_OUT_GET_BOOT_RESERVED_LEN 3


/***********************************/
/* MC_CMD_MUM
 * Perform a MUM operation
 */
#define MC_CMD_MUM 0x57

#define MC_CMD_0x57_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_MUM_IN msgrequest */
#define    MC_CMD_MUM_IN_LEN 4
#define       MC_CMD_MUM_IN_OP_HDR_OFST 0
#define        MC_CMD_MUM_IN_OP_LBN 0
#define        MC_CMD_MUM_IN_OP_WIDTH 8
/* enum: NULL MCDI command to MUM */
#define          MC_CMD_MUM_OP_NULL 0x1
/* enum: Get MUM version */
#define          MC_CMD_MUM_OP_GET_VERSION 0x2
/* enum: Issue raw I2C command to MUM */
#define          MC_CMD_MUM_OP_RAW_CMD 0x3
/* enum: Read from registers on devices connected to MUM. */
#define          MC_CMD_MUM_OP_READ 0x4
/* enum: Write to registers on devices connected to MUM. */
#define          MC_CMD_MUM_OP_WRITE 0x5
/* enum: Control UART logging. */
#define          MC_CMD_MUM_OP_LOG 0x6
/* enum: Operations on MUM GPIO lines */
#define          MC_CMD_MUM_OP_GPIO 0x7
/* enum: Get sensor readings from MUM */
#define          MC_CMD_MUM_OP_READ_SENSORS 0x8
/* enum: Initiate clock programming on the MUM */
#define          MC_CMD_MUM_OP_PROGRAM_CLOCKS 0x9
/* enum: Initiate FPGA load from flash on the MUM */
#define          MC_CMD_MUM_OP_FPGA_LOAD 0xa
/* enum: Request sensor reading from MUM ADC resulting from earlier request via
 * MUM ATB
 */
#define          MC_CMD_MUM_OP_READ_ATB_SENSOR 0xb
/* enum: Send commands relating to the QSFP ports via the MUM for PHY
 * operations
 */
#define          MC_CMD_MUM_OP_QSFP 0xc
/* enum: Request discrete and SODIMM DDR info (type, size, speed grade, voltage
 * level) from MUM
 */
#define          MC_CMD_MUM_OP_READ_DDR_INFO 0xd

/* MC_CMD_MUM_IN_NULL msgrequest */
#define    MC_CMD_MUM_IN_NULL_LEN 4
/* MUM cmd header */
#define       MC_CMD_MUM_IN_CMD_OFST 0

/* MC_CMD_MUM_IN_GET_VERSION msgrequest */
#define    MC_CMD_MUM_IN_GET_VERSION_LEN 4
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */

/* MC_CMD_MUM_IN_READ msgrequest */
#define    MC_CMD_MUM_IN_READ_LEN 16
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
/* ID of (device connected to MUM) to read from registers of */
#define       MC_CMD_MUM_IN_READ_DEVICE_OFST 4
/* enum: Hittite HMC1035 clock generator on Sorrento board */
#define          MC_CMD_MUM_DEV_HITTITE 0x1
/* enum: Hittite HMC1035 clock generator for NIC-side on Sorrento board */
#define          MC_CMD_MUM_DEV_HITTITE_NIC 0x2
/* 32-bit address to read from */
#define       MC_CMD_MUM_IN_READ_ADDR_OFST 8
/* Number of words to read. */
#define       MC_CMD_MUM_IN_READ_NUMWORDS_OFST 12

/* MC_CMD_MUM_IN_WRITE msgrequest */
#define    MC_CMD_MUM_IN_WRITE_LENMIN 16
#define    MC_CMD_MUM_IN_WRITE_LENMAX 252
#define    MC_CMD_MUM_IN_WRITE_LEN(num) (12+4*(num))
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
/* ID of (device connected to MUM) to write to registers of */
#define       MC_CMD_MUM_IN_WRITE_DEVICE_OFST 4
/* enum: Hittite HMC1035 clock generator on Sorrento board */
/*               MC_CMD_MUM_DEV_HITTITE 0x1 */
/* 32-bit address to write to */
#define       MC_CMD_MUM_IN_WRITE_ADDR_OFST 8
/* Words to write */
#define       MC_CMD_MUM_IN_WRITE_BUFFER_OFST 12
#define       MC_CMD_MUM_IN_WRITE_BUFFER_LEN 4
#define       MC_CMD_MUM_IN_WRITE_BUFFER_MINNUM 1
#define       MC_CMD_MUM_IN_WRITE_BUFFER_MAXNUM 60

/* MC_CMD_MUM_IN_RAW_CMD msgrequest */
#define    MC_CMD_MUM_IN_RAW_CMD_LENMIN 17
#define    MC_CMD_MUM_IN_RAW_CMD_LENMAX 252
#define    MC_CMD_MUM_IN_RAW_CMD_LEN(num) (16+1*(num))
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
/* MUM I2C cmd code */
#define       MC_CMD_MUM_IN_RAW_CMD_CMD_CODE_OFST 4
/* Number of bytes to write */
#define       MC_CMD_MUM_IN_RAW_CMD_NUM_WRITE_OFST 8
/* Number of bytes to read */
#define       MC_CMD_MUM_IN_RAW_CMD_NUM_READ_OFST 12
/* Bytes to write */
#define       MC_CMD_MUM_IN_RAW_CMD_WRITE_DATA_OFST 16
#define       MC_CMD_MUM_IN_RAW_CMD_WRITE_DATA_LEN 1
#define       MC_CMD_MUM_IN_RAW_CMD_WRITE_DATA_MINNUM 1
#define       MC_CMD_MUM_IN_RAW_CMD_WRITE_DATA_MAXNUM 236

/* MC_CMD_MUM_IN_LOG msgrequest */
#define    MC_CMD_MUM_IN_LOG_LEN 8
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_LOG_OP_OFST 4
#define          MC_CMD_MUM_IN_LOG_OP_UART  0x1 /* enum */

/* MC_CMD_MUM_IN_LOG_OP_UART msgrequest */
#define    MC_CMD_MUM_IN_LOG_OP_UART_LEN 12
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
/*            MC_CMD_MUM_IN_LOG_OP_OFST 4 */
/* Enable/disable debug output to UART */
#define       MC_CMD_MUM_IN_LOG_OP_UART_ENABLE_OFST 8

/* MC_CMD_MUM_IN_GPIO msgrequest */
#define    MC_CMD_MUM_IN_GPIO_LEN 8
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_HDR_OFST 4
#define        MC_CMD_MUM_IN_GPIO_OPCODE_LBN 0
#define        MC_CMD_MUM_IN_GPIO_OPCODE_WIDTH 8
#define          MC_CMD_MUM_IN_GPIO_IN_READ 0x0 /* enum */
#define          MC_CMD_MUM_IN_GPIO_OUT_WRITE 0x1 /* enum */
#define          MC_CMD_MUM_IN_GPIO_OUT_READ 0x2 /* enum */
#define          MC_CMD_MUM_IN_GPIO_OUT_ENABLE_WRITE 0x3 /* enum */
#define          MC_CMD_MUM_IN_GPIO_OUT_ENABLE_READ 0x4 /* enum */
#define          MC_CMD_MUM_IN_GPIO_OP 0x5 /* enum */

/* MC_CMD_MUM_IN_GPIO_IN_READ msgrequest */
#define    MC_CMD_MUM_IN_GPIO_IN_READ_LEN 8
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_IN_READ_HDR_OFST 4

/* MC_CMD_MUM_IN_GPIO_OUT_WRITE msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OUT_WRITE_LEN 16
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OUT_WRITE_HDR_OFST 4
/* The first 32-bit word to be written to the GPIO OUT register. */
#define       MC_CMD_MUM_IN_GPIO_OUT_WRITE_GPIOMASK1_OFST 8
/* The second 32-bit word to be written to the GPIO OUT register. */
#define       MC_CMD_MUM_IN_GPIO_OUT_WRITE_GPIOMASK2_OFST 12

/* MC_CMD_MUM_IN_GPIO_OUT_READ msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OUT_READ_LEN 8
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OUT_READ_HDR_OFST 4

/* MC_CMD_MUM_IN_GPIO_OUT_ENABLE_WRITE msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OUT_ENABLE_WRITE_LEN 16
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OUT_ENABLE_WRITE_HDR_OFST 4
/* The first 32-bit word to be written to the GPIO OUT ENABLE register. */
#define       MC_CMD_MUM_IN_GPIO_OUT_ENABLE_WRITE_GPIOMASK1_OFST 8
/* The second 32-bit word to be written to the GPIO OUT ENABLE register. */
#define       MC_CMD_MUM_IN_GPIO_OUT_ENABLE_WRITE_GPIOMASK2_OFST 12

/* MC_CMD_MUM_IN_GPIO_OUT_ENABLE_READ msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OUT_ENABLE_READ_LEN 8
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OUT_ENABLE_READ_HDR_OFST 4

/* MC_CMD_MUM_IN_GPIO_OP msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OP_LEN 8
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OP_HDR_OFST 4
#define        MC_CMD_MUM_IN_GPIO_OP_BITWISE_OP_LBN 8
#define        MC_CMD_MUM_IN_GPIO_OP_BITWISE_OP_WIDTH 8
#define          MC_CMD_MUM_IN_GPIO_OP_OUT_READ 0x0 /* enum */
#define          MC_CMD_MUM_IN_GPIO_OP_OUT_WRITE 0x1 /* enum */
#define          MC_CMD_MUM_IN_GPIO_OP_OUT_CONFIG 0x2 /* enum */
#define          MC_CMD_MUM_IN_GPIO_OP_OUT_ENABLE 0x3 /* enum */
#define        MC_CMD_MUM_IN_GPIO_OP_GPIO_NUMBER_LBN 16
#define        MC_CMD_MUM_IN_GPIO_OP_GPIO_NUMBER_WIDTH 8

/* MC_CMD_MUM_IN_GPIO_OP_OUT_READ msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OP_OUT_READ_LEN 8
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OP_OUT_READ_HDR_OFST 4

/* MC_CMD_MUM_IN_GPIO_OP_OUT_WRITE msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OP_OUT_WRITE_LEN 8
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OP_OUT_WRITE_HDR_OFST 4
#define        MC_CMD_MUM_IN_GPIO_OP_OUT_WRITE_WRITEBIT_LBN 24
#define        MC_CMD_MUM_IN_GPIO_OP_OUT_WRITE_WRITEBIT_WIDTH 8

/* MC_CMD_MUM_IN_GPIO_OP_OUT_CONFIG msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OP_OUT_CONFIG_LEN 8
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OP_OUT_CONFIG_HDR_OFST 4
#define        MC_CMD_MUM_IN_GPIO_OP_OUT_CONFIG_CFG_LBN 24
#define        MC_CMD_MUM_IN_GPIO_OP_OUT_CONFIG_CFG_WIDTH 8

/* MC_CMD_MUM_IN_GPIO_OP_OUT_ENABLE msgrequest */
#define    MC_CMD_MUM_IN_GPIO_OP_OUT_ENABLE_LEN 8
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_GPIO_OP_OUT_ENABLE_HDR_OFST 4
#define        MC_CMD_MUM_IN_GPIO_OP_OUT_ENABLE_ENABLEBIT_LBN 24
#define        MC_CMD_MUM_IN_GPIO_OP_OUT_ENABLE_ENABLEBIT_WIDTH 8

/* MC_CMD_MUM_IN_READ_SENSORS msgrequest */
#define    MC_CMD_MUM_IN_READ_SENSORS_LEN 8
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_READ_SENSORS_PARAMS_OFST 4
#define        MC_CMD_MUM_IN_READ_SENSORS_SENSOR_ID_LBN 0
#define        MC_CMD_MUM_IN_READ_SENSORS_SENSOR_ID_WIDTH 8
#define        MC_CMD_MUM_IN_READ_SENSORS_NUM_SENSORS_LBN 8
#define        MC_CMD_MUM_IN_READ_SENSORS_NUM_SENSORS_WIDTH 8

/* MC_CMD_MUM_IN_PROGRAM_CLOCKS msgrequest */
#define    MC_CMD_MUM_IN_PROGRAM_CLOCKS_LEN 12
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
/* Bit-mask of clocks to be programmed */
#define       MC_CMD_MUM_IN_PROGRAM_CLOCKS_MASK_OFST 4
#define          MC_CMD_MUM_CLOCK_ID_FPGA 0x0 /* enum */
#define          MC_CMD_MUM_CLOCK_ID_DDR 0x1 /* enum */
#define          MC_CMD_MUM_CLOCK_ID_NIC 0x2 /* enum */
/* Control flags for clock programming */
#define       MC_CMD_MUM_IN_PROGRAM_CLOCKS_FLAGS_OFST 8
#define        MC_CMD_MUM_IN_PROGRAM_CLOCKS_OVERCLOCK_110_LBN 0
#define        MC_CMD_MUM_IN_PROGRAM_CLOCKS_OVERCLOCK_110_WIDTH 1
#define        MC_CMD_MUM_IN_PROGRAM_CLOCKS_CLOCK_NIC_FROM_FPGA_LBN 1
#define        MC_CMD_MUM_IN_PROGRAM_CLOCKS_CLOCK_NIC_FROM_FPGA_WIDTH 1
#define        MC_CMD_MUM_IN_PROGRAM_CLOCKS_CLOCK_REF_FROM_XO_LBN 2
#define        MC_CMD_MUM_IN_PROGRAM_CLOCKS_CLOCK_REF_FROM_XO_WIDTH 1

/* MC_CMD_MUM_IN_FPGA_LOAD msgrequest */
#define    MC_CMD_MUM_IN_FPGA_LOAD_LEN 8
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
/* Enable/Disable FPGA config from flash */
#define       MC_CMD_MUM_IN_FPGA_LOAD_ENABLE_OFST 4

/* MC_CMD_MUM_IN_READ_ATB_SENSOR msgrequest */
#define    MC_CMD_MUM_IN_READ_ATB_SENSOR_LEN 4
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */

/* MC_CMD_MUM_IN_QSFP msgrequest */
#define    MC_CMD_MUM_IN_QSFP_LEN 12
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_QSFP_HDR_OFST 4
#define        MC_CMD_MUM_IN_QSFP_OPCODE_LBN 0
#define        MC_CMD_MUM_IN_QSFP_OPCODE_WIDTH 4
#define          MC_CMD_MUM_IN_QSFP_INIT 0x0 /* enum */
#define          MC_CMD_MUM_IN_QSFP_RECONFIGURE 0x1 /* enum */
#define          MC_CMD_MUM_IN_QSFP_GET_SUPPORTED_CAP 0x2 /* enum */
#define          MC_CMD_MUM_IN_QSFP_GET_MEDIA_INFO 0x3 /* enum */
#define          MC_CMD_MUM_IN_QSFP_FILL_STATS 0x4 /* enum */
#define          MC_CMD_MUM_IN_QSFP_POLL_BIST 0x5 /* enum */
#define       MC_CMD_MUM_IN_QSFP_IDX_OFST 8

/* MC_CMD_MUM_IN_QSFP_INIT msgrequest */
#define    MC_CMD_MUM_IN_QSFP_INIT_LEN 16
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_QSFP_INIT_HDR_OFST 4
#define       MC_CMD_MUM_IN_QSFP_INIT_IDX_OFST 8
#define       MC_CMD_MUM_IN_QSFP_INIT_CAGE_OFST 12

/* MC_CMD_MUM_IN_QSFP_RECONFIGURE msgrequest */
#define    MC_CMD_MUM_IN_QSFP_RECONFIGURE_LEN 24
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_QSFP_RECONFIGURE_HDR_OFST 4
#define       MC_CMD_MUM_IN_QSFP_RECONFIGURE_IDX_OFST 8
#define       MC_CMD_MUM_IN_QSFP_RECONFIGURE_TX_DISABLE_OFST 12
#define       MC_CMD_MUM_IN_QSFP_RECONFIGURE_PORT_LANES_OFST 16
#define       MC_CMD_MUM_IN_QSFP_RECONFIGURE_PORT_LINK_SPEED_OFST 20

/* MC_CMD_MUM_IN_QSFP_GET_SUPPORTED_CAP msgrequest */
#define    MC_CMD_MUM_IN_QSFP_GET_SUPPORTED_CAP_LEN 12
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_QSFP_GET_SUPPORTED_CAP_HDR_OFST 4
#define       MC_CMD_MUM_IN_QSFP_GET_SUPPORTED_CAP_IDX_OFST 8

/* MC_CMD_MUM_IN_QSFP_GET_MEDIA_INFO msgrequest */
#define    MC_CMD_MUM_IN_QSFP_GET_MEDIA_INFO_LEN 16
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_QSFP_GET_MEDIA_INFO_HDR_OFST 4
#define       MC_CMD_MUM_IN_QSFP_GET_MEDIA_INFO_IDX_OFST 8
#define       MC_CMD_MUM_IN_QSFP_GET_MEDIA_INFO_PAGE_OFST 12

/* MC_CMD_MUM_IN_QSFP_FILL_STATS msgrequest */
#define    MC_CMD_MUM_IN_QSFP_FILL_STATS_LEN 12
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_QSFP_FILL_STATS_HDR_OFST 4
#define       MC_CMD_MUM_IN_QSFP_FILL_STATS_IDX_OFST 8

/* MC_CMD_MUM_IN_QSFP_POLL_BIST msgrequest */
#define    MC_CMD_MUM_IN_QSFP_POLL_BIST_LEN 12
/*            MC_CMD_MUM_IN_CMD_OFST 0 */
#define       MC_CMD_MUM_IN_QSFP_POLL_BIST_HDR_OFST 4
#define       MC_CMD_MUM_IN_QSFP_POLL_BIST_IDX_OFST 8

/* MC_CMD_MUM_IN_READ_DDR_INFO msgrequest */
#define    MC_CMD_MUM_IN_READ_DDR_INFO_LEN 4
/* MUM cmd header */
/*            MC_CMD_MUM_IN_CMD_OFST 0 */

/* MC_CMD_MUM_OUT msgresponse */
#define    MC_CMD_MUM_OUT_LEN 0

/* MC_CMD_MUM_OUT_NULL msgresponse */
#define    MC_CMD_MUM_OUT_NULL_LEN 0

/* MC_CMD_MUM_OUT_GET_VERSION msgresponse */
#define    MC_CMD_MUM_OUT_GET_VERSION_LEN 12
#define       MC_CMD_MUM_OUT_GET_VERSION_FIRMWARE_OFST 0
#define       MC_CMD_MUM_OUT_GET_VERSION_VERSION_OFST 4
#define       MC_CMD_MUM_OUT_GET_VERSION_VERSION_LEN 8
#define       MC_CMD_MUM_OUT_GET_VERSION_VERSION_LO_OFST 4
#define       MC_CMD_MUM_OUT_GET_VERSION_VERSION_HI_OFST 8

/* MC_CMD_MUM_OUT_RAW_CMD msgresponse */
#define    MC_CMD_MUM_OUT_RAW_CMD_LENMIN 1
#define    MC_CMD_MUM_OUT_RAW_CMD_LENMAX 252
#define    MC_CMD_MUM_OUT_RAW_CMD_LEN(num) (0+1*(num))
/* returned data */
#define       MC_CMD_MUM_OUT_RAW_CMD_DATA_OFST 0
#define       MC_CMD_MUM_OUT_RAW_CMD_DATA_LEN 1
#define       MC_CMD_MUM_OUT_RAW_CMD_DATA_MINNUM 1
#define       MC_CMD_MUM_OUT_RAW_CMD_DATA_MAXNUM 252

/* MC_CMD_MUM_OUT_READ msgresponse */
#define    MC_CMD_MUM_OUT_READ_LENMIN 4
#define    MC_CMD_MUM_OUT_READ_LENMAX 252
#define    MC_CMD_MUM_OUT_READ_LEN(num) (0+4*(num))
#define       MC_CMD_MUM_OUT_READ_BUFFER_OFST 0
#define       MC_CMD_MUM_OUT_READ_BUFFER_LEN 4
#define       MC_CMD_MUM_OUT_READ_BUFFER_MINNUM 1
#define       MC_CMD_MUM_OUT_READ_BUFFER_MAXNUM 63

/* MC_CMD_MUM_OUT_WRITE msgresponse */
#define    MC_CMD_MUM_OUT_WRITE_LEN 0

/* MC_CMD_MUM_OUT_LOG msgresponse */
#define    MC_CMD_MUM_OUT_LOG_LEN 0

/* MC_CMD_MUM_OUT_LOG_OP_UART msgresponse */
#define    MC_CMD_MUM_OUT_LOG_OP_UART_LEN 0

/* MC_CMD_MUM_OUT_GPIO_IN_READ msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_IN_READ_LEN 8
/* The first 32-bit word read from the GPIO IN register. */
#define       MC_CMD_MUM_OUT_GPIO_IN_READ_GPIOMASK1_OFST 0
/* The second 32-bit word read from the GPIO IN register. */
#define       MC_CMD_MUM_OUT_GPIO_IN_READ_GPIOMASK2_OFST 4

/* MC_CMD_MUM_OUT_GPIO_OUT_WRITE msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_OUT_WRITE_LEN 0

/* MC_CMD_MUM_OUT_GPIO_OUT_READ msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_OUT_READ_LEN 8
/* The first 32-bit word read from the GPIO OUT register. */
#define       MC_CMD_MUM_OUT_GPIO_OUT_READ_GPIOMASK1_OFST 0
/* The second 32-bit word read from the GPIO OUT register. */
#define       MC_CMD_MUM_OUT_GPIO_OUT_READ_GPIOMASK2_OFST 4

/* MC_CMD_MUM_OUT_GPIO_OUT_ENABLE_WRITE msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_OUT_ENABLE_WRITE_LEN 0

/* MC_CMD_MUM_OUT_GPIO_OUT_ENABLE_READ msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_OUT_ENABLE_READ_LEN 8
#define       MC_CMD_MUM_OUT_GPIO_OUT_ENABLE_READ_GPIOMASK1_OFST 0
#define       MC_CMD_MUM_OUT_GPIO_OUT_ENABLE_READ_GPIOMASK2_OFST 4

/* MC_CMD_MUM_OUT_GPIO_OP_OUT_READ msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_OP_OUT_READ_LEN 4
#define       MC_CMD_MUM_OUT_GPIO_OP_OUT_READ_BIT_READ_OFST 0

/* MC_CMD_MUM_OUT_GPIO_OP_OUT_WRITE msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_OP_OUT_WRITE_LEN 0

/* MC_CMD_MUM_OUT_GPIO_OP_OUT_CONFIG msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_OP_OUT_CONFIG_LEN 0

/* MC_CMD_MUM_OUT_GPIO_OP_OUT_ENABLE msgresponse */
#define    MC_CMD_MUM_OUT_GPIO_OP_OUT_ENABLE_LEN 0

/* MC_CMD_MUM_OUT_READ_SENSORS msgresponse */
#define    MC_CMD_MUM_OUT_READ_SENSORS_LENMIN 4
#define    MC_CMD_MUM_OUT_READ_SENSORS_LENMAX 252
#define    MC_CMD_MUM_OUT_READ_SENSORS_LEN(num) (0+4*(num))
#define       MC_CMD_MUM_OUT_READ_SENSORS_DATA_OFST 0
#define       MC_CMD_MUM_OUT_READ_SENSORS_DATA_LEN 4
#define       MC_CMD_MUM_OUT_READ_SENSORS_DATA_MINNUM 1
#define       MC_CMD_MUM_OUT_READ_SENSORS_DATA_MAXNUM 63
#define        MC_CMD_MUM_OUT_READ_SENSORS_READING_LBN 0
#define        MC_CMD_MUM_OUT_READ_SENSORS_READING_WIDTH 16
#define        MC_CMD_MUM_OUT_READ_SENSORS_STATE_LBN 16
#define        MC_CMD_MUM_OUT_READ_SENSORS_STATE_WIDTH 8
#define        MC_CMD_MUM_OUT_READ_SENSORS_TYPE_LBN 24
#define        MC_CMD_MUM_OUT_READ_SENSORS_TYPE_WIDTH 8

/* MC_CMD_MUM_OUT_PROGRAM_CLOCKS msgresponse */
#define    MC_CMD_MUM_OUT_PROGRAM_CLOCKS_LEN 4
#define       MC_CMD_MUM_OUT_PROGRAM_CLOCKS_OK_MASK_OFST 0

/* MC_CMD_MUM_OUT_FPGA_LOAD msgresponse */
#define    MC_CMD_MUM_OUT_FPGA_LOAD_LEN 0

/* MC_CMD_MUM_OUT_READ_ATB_SENSOR msgresponse */
#define    MC_CMD_MUM_OUT_READ_ATB_SENSOR_LEN 4
#define       MC_CMD_MUM_OUT_READ_ATB_SENSOR_RESULT_OFST 0

/* MC_CMD_MUM_OUT_QSFP_INIT msgresponse */
#define    MC_CMD_MUM_OUT_QSFP_INIT_LEN 0

/* MC_CMD_MUM_OUT_QSFP_RECONFIGURE msgresponse */
#define    MC_CMD_MUM_OUT_QSFP_RECONFIGURE_LEN 8
#define       MC_CMD_MUM_OUT_QSFP_RECONFIGURE_PORT_PHY_LP_CAP_OFST 0
#define       MC_CMD_MUM_OUT_QSFP_RECONFIGURE_PORT_PHY_FLAGS_OFST 4
#define        MC_CMD_MUM_OUT_QSFP_RECONFIGURE_PORT_PHY_READY_LBN 0
#define        MC_CMD_MUM_OUT_QSFP_RECONFIGURE_PORT_PHY_READY_WIDTH 1
#define        MC_CMD_MUM_OUT_QSFP_RECONFIGURE_PORT_PHY_LINK_UP_LBN 1
#define        MC_CMD_MUM_OUT_QSFP_RECONFIGURE_PORT_PHY_LINK_UP_WIDTH 1

/* MC_CMD_MUM_OUT_QSFP_GET_SUPPORTED_CAP msgresponse */
#define    MC_CMD_MUM_OUT_QSFP_GET_SUPPORTED_CAP_LEN 4
#define       MC_CMD_MUM_OUT_QSFP_GET_SUPPORTED_CAP_PORT_PHY_LP_CAP_OFST 0

/* MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO msgresponse */
#define    MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO_LENMIN 5
#define    MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO_LENMAX 252
#define    MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO_LEN(num) (4+1*(num))
/* in bytes */
#define       MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO_DATALEN_OFST 0
#define       MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO_DATA_OFST 4
#define       MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO_DATA_LEN 1
#define       MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO_DATA_MINNUM 1
#define       MC_CMD_MUM_OUT_QSFP_GET_MEDIA_INFO_DATA_MAXNUM 248

/* MC_CMD_MUM_OUT_QSFP_FILL_STATS msgresponse */
#define    MC_CMD_MUM_OUT_QSFP_FILL_STATS_LEN 8
#define       MC_CMD_MUM_OUT_QSFP_FILL_STATS_PORT_PHY_STATS_PMA_PMD_LINK_UP_OFST 0
#define       MC_CMD_MUM_OUT_QSFP_FILL_STATS_PORT_PHY_STATS_PCS_LINK_UP_OFST 4

/* MC_CMD_MUM_OUT_QSFP_POLL_BIST msgresponse */
#define    MC_CMD_MUM_OUT_QSFP_POLL_BIST_LEN 4
#define       MC_CMD_MUM_OUT_QSFP_POLL_BIST_TEST_OFST 0

/* MC_CMD_MUM_OUT_READ_DDR_INFO msgresponse */
#define    MC_CMD_MUM_OUT_READ_DDR_INFO_LENMIN 24
#define    MC_CMD_MUM_OUT_READ_DDR_INFO_LENMAX 248
#define    MC_CMD_MUM_OUT_READ_DDR_INFO_LEN(num) (8+8*(num))
/* Discrete (soldered) DDR resistor strap info */
#define       MC_CMD_MUM_OUT_READ_DDR_INFO_DISCRETE_DDR_INFO_OFST 0
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_VRATIO_LBN 0
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_VRATIO_WIDTH 16
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_RESERVED1_LBN 16
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_RESERVED1_WIDTH 16
/* Number of SODIMM info records */
#define       MC_CMD_MUM_OUT_READ_DDR_INFO_NUM_RECORDS_OFST 4
/* Array of SODIMM info records */
#define       MC_CMD_MUM_OUT_READ_DDR_INFO_SODIMM_INFO_RECORD_OFST 8
#define       MC_CMD_MUM_OUT_READ_DDR_INFO_SODIMM_INFO_RECORD_LEN 8
#define       MC_CMD_MUM_OUT_READ_DDR_INFO_SODIMM_INFO_RECORD_LO_OFST 8
#define       MC_CMD_MUM_OUT_READ_DDR_INFO_SODIMM_INFO_RECORD_HI_OFST 12
#define       MC_CMD_MUM_OUT_READ_DDR_INFO_SODIMM_INFO_RECORD_MINNUM 2
#define       MC_CMD_MUM_OUT_READ_DDR_INFO_SODIMM_INFO_RECORD_MAXNUM 30
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_BANK_ID_LBN 0
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_BANK_ID_WIDTH 8
/* enum: SODIMM bank 1 (Top SODIMM for Sorrento) */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_BANK1 0x0
/* enum: SODIMM bank 2 (Bottom SODDIMM for Sorrento) */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_BANK2 0x1
/* enum: Total number of SODIMM banks */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_NUM_BANKS 0x2
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_TYPE_LBN 8
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_TYPE_WIDTH 8
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_RANK_LBN 16
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_RANK_WIDTH 4
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_VOLTAGE_LBN 20
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_VOLTAGE_WIDTH 4
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_NOT_POWERED 0x0 /* enum */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_1V25 0x1 /* enum */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_1V35 0x2 /* enum */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_1V5 0x3 /* enum */
/* enum: Values 5-15 are reserved for future usage */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_1V8 0x4
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_SIZE_LBN 24
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_SIZE_WIDTH 8
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_SPEED_LBN 32
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_SPEED_WIDTH 16
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_STATE_LBN 48
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_STATE_WIDTH 4
/* enum: No module present */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_ABSENT 0x0
/* enum: Module present supported and powered on */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_PRESENT_POWERED 0x1
/* enum: Module present but bad type */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_PRESENT_BAD_TYPE 0x2
/* enum: Module present but incompatible voltage */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_PRESENT_BAD_VOLTAGE 0x3
/* enum: Module present but unknown SPD */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_PRESENT_BAD_SPD 0x4
/* enum: Module present but slot cannot support it */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_PRESENT_BAD_SLOT 0x5
/* enum: Modules may or may not be present, but cannot establish contact by I2C
 */
#define          MC_CMD_MUM_OUT_READ_DDR_INFO_NOT_REACHABLE 0x6
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_RESERVED2_LBN 52
#define        MC_CMD_MUM_OUT_READ_DDR_INFO_RESERVED2_WIDTH 12

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
/* enum: Synonym for EXPROM_CONFIG_PORT0 as used in pmap files */
#define          NVRAM_PARTITION_TYPE_EXPROM_CONFIG        0x600
/* enum: Expansion ROM configuration data for port 1 */
#define          NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT1  0x601
/* enum: Expansion ROM configuration data for port 2 */
#define          NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT2  0x602
/* enum: Expansion ROM configuration data for port 3 */
#define          NVRAM_PARTITION_TYPE_EXPROM_CONFIG_PORT3  0x603
/* enum: Non-volatile log output partition */
#define          NVRAM_PARTITION_TYPE_LOG                  0x700
/* enum: Non-volatile log output of second core on dual-core device */
#define          NVRAM_PARTITION_TYPE_LOG_SLAVE            0x701
/* enum: Device state dump output partition */
#define          NVRAM_PARTITION_TYPE_DUMP                 0x800
/* enum: Application license key storage partition */
#define          NVRAM_PARTITION_TYPE_LICENSE              0x900
/* enum: Start of range used for PHY partitions (low 8 bits are the PHY ID) */
#define          NVRAM_PARTITION_TYPE_PHY_MIN              0xa00
/* enum: End of range used for PHY partitions (low 8 bits are the PHY ID) */
#define          NVRAM_PARTITION_TYPE_PHY_MAX              0xaff
/* enum: Primary FPGA partition */
#define          NVRAM_PARTITION_TYPE_FPGA                 0xb00
/* enum: Secondary FPGA partition */
#define          NVRAM_PARTITION_TYPE_FPGA_BACKUP          0xb01
/* enum: FC firmware partition */
#define          NVRAM_PARTITION_TYPE_FC_FIRMWARE          0xb02
/* enum: FC License partition */
#define          NVRAM_PARTITION_TYPE_FC_LICENSE           0xb03
/* enum: Non-volatile log output partition for FC */
#define          NVRAM_PARTITION_TYPE_FC_LOG               0xb04
/* enum: MUM firmware partition */
#define          NVRAM_PARTITION_TYPE_MUM_FIRMWARE         0xc00
/* enum: MUM Non-volatile log output partition. */
#define          NVRAM_PARTITION_TYPE_MUM_LOG              0xc01
/* enum: MUM Application table partition. */
#define          NVRAM_PARTITION_TYPE_MUM_APPTABLE         0xc02
/* enum: MUM boot rom partition. */
#define          NVRAM_PARTITION_TYPE_MUM_BOOT_ROM         0xc03
/* enum: MUM production signatures & calibration rom partition. */
#define          NVRAM_PARTITION_TYPE_MUM_PROD_ROM         0xc04
/* enum: MUM user signatures & calibration rom partition. */
#define          NVRAM_PARTITION_TYPE_MUM_USER_ROM         0xc05
/* enum: MUM fuses and lockbits partition. */
#define          NVRAM_PARTITION_TYPE_MUM_FUSELOCK         0xc06
/* enum: UEFI expansion ROM if separate from PXE */
#define          NVRAM_PARTITION_TYPE_EXPANSION_UEFI       0xd00
/* enum: Spare partition 0 */
#define          NVRAM_PARTITION_TYPE_SPARE_0              0x1000
/* enum: Used for XIP code of shmbooted images */
#define          NVRAM_PARTITION_TYPE_XIP_SCRATCH          0x1100
/* enum: Spare partition 2 */
#define          NVRAM_PARTITION_TYPE_SPARE_2              0x1200
/* enum: Manufacturing partition. Used during manufacture to pass information
 * between XJTAG and Manftest.
 */
#define          NVRAM_PARTITION_TYPE_MANUFACTURING        0x1300
/* enum: Spare partition 4 */
#define          NVRAM_PARTITION_TYPE_SPARE_4              0x1400
/* enum: Spare partition 5 */
#define          NVRAM_PARTITION_TYPE_SPARE_5              0x1500
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

/* LICENSED_APP_ID structuredef */
#define    LICENSED_APP_ID_LEN 4
#define       LICENSED_APP_ID_ID_OFST 0
/* enum: OpenOnload */
#define          LICENSED_APP_ID_ONLOAD                  0x1
/* enum: PTP timestamping */
#define          LICENSED_APP_ID_PTP                     0x2
/* enum: SolarCapture Pro */
#define          LICENSED_APP_ID_SOLARCAPTURE_PRO        0x4
/* enum: SolarSecure filter engine */
#define          LICENSED_APP_ID_SOLARSECURE             0x8
/* enum: Performance monitor */
#define          LICENSED_APP_ID_PERF_MONITOR            0x10
/* enum: SolarCapture Live */
#define          LICENSED_APP_ID_SOLARCAPTURE_LIVE       0x20
/* enum: Capture SolarSystem */
#define          LICENSED_APP_ID_CAPTURE_SOLARSYSTEM     0x40
/* enum: Network Access Control */
#define          LICENSED_APP_ID_NETWORK_ACCESS_CONTROL  0x80
/* enum: TCP Direct */
#define          LICENSED_APP_ID_TCP_DIRECT              0x100
/* enum: Low Latency */
#define          LICENSED_APP_ID_LOW_LATENCY             0x200
/* enum: SolarCapture Tap */
#define          LICENSED_APP_ID_SOLARCAPTURE_TAP        0x400
/* enum: Capture SolarSystem 40G */
#define          LICENSED_APP_ID_CAPTURE_SOLARSYSTEM_40G 0x800
#define       LICENSED_APP_ID_ID_LBN 0
#define       LICENSED_APP_ID_ID_WIDTH 32

/* LICENSED_FEATURES structuredef */
#define    LICENSED_FEATURES_LEN 8
/* Bitmask of licensed firmware features */
#define       LICENSED_FEATURES_MASK_OFST 0
#define       LICENSED_FEATURES_MASK_LEN 8
#define       LICENSED_FEATURES_MASK_LO_OFST 0
#define       LICENSED_FEATURES_MASK_HI_OFST 4
#define        LICENSED_FEATURES_RX_CUT_THROUGH_LBN 0
#define        LICENSED_FEATURES_RX_CUT_THROUGH_WIDTH 1
#define        LICENSED_FEATURES_PIO_LBN 1
#define        LICENSED_FEATURES_PIO_WIDTH 1
#define        LICENSED_FEATURES_EVQ_TIMER_LBN 2
#define        LICENSED_FEATURES_EVQ_TIMER_WIDTH 1
#define        LICENSED_FEATURES_CLOCK_LBN 3
#define        LICENSED_FEATURES_CLOCK_WIDTH 1
#define        LICENSED_FEATURES_RX_TIMESTAMPS_LBN 4
#define        LICENSED_FEATURES_RX_TIMESTAMPS_WIDTH 1
#define        LICENSED_FEATURES_TX_TIMESTAMPS_LBN 5
#define        LICENSED_FEATURES_TX_TIMESTAMPS_WIDTH 1
#define        LICENSED_FEATURES_RX_SNIFF_LBN 6
#define        LICENSED_FEATURES_RX_SNIFF_WIDTH 1
#define        LICENSED_FEATURES_TX_SNIFF_LBN 7
#define        LICENSED_FEATURES_TX_SNIFF_WIDTH 1
#define        LICENSED_FEATURES_PROXY_FILTER_OPS_LBN 8
#define        LICENSED_FEATURES_PROXY_FILTER_OPS_WIDTH 1
#define        LICENSED_FEATURES_EVENT_CUT_THROUGH_LBN 9
#define        LICENSED_FEATURES_EVENT_CUT_THROUGH_WIDTH 1
#define       LICENSED_FEATURES_MASK_LBN 0
#define       LICENSED_FEATURES_MASK_WIDTH 64

/* LICENSED_V3_APPS structuredef */
#define    LICENSED_V3_APPS_LEN 8
/* Bitmask of licensed applications */
#define       LICENSED_V3_APPS_MASK_OFST 0
#define       LICENSED_V3_APPS_MASK_LEN 8
#define       LICENSED_V3_APPS_MASK_LO_OFST 0
#define       LICENSED_V3_APPS_MASK_HI_OFST 4
#define        LICENSED_V3_APPS_ONLOAD_LBN 0
#define        LICENSED_V3_APPS_ONLOAD_WIDTH 1
#define        LICENSED_V3_APPS_PTP_LBN 1
#define        LICENSED_V3_APPS_PTP_WIDTH 1
#define        LICENSED_V3_APPS_SOLARCAPTURE_PRO_LBN 2
#define        LICENSED_V3_APPS_SOLARCAPTURE_PRO_WIDTH 1
#define        LICENSED_V3_APPS_SOLARSECURE_LBN 3
#define        LICENSED_V3_APPS_SOLARSECURE_WIDTH 1
#define        LICENSED_V3_APPS_PERF_MONITOR_LBN 4
#define        LICENSED_V3_APPS_PERF_MONITOR_WIDTH 1
#define        LICENSED_V3_APPS_SOLARCAPTURE_LIVE_LBN 5
#define        LICENSED_V3_APPS_SOLARCAPTURE_LIVE_WIDTH 1
#define        LICENSED_V3_APPS_CAPTURE_SOLARSYSTEM_LBN 6
#define        LICENSED_V3_APPS_CAPTURE_SOLARSYSTEM_WIDTH 1
#define        LICENSED_V3_APPS_NETWORK_ACCESS_CONTROL_LBN 7
#define        LICENSED_V3_APPS_NETWORK_ACCESS_CONTROL_WIDTH 1
#define        LICENSED_V3_APPS_TCP_DIRECT_LBN 8
#define        LICENSED_V3_APPS_TCP_DIRECT_WIDTH 1
#define        LICENSED_V3_APPS_LOW_LATENCY_LBN 9
#define        LICENSED_V3_APPS_LOW_LATENCY_WIDTH 1
#define        LICENSED_V3_APPS_SOLARCAPTURE_TAP_LBN 10
#define        LICENSED_V3_APPS_SOLARCAPTURE_TAP_WIDTH 1
#define        LICENSED_V3_APPS_CAPTURE_SOLARSYSTEM_40G_LBN 11
#define        LICENSED_V3_APPS_CAPTURE_SOLARSYSTEM_40G_WIDTH 1
#define       LICENSED_V3_APPS_MASK_LBN 0
#define       LICENSED_V3_APPS_MASK_WIDTH 64

/* LICENSED_V3_FEATURES structuredef */
#define    LICENSED_V3_FEATURES_LEN 8
/* Bitmask of licensed firmware features */
#define       LICENSED_V3_FEATURES_MASK_OFST 0
#define       LICENSED_V3_FEATURES_MASK_LEN 8
#define       LICENSED_V3_FEATURES_MASK_LO_OFST 0
#define       LICENSED_V3_FEATURES_MASK_HI_OFST 4
#define        LICENSED_V3_FEATURES_RX_CUT_THROUGH_LBN 0
#define        LICENSED_V3_FEATURES_RX_CUT_THROUGH_WIDTH 1
#define        LICENSED_V3_FEATURES_PIO_LBN 1
#define        LICENSED_V3_FEATURES_PIO_WIDTH 1
#define        LICENSED_V3_FEATURES_EVQ_TIMER_LBN 2
#define        LICENSED_V3_FEATURES_EVQ_TIMER_WIDTH 1
#define        LICENSED_V3_FEATURES_CLOCK_LBN 3
#define        LICENSED_V3_FEATURES_CLOCK_WIDTH 1
#define        LICENSED_V3_FEATURES_RX_TIMESTAMPS_LBN 4
#define        LICENSED_V3_FEATURES_RX_TIMESTAMPS_WIDTH 1
#define        LICENSED_V3_FEATURES_TX_TIMESTAMPS_LBN 5
#define        LICENSED_V3_FEATURES_TX_TIMESTAMPS_WIDTH 1
#define        LICENSED_V3_FEATURES_RX_SNIFF_LBN 6
#define        LICENSED_V3_FEATURES_RX_SNIFF_WIDTH 1
#define        LICENSED_V3_FEATURES_TX_SNIFF_LBN 7
#define        LICENSED_V3_FEATURES_TX_SNIFF_WIDTH 1
#define        LICENSED_V3_FEATURES_PROXY_FILTER_OPS_LBN 8
#define        LICENSED_V3_FEATURES_PROXY_FILTER_OPS_WIDTH 1
#define        LICENSED_V3_FEATURES_EVENT_CUT_THROUGH_LBN 9
#define        LICENSED_V3_FEATURES_EVENT_CUT_THROUGH_WIDTH 1
#define       LICENSED_V3_FEATURES_MASK_LBN 0
#define       LICENSED_V3_FEATURES_MASK_WIDTH 64

/* TX_TIMESTAMP_EVENT structuredef */
#define    TX_TIMESTAMP_EVENT_LEN 6
/* lower 16 bits of timestamp data */
#define       TX_TIMESTAMP_EVENT_TSTAMP_DATA_LO_OFST 0
#define       TX_TIMESTAMP_EVENT_TSTAMP_DATA_LO_LEN 2
#define       TX_TIMESTAMP_EVENT_TSTAMP_DATA_LO_LBN 0
#define       TX_TIMESTAMP_EVENT_TSTAMP_DATA_LO_WIDTH 16
/* Type of TX event, ordinary TX completion, low or high part of TX timestamp
 */
#define       TX_TIMESTAMP_EVENT_TX_EV_TYPE_OFST 3
#define       TX_TIMESTAMP_EVENT_TX_EV_TYPE_LEN 1
/* enum: This is a TX completion event, not a timestamp */
#define          TX_TIMESTAMP_EVENT_TX_EV_COMPLETION  0x0
/* enum: This is the low part of a TX timestamp event */
#define          TX_TIMESTAMP_EVENT_TX_EV_TSTAMP_LO  0x51
/* enum: This is the high part of a TX timestamp event */
#define          TX_TIMESTAMP_EVENT_TX_EV_TSTAMP_HI  0x52
#define       TX_TIMESTAMP_EVENT_TX_EV_TYPE_LBN 24
#define       TX_TIMESTAMP_EVENT_TX_EV_TYPE_WIDTH 8
/* upper 16 bits of timestamp data */
#define       TX_TIMESTAMP_EVENT_TSTAMP_DATA_HI_OFST 4
#define       TX_TIMESTAMP_EVENT_TSTAMP_DATA_HI_LEN 2
#define       TX_TIMESTAMP_EVENT_TSTAMP_DATA_HI_LBN 32
#define       TX_TIMESTAMP_EVENT_TSTAMP_DATA_HI_WIDTH 16

/* RSS_MODE structuredef */
#define    RSS_MODE_LEN 1
/* The RSS mode for a particular packet type is a value from 0 - 15 which can
 * be considered as 4 bits selecting which fields are included in the hash. (A
 * value 0 effectively disables RSS spreading for the packet type.) The YAML
 * generation tools require this structure to be a whole number of bytes wide,
 * but only 4 bits are relevant.
 */
#define       RSS_MODE_HASH_SELECTOR_OFST 0
#define       RSS_MODE_HASH_SELECTOR_LEN 1
#define        RSS_MODE_HASH_SRC_ADDR_LBN 0
#define        RSS_MODE_HASH_SRC_ADDR_WIDTH 1
#define        RSS_MODE_HASH_DST_ADDR_LBN 1
#define        RSS_MODE_HASH_DST_ADDR_WIDTH 1
#define        RSS_MODE_HASH_SRC_PORT_LBN 2
#define        RSS_MODE_HASH_SRC_PORT_WIDTH 1
#define        RSS_MODE_HASH_DST_PORT_LBN 3
#define        RSS_MODE_HASH_DST_PORT_WIDTH 1
#define       RSS_MODE_HASH_SELECTOR_LBN 0
#define       RSS_MODE_HASH_SELECTOR_WIDTH 8


/***********************************/
/* MC_CMD_READ_REGS
 * Get a dump of the MCPU registers
 */
#define MC_CMD_READ_REGS 0x50

#define MC_CMD_0x50_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x80_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
#define        MC_CMD_INIT_EVQ_IN_FLAG_USE_TIMER_LBN 6
#define        MC_CMD_INIT_EVQ_IN_FLAG_USE_TIMER_WIDTH 1
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

/* MC_CMD_INIT_EVQ_V2_IN msgrequest */
#define    MC_CMD_INIT_EVQ_V2_IN_LENMIN 44
#define    MC_CMD_INIT_EVQ_V2_IN_LENMAX 548
#define    MC_CMD_INIT_EVQ_V2_IN_LEN(num) (36+8*(num))
/* Size, in entries */
#define       MC_CMD_INIT_EVQ_V2_IN_SIZE_OFST 0
/* Desired instance. Must be set to a specific instance, which is a function
 * local queue index.
 */
#define       MC_CMD_INIT_EVQ_V2_IN_INSTANCE_OFST 4
/* The initial timer value. The load value is ignored if the timer mode is DIS.
 */
#define       MC_CMD_INIT_EVQ_V2_IN_TMR_LOAD_OFST 8
/* The reload value is ignored in one-shot modes */
#define       MC_CMD_INIT_EVQ_V2_IN_TMR_RELOAD_OFST 12
/* tbd */
#define       MC_CMD_INIT_EVQ_V2_IN_FLAGS_OFST 16
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_INTERRUPTING_LBN 0
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_INTERRUPTING_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_RPTR_DOS_LBN 1
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_RPTR_DOS_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_INT_ARMD_LBN 2
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_INT_ARMD_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_CUT_THRU_LBN 3
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_CUT_THRU_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_RX_MERGE_LBN 4
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_RX_MERGE_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_TX_MERGE_LBN 5
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_TX_MERGE_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_USE_TIMER_LBN 6
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_USE_TIMER_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_TYPE_LBN 7
#define        MC_CMD_INIT_EVQ_V2_IN_FLAG_TYPE_WIDTH 4
/* enum: All initialisation flags specified by host. */
#define          MC_CMD_INIT_EVQ_V2_IN_FLAG_TYPE_MANUAL 0x0
/* enum: MEDFORD only. Certain initialisation flags specified by host may be
 * over-ridden by firmware based on licenses and firmware variant in order to
 * provide the lowest latency achievable. See
 * MC_CMD_INIT_EVQ_V2/MC_CMD_INIT_EVQ_V2_OUT/FLAGS for list of affected flags.
 */
#define          MC_CMD_INIT_EVQ_V2_IN_FLAG_TYPE_LOW_LATENCY 0x1
/* enum: MEDFORD only. Certain initialisation flags specified by host may be
 * over-ridden by firmware based on licenses and firmware variant in order to
 * provide the best throughput achievable. See
 * MC_CMD_INIT_EVQ_V2/MC_CMD_INIT_EVQ_V2_OUT/FLAGS for list of affected flags.
 */
#define          MC_CMD_INIT_EVQ_V2_IN_FLAG_TYPE_THROUGHPUT 0x2
/* enum: MEDFORD only. Certain initialisation flags may be over-ridden by
 * firmware based on licenses and firmware variant. See
 * MC_CMD_INIT_EVQ_V2/MC_CMD_INIT_EVQ_V2_OUT/FLAGS for list of affected flags.
 */
#define          MC_CMD_INIT_EVQ_V2_IN_FLAG_TYPE_AUTO 0x3
#define       MC_CMD_INIT_EVQ_V2_IN_TMR_MODE_OFST 20
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_V2_IN_TMR_MODE_DIS 0x0
/* enum: Immediate */
#define          MC_CMD_INIT_EVQ_V2_IN_TMR_IMMED_START 0x1
/* enum: Triggered */
#define          MC_CMD_INIT_EVQ_V2_IN_TMR_TRIG_START 0x2
/* enum: Hold-off */
#define          MC_CMD_INIT_EVQ_V2_IN_TMR_INT_HLDOFF 0x3
/* Target EVQ for wakeups if in wakeup mode. */
#define       MC_CMD_INIT_EVQ_V2_IN_TARGET_EVQ_OFST 24
/* Target interrupt if in interrupting mode (note union with target EVQ). Use
 * MC_CMD_RESOURCE_INSTANCE_ANY unless a specific one required for test
 * purposes.
 */
#define       MC_CMD_INIT_EVQ_V2_IN_IRQ_NUM_OFST 24
/* Event Counter Mode. */
#define       MC_CMD_INIT_EVQ_V2_IN_COUNT_MODE_OFST 28
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_V2_IN_COUNT_MODE_DIS 0x0
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_V2_IN_COUNT_MODE_RX 0x1
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_V2_IN_COUNT_MODE_TX 0x2
/* enum: Disabled */
#define          MC_CMD_INIT_EVQ_V2_IN_COUNT_MODE_RXTX 0x3
/* Event queue packet count threshold. */
#define       MC_CMD_INIT_EVQ_V2_IN_COUNT_THRSHLD_OFST 32
/* 64-bit address of 4k of 4k-aligned host memory buffer */
#define       MC_CMD_INIT_EVQ_V2_IN_DMA_ADDR_OFST 36
#define       MC_CMD_INIT_EVQ_V2_IN_DMA_ADDR_LEN 8
#define       MC_CMD_INIT_EVQ_V2_IN_DMA_ADDR_LO_OFST 36
#define       MC_CMD_INIT_EVQ_V2_IN_DMA_ADDR_HI_OFST 40
#define       MC_CMD_INIT_EVQ_V2_IN_DMA_ADDR_MINNUM 1
#define       MC_CMD_INIT_EVQ_V2_IN_DMA_ADDR_MAXNUM 64

/* MC_CMD_INIT_EVQ_V2_OUT msgresponse */
#define    MC_CMD_INIT_EVQ_V2_OUT_LEN 8
/* Only valid if INTRFLAG was true */
#define       MC_CMD_INIT_EVQ_V2_OUT_IRQ_OFST 0
/* Actual configuration applied on the card */
#define       MC_CMD_INIT_EVQ_V2_OUT_FLAGS_OFST 4
#define        MC_CMD_INIT_EVQ_V2_OUT_FLAG_CUT_THRU_LBN 0
#define        MC_CMD_INIT_EVQ_V2_OUT_FLAG_CUT_THRU_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_OUT_FLAG_RX_MERGE_LBN 1
#define        MC_CMD_INIT_EVQ_V2_OUT_FLAG_RX_MERGE_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_OUT_FLAG_TX_MERGE_LBN 2
#define        MC_CMD_INIT_EVQ_V2_OUT_FLAG_TX_MERGE_WIDTH 1
#define        MC_CMD_INIT_EVQ_V2_OUT_FLAG_RXQ_FORCE_EV_MERGING_LBN 3
#define        MC_CMD_INIT_EVQ_V2_OUT_FLAG_RXQ_FORCE_EV_MERGING_WIDTH 1

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

#define MC_CMD_0x81_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_INIT_RXQ_IN msgrequest: Legacy RXQ_INIT request. Use extended version
 * in new code.
 */
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
#define        MC_CMD_INIT_RXQ_IN_FLAG_DISABLE_SCATTER_LBN 9
#define        MC_CMD_INIT_RXQ_IN_FLAG_DISABLE_SCATTER_WIDTH 1
#define        MC_CMD_INIT_RXQ_IN_UNUSED_LBN 10
#define        MC_CMD_INIT_RXQ_IN_UNUSED_WIDTH 1
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

/* MC_CMD_INIT_RXQ_EXT_IN msgrequest: Extended RXQ_INIT with additional mode
 * flags
 */
#define    MC_CMD_INIT_RXQ_EXT_IN_LEN 544
/* Size, in entries */
#define       MC_CMD_INIT_RXQ_EXT_IN_SIZE_OFST 0
/* The EVQ to send events to. This is an index originally specified to INIT_EVQ
 */
#define       MC_CMD_INIT_RXQ_EXT_IN_TARGET_EVQ_OFST 4
/* The value to put in the event data. Check hardware spec. for valid range. */
#define       MC_CMD_INIT_RXQ_EXT_IN_LABEL_OFST 8
/* Desired instance. Must be set to a specific instance, which is a function
 * local queue index.
 */
#define       MC_CMD_INIT_RXQ_EXT_IN_INSTANCE_OFST 12
/* There will be more flags here. */
#define       MC_CMD_INIT_RXQ_EXT_IN_FLAGS_OFST 16
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_BUFF_MODE_LBN 0
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_BUFF_MODE_WIDTH 1
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_HDR_SPLIT_LBN 1
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_HDR_SPLIT_WIDTH 1
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_TIMESTAMP_LBN 2
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_TIMESTAMP_WIDTH 1
#define        MC_CMD_INIT_RXQ_EXT_IN_CRC_MODE_LBN 3
#define        MC_CMD_INIT_RXQ_EXT_IN_CRC_MODE_WIDTH 4
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_CHAIN_LBN 7
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_CHAIN_WIDTH 1
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_PREFIX_LBN 8
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_PREFIX_WIDTH 1
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_DISABLE_SCATTER_LBN 9
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_DISABLE_SCATTER_WIDTH 1
#define        MC_CMD_INIT_RXQ_EXT_IN_DMA_MODE_LBN 10
#define        MC_CMD_INIT_RXQ_EXT_IN_DMA_MODE_WIDTH 4
/* enum: One packet per descriptor (for normal networking) */
#define          MC_CMD_INIT_RXQ_EXT_IN_SINGLE_PACKET  0x0
/* enum: Pack multiple packets into large descriptors (for SolarCapture) */
#define          MC_CMD_INIT_RXQ_EXT_IN_PACKED_STREAM  0x1
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_SNAPSHOT_MODE_LBN 14
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_SNAPSHOT_MODE_WIDTH 1
#define        MC_CMD_INIT_RXQ_EXT_IN_PACKED_STREAM_BUFF_SIZE_LBN 15
#define        MC_CMD_INIT_RXQ_EXT_IN_PACKED_STREAM_BUFF_SIZE_WIDTH 3
#define          MC_CMD_INIT_RXQ_EXT_IN_PS_BUFF_1M  0x0 /* enum */
#define          MC_CMD_INIT_RXQ_EXT_IN_PS_BUFF_512K  0x1 /* enum */
#define          MC_CMD_INIT_RXQ_EXT_IN_PS_BUFF_256K  0x2 /* enum */
#define          MC_CMD_INIT_RXQ_EXT_IN_PS_BUFF_128K  0x3 /* enum */
#define          MC_CMD_INIT_RXQ_EXT_IN_PS_BUFF_64K  0x4 /* enum */
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_WANT_OUTER_CLASSES_LBN 18
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_WANT_OUTER_CLASSES_WIDTH 1
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_FORCE_EV_MERGING_LBN 19
#define        MC_CMD_INIT_RXQ_EXT_IN_FLAG_FORCE_EV_MERGING_WIDTH 1
/* Owner ID to use if in buffer mode (zero if physical) */
#define       MC_CMD_INIT_RXQ_EXT_IN_OWNER_ID_OFST 20
/* The port ID associated with the v-adaptor which should contain this DMAQ. */
#define       MC_CMD_INIT_RXQ_EXT_IN_PORT_ID_OFST 24
/* 64-bit address of 4k of 4k-aligned host memory buffer */
#define       MC_CMD_INIT_RXQ_EXT_IN_DMA_ADDR_OFST 28
#define       MC_CMD_INIT_RXQ_EXT_IN_DMA_ADDR_LEN 8
#define       MC_CMD_INIT_RXQ_EXT_IN_DMA_ADDR_LO_OFST 28
#define       MC_CMD_INIT_RXQ_EXT_IN_DMA_ADDR_HI_OFST 32
#define       MC_CMD_INIT_RXQ_EXT_IN_DMA_ADDR_NUM 64
/* Maximum length of packet to receive, if SNAPSHOT_MODE flag is set */
#define       MC_CMD_INIT_RXQ_EXT_IN_SNAPSHOT_LENGTH_OFST 540

/* MC_CMD_INIT_RXQ_OUT msgresponse */
#define    MC_CMD_INIT_RXQ_OUT_LEN 0

/* MC_CMD_INIT_RXQ_EXT_OUT msgresponse */
#define    MC_CMD_INIT_RXQ_EXT_OUT_LEN 0


/***********************************/
/* MC_CMD_INIT_TXQ
 */
#define MC_CMD_INIT_TXQ 0x82

#define MC_CMD_0x82_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_INIT_TXQ_IN msgrequest: Legacy INIT_TXQ request. Use extended version
 * in new code.
 */
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
#define        MC_CMD_INIT_TXQ_IN_FLAG_INNER_IP_CSUM_EN_LBN 10
#define        MC_CMD_INIT_TXQ_IN_FLAG_INNER_IP_CSUM_EN_WIDTH 1
#define        MC_CMD_INIT_TXQ_IN_FLAG_INNER_TCP_CSUM_EN_LBN 11
#define        MC_CMD_INIT_TXQ_IN_FLAG_INNER_TCP_CSUM_EN_WIDTH 1
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

/* MC_CMD_INIT_TXQ_EXT_IN msgrequest: Extended INIT_TXQ with additional mode
 * flags
 */
#define    MC_CMD_INIT_TXQ_EXT_IN_LEN 544
/* Size, in entries */
#define       MC_CMD_INIT_TXQ_EXT_IN_SIZE_OFST 0
/* The EVQ to send events to. This is an index originally specified to
 * INIT_EVQ.
 */
#define       MC_CMD_INIT_TXQ_EXT_IN_TARGET_EVQ_OFST 4
/* The value to put in the event data. Check hardware spec. for valid range. */
#define       MC_CMD_INIT_TXQ_EXT_IN_LABEL_OFST 8
/* Desired instance. Must be set to a specific instance, which is a function
 * local queue index.
 */
#define       MC_CMD_INIT_TXQ_EXT_IN_INSTANCE_OFST 12
/* There will be more flags here. */
#define       MC_CMD_INIT_TXQ_EXT_IN_FLAGS_OFST 16
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_BUFF_MODE_LBN 0
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_BUFF_MODE_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_IP_CSUM_DIS_LBN 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_IP_CSUM_DIS_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_TCP_CSUM_DIS_LBN 2
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_TCP_CSUM_DIS_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_TCP_UDP_ONLY_LBN 3
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_TCP_UDP_ONLY_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_CRC_MODE_LBN 4
#define        MC_CMD_INIT_TXQ_EXT_IN_CRC_MODE_WIDTH 4
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_TIMESTAMP_LBN 8
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_TIMESTAMP_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_PACER_BYPASS_LBN 9
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_PACER_BYPASS_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_INNER_IP_CSUM_EN_LBN 10
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_INNER_IP_CSUM_EN_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_INNER_TCP_CSUM_EN_LBN 11
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_INNER_TCP_CSUM_EN_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_TSOV2_EN_LBN 12
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_TSOV2_EN_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_CTPIO_LBN 13
#define        MC_CMD_INIT_TXQ_EXT_IN_FLAG_CTPIO_WIDTH 1
/* Owner ID to use if in buffer mode (zero if physical) */
#define       MC_CMD_INIT_TXQ_EXT_IN_OWNER_ID_OFST 20
/* The port ID associated with the v-adaptor which should contain this DMAQ. */
#define       MC_CMD_INIT_TXQ_EXT_IN_PORT_ID_OFST 24
/* 64-bit address of 4k of 4k-aligned host memory buffer */
#define       MC_CMD_INIT_TXQ_EXT_IN_DMA_ADDR_OFST 28
#define       MC_CMD_INIT_TXQ_EXT_IN_DMA_ADDR_LEN 8
#define       MC_CMD_INIT_TXQ_EXT_IN_DMA_ADDR_LO_OFST 28
#define       MC_CMD_INIT_TXQ_EXT_IN_DMA_ADDR_HI_OFST 32
#define       MC_CMD_INIT_TXQ_EXT_IN_DMA_ADDR_MINNUM 1
#define       MC_CMD_INIT_TXQ_EXT_IN_DMA_ADDR_MAXNUM 64
/* Flags related to Qbb flow control mode. */
#define       MC_CMD_INIT_TXQ_EXT_IN_QBB_FLAGS_OFST 540
#define        MC_CMD_INIT_TXQ_EXT_IN_QBB_ENABLE_LBN 0
#define        MC_CMD_INIT_TXQ_EXT_IN_QBB_ENABLE_WIDTH 1
#define        MC_CMD_INIT_TXQ_EXT_IN_QBB_PRIORITY_LBN 1
#define        MC_CMD_INIT_TXQ_EXT_IN_QBB_PRIORITY_WIDTH 3

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

#define MC_CMD_0x83_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x84_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x85_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x86_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x5b_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_PROXY_CMD_IN msgrequest */
#define    MC_CMD_PROXY_CMD_IN_LEN 4
/* The handle of the target function. */
#define       MC_CMD_PROXY_CMD_IN_TARGET_OFST 0
#define        MC_CMD_PROXY_CMD_IN_TARGET_PF_LBN 0
#define        MC_CMD_PROXY_CMD_IN_TARGET_PF_WIDTH 16
#define        MC_CMD_PROXY_CMD_IN_TARGET_VF_LBN 16
#define        MC_CMD_PROXY_CMD_IN_TARGET_VF_WIDTH 16
#define          MC_CMD_PROXY_CMD_IN_VF_NULL  0xffff /* enum */

/* MC_CMD_PROXY_CMD_OUT msgresponse */
#define    MC_CMD_PROXY_CMD_OUT_LEN 0

/* MC_PROXY_STATUS_BUFFER structuredef: Host memory status buffer used to
 * manage proxied requests
 */
#define    MC_PROXY_STATUS_BUFFER_LEN 16
/* Handle allocated by the firmware for this proxy transaction */
#define       MC_PROXY_STATUS_BUFFER_HANDLE_OFST 0
/* enum: An invalid handle. */
#define          MC_PROXY_STATUS_BUFFER_HANDLE_INVALID  0x0
#define       MC_PROXY_STATUS_BUFFER_HANDLE_LBN 0
#define       MC_PROXY_STATUS_BUFFER_HANDLE_WIDTH 32
/* The requesting physical function number */
#define       MC_PROXY_STATUS_BUFFER_PF_OFST 4
#define       MC_PROXY_STATUS_BUFFER_PF_LEN 2
#define       MC_PROXY_STATUS_BUFFER_PF_LBN 32
#define       MC_PROXY_STATUS_BUFFER_PF_WIDTH 16
/* The requesting virtual function number. Set to VF_NULL if the target is a
 * PF.
 */
#define       MC_PROXY_STATUS_BUFFER_VF_OFST 6
#define       MC_PROXY_STATUS_BUFFER_VF_LEN 2
#define       MC_PROXY_STATUS_BUFFER_VF_LBN 48
#define       MC_PROXY_STATUS_BUFFER_VF_WIDTH 16
/* The target function RID. */
#define       MC_PROXY_STATUS_BUFFER_RID_OFST 8
#define       MC_PROXY_STATUS_BUFFER_RID_LEN 2
#define       MC_PROXY_STATUS_BUFFER_RID_LBN 64
#define       MC_PROXY_STATUS_BUFFER_RID_WIDTH 16
/* The status of the proxy as described in MC_CMD_PROXY_COMPLETE. */
#define       MC_PROXY_STATUS_BUFFER_STATUS_OFST 10
#define       MC_PROXY_STATUS_BUFFER_STATUS_LEN 2
#define       MC_PROXY_STATUS_BUFFER_STATUS_LBN 80
#define       MC_PROXY_STATUS_BUFFER_STATUS_WIDTH 16
/* If a request is authorized rather than carried out by the host, this is the
 * elevated privilege mask granted to the requesting function.
 */
#define       MC_PROXY_STATUS_BUFFER_GRANTED_PRIVILEGES_OFST 12
#define       MC_PROXY_STATUS_BUFFER_GRANTED_PRIVILEGES_LBN 96
#define       MC_PROXY_STATUS_BUFFER_GRANTED_PRIVILEGES_WIDTH 32


/***********************************/
/* MC_CMD_PROXY_CONFIGURE
 * Enable/disable authorization of MCDI requests from unprivileged functions by
 * a designated admin function
 */
#define MC_CMD_PROXY_CONFIGURE 0x58

#define MC_CMD_0x58_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_PROXY_CONFIGURE_IN msgrequest */
#define    MC_CMD_PROXY_CONFIGURE_IN_LEN 108
#define       MC_CMD_PROXY_CONFIGURE_IN_FLAGS_OFST 0
#define        MC_CMD_PROXY_CONFIGURE_IN_ENABLE_LBN 0
#define        MC_CMD_PROXY_CONFIGURE_IN_ENABLE_WIDTH 1
/* Host provides a contiguous memory buffer that contains at least NUM_BLOCKS
 * of blocks, each of the size REQUEST_BLOCK_SIZE.
 */
#define       MC_CMD_PROXY_CONFIGURE_IN_STATUS_BUFF_ADDR_OFST 4
#define       MC_CMD_PROXY_CONFIGURE_IN_STATUS_BUFF_ADDR_LEN 8
#define       MC_CMD_PROXY_CONFIGURE_IN_STATUS_BUFF_ADDR_LO_OFST 4
#define       MC_CMD_PROXY_CONFIGURE_IN_STATUS_BUFF_ADDR_HI_OFST 8
/* Must be a power of 2 */
#define       MC_CMD_PROXY_CONFIGURE_IN_STATUS_BLOCK_SIZE_OFST 12
/* Host provides a contiguous memory buffer that contains at least NUM_BLOCKS
 * of blocks, each of the size REPLY_BLOCK_SIZE.
 */
#define       MC_CMD_PROXY_CONFIGURE_IN_REQUEST_BUFF_ADDR_OFST 16
#define       MC_CMD_PROXY_CONFIGURE_IN_REQUEST_BUFF_ADDR_LEN 8
#define       MC_CMD_PROXY_CONFIGURE_IN_REQUEST_BUFF_ADDR_LO_OFST 16
#define       MC_CMD_PROXY_CONFIGURE_IN_REQUEST_BUFF_ADDR_HI_OFST 20
/* Must be a power of 2 */
#define       MC_CMD_PROXY_CONFIGURE_IN_REQUEST_BLOCK_SIZE_OFST 24
/* Host provides a contiguous memory buffer that contains at least NUM_BLOCKS
 * of blocks, each of the size STATUS_BLOCK_SIZE. This buffer is only needed if
 * host intends to complete proxied operations by using MC_CMD_PROXY_CMD.
 */
#define       MC_CMD_PROXY_CONFIGURE_IN_REPLY_BUFF_ADDR_OFST 28
#define       MC_CMD_PROXY_CONFIGURE_IN_REPLY_BUFF_ADDR_LEN 8
#define       MC_CMD_PROXY_CONFIGURE_IN_REPLY_BUFF_ADDR_LO_OFST 28
#define       MC_CMD_PROXY_CONFIGURE_IN_REPLY_BUFF_ADDR_HI_OFST 32
/* Must be a power of 2, or zero if this buffer is not provided */
#define       MC_CMD_PROXY_CONFIGURE_IN_REPLY_BLOCK_SIZE_OFST 36
/* Applies to all three buffers */
#define       MC_CMD_PROXY_CONFIGURE_IN_NUM_BLOCKS_OFST 40
/* A bit mask defining which MCDI operations may be proxied */
#define       MC_CMD_PROXY_CONFIGURE_IN_ALLOWED_MCDI_MASK_OFST 44
#define       MC_CMD_PROXY_CONFIGURE_IN_ALLOWED_MCDI_MASK_LEN 64

/* MC_CMD_PROXY_CONFIGURE_EXT_IN msgrequest */
#define    MC_CMD_PROXY_CONFIGURE_EXT_IN_LEN 112
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_FLAGS_OFST 0
#define        MC_CMD_PROXY_CONFIGURE_EXT_IN_ENABLE_LBN 0
#define        MC_CMD_PROXY_CONFIGURE_EXT_IN_ENABLE_WIDTH 1
/* Host provides a contiguous memory buffer that contains at least NUM_BLOCKS
 * of blocks, each of the size REQUEST_BLOCK_SIZE.
 */
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_STATUS_BUFF_ADDR_OFST 4
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_STATUS_BUFF_ADDR_LEN 8
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_STATUS_BUFF_ADDR_LO_OFST 4
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_STATUS_BUFF_ADDR_HI_OFST 8
/* Must be a power of 2 */
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_STATUS_BLOCK_SIZE_OFST 12
/* Host provides a contiguous memory buffer that contains at least NUM_BLOCKS
 * of blocks, each of the size REPLY_BLOCK_SIZE.
 */
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REQUEST_BUFF_ADDR_OFST 16
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REQUEST_BUFF_ADDR_LEN 8
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REQUEST_BUFF_ADDR_LO_OFST 16
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REQUEST_BUFF_ADDR_HI_OFST 20
/* Must be a power of 2 */
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REQUEST_BLOCK_SIZE_OFST 24
/* Host provides a contiguous memory buffer that contains at least NUM_BLOCKS
 * of blocks, each of the size STATUS_BLOCK_SIZE. This buffer is only needed if
 * host intends to complete proxied operations by using MC_CMD_PROXY_CMD.
 */
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REPLY_BUFF_ADDR_OFST 28
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REPLY_BUFF_ADDR_LEN 8
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REPLY_BUFF_ADDR_LO_OFST 28
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REPLY_BUFF_ADDR_HI_OFST 32
/* Must be a power of 2, or zero if this buffer is not provided */
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_REPLY_BLOCK_SIZE_OFST 36
/* Applies to all three buffers */
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_NUM_BLOCKS_OFST 40
/* A bit mask defining which MCDI operations may be proxied */
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_ALLOWED_MCDI_MASK_OFST 44
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_ALLOWED_MCDI_MASK_LEN 64
#define       MC_CMD_PROXY_CONFIGURE_EXT_IN_RESERVED_OFST 108

/* MC_CMD_PROXY_CONFIGURE_OUT msgresponse */
#define    MC_CMD_PROXY_CONFIGURE_OUT_LEN 0


/***********************************/
/* MC_CMD_PROXY_COMPLETE
 * Tells FW that a requested proxy operation has either been completed (by
 * using MC_CMD_PROXY_CMD) or authorized/declined. May only be sent by the
 * function that enabled proxying/authorization (by using
 * MC_CMD_PROXY_CONFIGURE).
 */
#define MC_CMD_PROXY_COMPLETE 0x5f

#define MC_CMD_0x5f_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_PROXY_COMPLETE_IN msgrequest */
#define    MC_CMD_PROXY_COMPLETE_IN_LEN 12
#define       MC_CMD_PROXY_COMPLETE_IN_BLOCK_INDEX_OFST 0
#define       MC_CMD_PROXY_COMPLETE_IN_STATUS_OFST 4
/* enum: The operation has been completed by using MC_CMD_PROXY_CMD, the reply
 * is stored in the REPLY_BUFF.
 */
#define          MC_CMD_PROXY_COMPLETE_IN_COMPLETE 0x0
/* enum: The operation has been authorized. The originating function may now
 * try again.
 */
#define          MC_CMD_PROXY_COMPLETE_IN_AUTHORIZED 0x1
/* enum: The operation has been declined. */
#define          MC_CMD_PROXY_COMPLETE_IN_DECLINED 0x2
/* enum: The authorization failed because the relevant application did not
 * respond in time.
 */
#define          MC_CMD_PROXY_COMPLETE_IN_TIMEDOUT 0x3
#define       MC_CMD_PROXY_COMPLETE_IN_HANDLE_OFST 8

/* MC_CMD_PROXY_COMPLETE_OUT msgresponse */
#define    MC_CMD_PROXY_COMPLETE_OUT_LEN 0


/***********************************/
/* MC_CMD_ALLOC_BUFTBL_CHUNK
 * Allocate a set of buffer table entries using the specified owner ID. This
 * operation allocates the required buffer table entries (and fails if it
 * cannot do so). The buffer table entries will initially be zeroed.
 */
#define MC_CMD_ALLOC_BUFTBL_CHUNK 0x87

#define MC_CMD_0x87_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

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

#define MC_CMD_0x88_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

/* MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN msgrequest */
#define    MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_LENMIN 20
#define    MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_LENMAX 268
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
#define       MC_CMD_PROGRAM_BUFTBL_ENTRIES_IN_ENTRY_MAXNUM 32

/* MC_CMD_PROGRAM_BUFTBL_ENTRIES_OUT msgresponse */
#define    MC_CMD_PROGRAM_BUFTBL_ENTRIES_OUT_LEN 0


/***********************************/
/* MC_CMD_FREE_BUFTBL_CHUNK
 */
#define MC_CMD_FREE_BUFTBL_CHUNK 0x89

#define MC_CMD_0x89_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

/* MC_CMD_FREE_BUFTBL_CHUNK_IN msgrequest */
#define    MC_CMD_FREE_BUFTBL_CHUNK_IN_LEN 4
#define       MC_CMD_FREE_BUFTBL_CHUNK_IN_HANDLE_OFST 0

/* MC_CMD_FREE_BUFTBL_CHUNK_OUT msgresponse */
#define    MC_CMD_FREE_BUFTBL_CHUNK_OUT_LEN 0

/* PORT_CONFIG_ENTRY structuredef */
#define    PORT_CONFIG_ENTRY_LEN 16
/* External port number (label) */
#define       PORT_CONFIG_ENTRY_EXT_NUMBER_OFST 0
#define       PORT_CONFIG_ENTRY_EXT_NUMBER_LEN 1
#define       PORT_CONFIG_ENTRY_EXT_NUMBER_LBN 0
#define       PORT_CONFIG_ENTRY_EXT_NUMBER_WIDTH 8
/* Port core location */
#define       PORT_CONFIG_ENTRY_CORE_OFST 1
#define       PORT_CONFIG_ENTRY_CORE_LEN 1
#define          PORT_CONFIG_ENTRY_STANDALONE  0x0 /* enum */
#define          PORT_CONFIG_ENTRY_MASTER  0x1 /* enum */
#define          PORT_CONFIG_ENTRY_SLAVE  0x2 /* enum */
#define       PORT_CONFIG_ENTRY_CORE_LBN 8
#define       PORT_CONFIG_ENTRY_CORE_WIDTH 8
/* Internal number (HW resource) relative to the core */
#define       PORT_CONFIG_ENTRY_INT_NUMBER_OFST 2
#define       PORT_CONFIG_ENTRY_INT_NUMBER_LEN 1
#define       PORT_CONFIG_ENTRY_INT_NUMBER_LBN 16
#define       PORT_CONFIG_ENTRY_INT_NUMBER_WIDTH 8
/* Reserved */
#define       PORT_CONFIG_ENTRY_RSVD_OFST 3
#define       PORT_CONFIG_ENTRY_RSVD_LEN 1
#define       PORT_CONFIG_ENTRY_RSVD_LBN 24
#define       PORT_CONFIG_ENTRY_RSVD_WIDTH 8
/* Bitmask of KR lanes used by the port */
#define       PORT_CONFIG_ENTRY_LANES_OFST 4
#define       PORT_CONFIG_ENTRY_LANES_LBN 32
#define       PORT_CONFIG_ENTRY_LANES_WIDTH 32
/* Port capabilities (MC_CMD_PHY_CAP_*) */
#define       PORT_CONFIG_ENTRY_SUPPORTED_CAPS_OFST 8
#define       PORT_CONFIG_ENTRY_SUPPORTED_CAPS_LBN 64
#define       PORT_CONFIG_ENTRY_SUPPORTED_CAPS_WIDTH 32
/* Reserved (align to 16 bytes) */
#define       PORT_CONFIG_ENTRY_RSVD2_OFST 12
#define       PORT_CONFIG_ENTRY_RSVD2_LBN 96
#define       PORT_CONFIG_ENTRY_RSVD2_WIDTH 32


/***********************************/
/* MC_CMD_FILTER_OP
 * Multiplexed MCDI call for filter operations
 */
#define MC_CMD_FILTER_OP 0x8a

#define MC_CMD_0x8a_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* enum: loop back to TXDP 0 */
#define          MC_CMD_FILTER_OP_IN_RX_DEST_TX0  0x3
/* enum: loop back to TXDP 1 */
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
 * MC_CMD_DOT1P_MAPPING_ALLOC.
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

/* MC_CMD_FILTER_OP_EXT_IN msgrequest: Extension to MC_CMD_FILTER_OP_IN to
 * include handling of VXLAN/NVGRE encapsulated frame filtering (which is
 * supported on Medford only).
 */
#define    MC_CMD_FILTER_OP_EXT_IN_LEN 172
/* identifies the type of operation requested */
#define       MC_CMD_FILTER_OP_EXT_IN_OP_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_FILTER_OP_IN/OP */
/* filter handle (for remove / unsubscribe operations) */
#define       MC_CMD_FILTER_OP_EXT_IN_HANDLE_OFST 4
#define       MC_CMD_FILTER_OP_EXT_IN_HANDLE_LEN 8
#define       MC_CMD_FILTER_OP_EXT_IN_HANDLE_LO_OFST 4
#define       MC_CMD_FILTER_OP_EXT_IN_HANDLE_HI_OFST 8
/* The port ID associated with the v-adaptor which should contain this filter.
 */
#define       MC_CMD_FILTER_OP_EXT_IN_PORT_ID_OFST 12
/* fields to include in match criteria */
#define       MC_CMD_FILTER_OP_EXT_IN_MATCH_FIELDS_OFST 16
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_SRC_IP_LBN 0
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_SRC_IP_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_DST_IP_LBN 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_DST_IP_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_SRC_MAC_LBN 2
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_SRC_MAC_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_SRC_PORT_LBN 3
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_SRC_PORT_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_DST_MAC_LBN 4
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_DST_MAC_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_DST_PORT_LBN 5
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_DST_PORT_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_ETHER_TYPE_LBN 6
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_ETHER_TYPE_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_INNER_VLAN_LBN 7
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_INNER_VLAN_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_OUTER_VLAN_LBN 8
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_OUTER_VLAN_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IP_PROTO_LBN 9
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IP_PROTO_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_FWDEF0_LBN 10
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_FWDEF0_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_VNI_OR_VSID_LBN 11
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_VNI_OR_VSID_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_SRC_IP_LBN 12
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_SRC_IP_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_DST_IP_LBN 13
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_DST_IP_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_SRC_MAC_LBN 14
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_SRC_MAC_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_SRC_PORT_LBN 15
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_SRC_PORT_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_DST_MAC_LBN 16
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_DST_MAC_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_DST_PORT_LBN 17
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_DST_PORT_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_ETHER_TYPE_LBN 18
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_ETHER_TYPE_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_INNER_VLAN_LBN 19
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_INNER_VLAN_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_OUTER_VLAN_LBN 20
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_OUTER_VLAN_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_IP_PROTO_LBN 21
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_IP_PROTO_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_FWDEF0_LBN 22
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_FWDEF0_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_FWDEF1_LBN 23
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_FWDEF1_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_UNKNOWN_MCAST_DST_LBN 24
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_UNKNOWN_MCAST_DST_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_UNKNOWN_UCAST_DST_LBN 25
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_IFRM_UNKNOWN_UCAST_DST_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_UNKNOWN_MCAST_DST_LBN 30
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_UNKNOWN_MCAST_DST_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_UNKNOWN_UCAST_DST_LBN 31
#define        MC_CMD_FILTER_OP_EXT_IN_MATCH_UNKNOWN_UCAST_DST_WIDTH 1
/* receive destination */
#define       MC_CMD_FILTER_OP_EXT_IN_RX_DEST_OFST 20
/* enum: drop packets */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_DEST_DROP  0x0
/* enum: receive to host */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_DEST_HOST  0x1
/* enum: receive to MC */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_DEST_MC  0x2
/* enum: loop back to TXDP 0 */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_DEST_TX0  0x3
/* enum: loop back to TXDP 1 */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_DEST_TX1  0x4
/* receive queue handle (for multiple queue modes, this is the base queue) */
#define       MC_CMD_FILTER_OP_EXT_IN_RX_QUEUE_OFST 24
/* receive mode */
#define       MC_CMD_FILTER_OP_EXT_IN_RX_MODE_OFST 28
/* enum: receive to just the specified queue */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_MODE_SIMPLE  0x0
/* enum: receive to multiple queues using RSS context */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_MODE_RSS  0x1
/* enum: receive to multiple queues using .1p mapping */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_MODE_DOT1P_MAPPING  0x2
/* enum: install a filter entry that will never match; for test purposes only
 */
#define          MC_CMD_FILTER_OP_EXT_IN_RX_MODE_TEST_NEVER_MATCH  0x80000000
/* RSS context (for RX_MODE_RSS) or .1p mapping handle (for
 * RX_MODE_DOT1P_MAPPING), as returned by MC_CMD_RSS_CONTEXT_ALLOC or
 * MC_CMD_DOT1P_MAPPING_ALLOC.
 */
#define       MC_CMD_FILTER_OP_EXT_IN_RX_CONTEXT_OFST 32
/* transmit domain (reserved; set to 0) */
#define       MC_CMD_FILTER_OP_EXT_IN_TX_DOMAIN_OFST 36
/* transmit destination (either set the MAC and/or PM bits for explicit
 * control, or set this field to TX_DEST_DEFAULT for sensible default
 * behaviour)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_TX_DEST_OFST 40
/* enum: request default behaviour (based on filter type) */
#define          MC_CMD_FILTER_OP_EXT_IN_TX_DEST_DEFAULT  0xffffffff
#define        MC_CMD_FILTER_OP_EXT_IN_TX_DEST_MAC_LBN 0
#define        MC_CMD_FILTER_OP_EXT_IN_TX_DEST_MAC_WIDTH 1
#define        MC_CMD_FILTER_OP_EXT_IN_TX_DEST_PM_LBN 1
#define        MC_CMD_FILTER_OP_EXT_IN_TX_DEST_PM_WIDTH 1
/* source MAC address to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_EXT_IN_SRC_MAC_OFST 44
#define       MC_CMD_FILTER_OP_EXT_IN_SRC_MAC_LEN 6
/* source port to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_EXT_IN_SRC_PORT_OFST 50
#define       MC_CMD_FILTER_OP_EXT_IN_SRC_PORT_LEN 2
/* destination MAC address to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_EXT_IN_DST_MAC_OFST 52
#define       MC_CMD_FILTER_OP_EXT_IN_DST_MAC_LEN 6
/* destination port to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_EXT_IN_DST_PORT_OFST 58
#define       MC_CMD_FILTER_OP_EXT_IN_DST_PORT_LEN 2
/* Ethernet type to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_EXT_IN_ETHER_TYPE_OFST 60
#define       MC_CMD_FILTER_OP_EXT_IN_ETHER_TYPE_LEN 2
/* Inner VLAN tag to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_EXT_IN_INNER_VLAN_OFST 62
#define       MC_CMD_FILTER_OP_EXT_IN_INNER_VLAN_LEN 2
/* Outer VLAN tag to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_EXT_IN_OUTER_VLAN_OFST 64
#define       MC_CMD_FILTER_OP_EXT_IN_OUTER_VLAN_LEN 2
/* IP protocol to match (in low byte; set high byte to 0) */
#define       MC_CMD_FILTER_OP_EXT_IN_IP_PROTO_OFST 66
#define       MC_CMD_FILTER_OP_EXT_IN_IP_PROTO_LEN 2
/* Firmware defined register 0 to match (reserved; set to 0) */
#define       MC_CMD_FILTER_OP_EXT_IN_FWDEF0_OFST 68
/* VNI (for VXLAN/Geneve, when IP protocol is UDP) or VSID (for NVGRE, when IP
 * protocol is GRE) to match (as bytes in network order; set last byte to 0 for
 * VXLAN/NVGRE, or 1 for Geneve)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_VNI_OR_VSID_OFST 72
#define        MC_CMD_FILTER_OP_EXT_IN_VNI_VALUE_LBN 0
#define        MC_CMD_FILTER_OP_EXT_IN_VNI_VALUE_WIDTH 24
#define        MC_CMD_FILTER_OP_EXT_IN_VNI_TYPE_LBN 24
#define        MC_CMD_FILTER_OP_EXT_IN_VNI_TYPE_WIDTH 8
/* enum: Match VXLAN traffic with this VNI */
#define          MC_CMD_FILTER_OP_EXT_IN_VNI_TYPE_VXLAN  0x0
/* enum: Match Geneve traffic with this VNI */
#define          MC_CMD_FILTER_OP_EXT_IN_VNI_TYPE_GENEVE  0x1
/* enum: Reserved for experimental development use */
#define          MC_CMD_FILTER_OP_EXT_IN_VNI_TYPE_EXPERIMENTAL  0xfe
#define        MC_CMD_FILTER_OP_EXT_IN_VSID_VALUE_LBN 0
#define        MC_CMD_FILTER_OP_EXT_IN_VSID_VALUE_WIDTH 24
#define        MC_CMD_FILTER_OP_EXT_IN_VSID_TYPE_LBN 24
#define        MC_CMD_FILTER_OP_EXT_IN_VSID_TYPE_WIDTH 8
/* enum: Match NVGRE traffic with this VSID */
#define          MC_CMD_FILTER_OP_EXT_IN_VSID_TYPE_NVGRE  0x0
/* source IP address to match (as bytes in network order; set last 12 bytes to
 * 0 for IPv4 address)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_SRC_IP_OFST 76
#define       MC_CMD_FILTER_OP_EXT_IN_SRC_IP_LEN 16
/* destination IP address to match (as bytes in network order; set last 12
 * bytes to 0 for IPv4 address)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_DST_IP_OFST 92
#define       MC_CMD_FILTER_OP_EXT_IN_DST_IP_LEN 16
/* VXLAN/NVGRE inner frame source MAC address to match (as bytes in network
 * order)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_SRC_MAC_OFST 108
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_SRC_MAC_LEN 6
/* VXLAN/NVGRE inner frame source port to match (as bytes in network order) */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_SRC_PORT_OFST 114
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_SRC_PORT_LEN 2
/* VXLAN/NVGRE inner frame destination MAC address to match (as bytes in
 * network order)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_DST_MAC_OFST 116
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_DST_MAC_LEN 6
/* VXLAN/NVGRE inner frame destination port to match (as bytes in network
 * order)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_DST_PORT_OFST 122
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_DST_PORT_LEN 2
/* VXLAN/NVGRE inner frame Ethernet type to match (as bytes in network order)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_ETHER_TYPE_OFST 124
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_ETHER_TYPE_LEN 2
/* VXLAN/NVGRE inner frame Inner VLAN tag to match (as bytes in network order)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_INNER_VLAN_OFST 126
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_INNER_VLAN_LEN 2
/* VXLAN/NVGRE inner frame Outer VLAN tag to match (as bytes in network order)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_OUTER_VLAN_OFST 128
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_OUTER_VLAN_LEN 2
/* VXLAN/NVGRE inner frame IP protocol to match (in low byte; set high byte to
 * 0)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_IP_PROTO_OFST 130
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_IP_PROTO_LEN 2
/* VXLAN/NVGRE inner frame Firmware defined register 0 to match (reserved; set
 * to 0)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_FWDEF0_OFST 132
/* VXLAN/NVGRE inner frame Firmware defined register 1 to match (reserved; set
 * to 0)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_FWDEF1_OFST 136
/* VXLAN/NVGRE inner frame source IP address to match (as bytes in network
 * order; set last 12 bytes to 0 for IPv4 address)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_SRC_IP_OFST 140
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_SRC_IP_LEN 16
/* VXLAN/NVGRE inner frame destination IP address to match (as bytes in network
 * order; set last 12 bytes to 0 for IPv4 address)
 */
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_DST_IP_OFST 156
#define       MC_CMD_FILTER_OP_EXT_IN_IFRM_DST_IP_LEN 16

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
/* enum: guaranteed invalid filter handle (low 32 bits) */
#define          MC_CMD_FILTER_OP_OUT_HANDLE_LO_INVALID  0xffffffff
/* enum: guaranteed invalid filter handle (high 32 bits) */
#define          MC_CMD_FILTER_OP_OUT_HANDLE_HI_INVALID  0xffffffff

/* MC_CMD_FILTER_OP_EXT_OUT msgresponse */
#define    MC_CMD_FILTER_OP_EXT_OUT_LEN 12
/* identifies the type of operation requested */
#define       MC_CMD_FILTER_OP_EXT_OUT_OP_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_FILTER_OP_EXT_IN/OP */
/* Returned filter handle (for insert / subscribe operations). Note that these
 * handles should be considered opaque to the host, although a value of
 * 0xFFFFFFFF_FFFFFFFF is guaranteed never to be a valid handle.
 */
#define       MC_CMD_FILTER_OP_EXT_OUT_HANDLE_OFST 4
#define       MC_CMD_FILTER_OP_EXT_OUT_HANDLE_LEN 8
#define       MC_CMD_FILTER_OP_EXT_OUT_HANDLE_LO_OFST 4
#define       MC_CMD_FILTER_OP_EXT_OUT_HANDLE_HI_OFST 8
/*            Enum values, see field(s): */
/*               MC_CMD_FILTER_OP_OUT/HANDLE */


/***********************************/
/* MC_CMD_GET_PARSER_DISP_INFO
 * Get information related to the parser-dispatcher subsystem
 */
#define MC_CMD_GET_PARSER_DISP_INFO 0xe4

#define MC_CMD_0xe4_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_PARSER_DISP_INFO_IN msgrequest */
#define    MC_CMD_GET_PARSER_DISP_INFO_IN_LEN 4
/* identifies the type of operation requested */
#define       MC_CMD_GET_PARSER_DISP_INFO_IN_OP_OFST 0
/* enum: read the list of supported RX filter matches */
#define          MC_CMD_GET_PARSER_DISP_INFO_IN_OP_GET_SUPPORTED_RX_MATCHES  0x1
/* enum: read flags indicating restrictions on filter insertion for the calling
 * client
 */
#define          MC_CMD_GET_PARSER_DISP_INFO_IN_OP_GET_RESTRICTIONS  0x2
/* enum: read properties relating to security rules (Medford-only; for use by
 * SolarSecure apps, not directly by drivers. See SF-114946-SW.)
 */
#define          MC_CMD_GET_PARSER_DISP_INFO_IN_OP_GET_SECURITY_RULE_INFO  0x3
/* enum: read the list of supported RX filter matches for VXLAN/NVGRE
 * encapsulated frames, which follow a different match sequence to normal
 * frames (Medford only)
 */
#define          MC_CMD_GET_PARSER_DISP_INFO_IN_OP_GET_SUPPORTED_ENCAP_RX_MATCHES  0x4

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

/* MC_CMD_GET_PARSER_DISP_RESTRICTIONS_OUT msgresponse */
#define    MC_CMD_GET_PARSER_DISP_RESTRICTIONS_OUT_LEN 8
/* identifies the type of operation requested */
#define       MC_CMD_GET_PARSER_DISP_RESTRICTIONS_OUT_OP_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_GET_PARSER_DISP_INFO_IN/OP */
/* bitfield of filter insertion restrictions */
#define       MC_CMD_GET_PARSER_DISP_RESTRICTIONS_OUT_RESTRICTION_FLAGS_OFST 4
#define        MC_CMD_GET_PARSER_DISP_RESTRICTIONS_OUT_DST_IP_MCAST_ONLY_LBN 0
#define        MC_CMD_GET_PARSER_DISP_RESTRICTIONS_OUT_DST_IP_MCAST_ONLY_WIDTH 1


/***********************************/
/* MC_CMD_PARSER_DISP_RW
 * Direct read/write of parser-dispatcher state (DICPUs and LUE) for debugging.
 * Please note that this interface is only of use to debug tools which have
 * knowledge of firmware and hardware data structures; nothing here is intended
 * for use by normal driver code.
 */
#define MC_CMD_PARSER_DISP_RW 0xe5

#define MC_CMD_0xe5_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_PARSER_DISP_RW_IN msgrequest */
#define    MC_CMD_PARSER_DISP_RW_IN_LEN 32
/* identifies the target of the operation */
#define       MC_CMD_PARSER_DISP_RW_IN_TARGET_OFST 0
/* enum: RX dispatcher CPU */
#define          MC_CMD_PARSER_DISP_RW_IN_RX_DICPU  0x0
/* enum: TX dispatcher CPU */
#define          MC_CMD_PARSER_DISP_RW_IN_TX_DICPU  0x1
/* enum: Lookup engine (with original metadata format) */
#define          MC_CMD_PARSER_DISP_RW_IN_LUE  0x2
/* enum: Lookup engine (with requested metadata format) */
#define          MC_CMD_PARSER_DISP_RW_IN_LUE_VERSIONED_METADATA  0x3
/* enum: RX0 dispatcher CPU (alias for RX_DICPU; Medford has 2 RX DICPUs) */
#define          MC_CMD_PARSER_DISP_RW_IN_RX0_DICPU  0x0
/* enum: RX1 dispatcher CPU (only valid for Medford) */
#define          MC_CMD_PARSER_DISP_RW_IN_RX1_DICPU  0x4
/* enum: Miscellaneous other state (only valid for Medford) */
#define          MC_CMD_PARSER_DISP_RW_IN_MISC_STATE  0x5
/* identifies the type of operation requested */
#define       MC_CMD_PARSER_DISP_RW_IN_OP_OFST 4
/* enum: read a word of DICPU DMEM or a LUE entry */
#define          MC_CMD_PARSER_DISP_RW_IN_READ  0x0
/* enum: write a word of DICPU DMEM or a LUE entry */
#define          MC_CMD_PARSER_DISP_RW_IN_WRITE  0x1
/* enum: read-modify-write a word of DICPU DMEM (not valid for LUE) */
#define          MC_CMD_PARSER_DISP_RW_IN_RMW  0x2
/* data memory address (DICPU targets) or LUE index (LUE targets) */
#define       MC_CMD_PARSER_DISP_RW_IN_ADDRESS_OFST 8
/* selector (for MISC_STATE target) */
#define       MC_CMD_PARSER_DISP_RW_IN_SELECTOR_OFST 8
/* enum: Port to datapath mapping */
#define          MC_CMD_PARSER_DISP_RW_IN_PORT_DP_MAPPING  0x1
/* value to write (for DMEM writes) */
#define       MC_CMD_PARSER_DISP_RW_IN_DMEM_WRITE_VALUE_OFST 12
/* XOR value (for DMEM read-modify-writes: new = (old & mask) ^ value) */
#define       MC_CMD_PARSER_DISP_RW_IN_DMEM_RMW_XOR_VALUE_OFST 12
/* AND mask (for DMEM read-modify-writes: new = (old & mask) ^ value) */
#define       MC_CMD_PARSER_DISP_RW_IN_DMEM_RMW_AND_MASK_OFST 16
/* metadata format (for LUE reads using LUE_VERSIONED_METADATA) */
#define       MC_CMD_PARSER_DISP_RW_IN_LUE_READ_METADATA_VERSION_OFST 12
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
/* datapath(s) used for each port (for MISC_STATE PORT_DP_MAPPING selector) */
#define       MC_CMD_PARSER_DISP_RW_OUT_PORT_DP_MAPPING_OFST 0
#define       MC_CMD_PARSER_DISP_RW_OUT_PORT_DP_MAPPING_LEN 4
#define       MC_CMD_PARSER_DISP_RW_OUT_PORT_DP_MAPPING_NUM 4
#define          MC_CMD_PARSER_DISP_RW_OUT_DP0  0x1 /* enum */
#define          MC_CMD_PARSER_DISP_RW_OUT_DP1  0x2 /* enum */


/***********************************/
/* MC_CMD_GET_PF_COUNT
 * Get number of PFs on the device.
 */
#define MC_CMD_GET_PF_COUNT 0xb6

#define MC_CMD_0xb6_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xb8_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xb9_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x8b_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_ALLOC_VIS_IN msgrequest */
#define    MC_CMD_ALLOC_VIS_IN_LEN 8
/* The minimum number of VIs that is acceptable */
#define       MC_CMD_ALLOC_VIS_IN_MIN_VI_COUNT_OFST 0
/* The maximum number of VIs that would be useful */
#define       MC_CMD_ALLOC_VIS_IN_MAX_VI_COUNT_OFST 4

/* MC_CMD_ALLOC_VIS_OUT msgresponse: Huntington-compatible VI_ALLOC request.
 * Use extended version in new code.
 */
#define    MC_CMD_ALLOC_VIS_OUT_LEN 8
/* The number of VIs allocated on this function */
#define       MC_CMD_ALLOC_VIS_OUT_VI_COUNT_OFST 0
/* The base absolute VI number allocated to this function. Required to
 * correctly interpret wakeup events.
 */
#define       MC_CMD_ALLOC_VIS_OUT_VI_BASE_OFST 4

/* MC_CMD_ALLOC_VIS_EXT_OUT msgresponse */
#define    MC_CMD_ALLOC_VIS_EXT_OUT_LEN 12
/* The number of VIs allocated on this function */
#define       MC_CMD_ALLOC_VIS_EXT_OUT_VI_COUNT_OFST 0
/* The base absolute VI number allocated to this function. Required to
 * correctly interpret wakeup events.
 */
#define       MC_CMD_ALLOC_VIS_EXT_OUT_VI_BASE_OFST 4
/* Function's port vi_shift value (always 0 on Huntington) */
#define       MC_CMD_ALLOC_VIS_EXT_OUT_VI_SHIFT_OFST 8


/***********************************/
/* MC_CMD_FREE_VIS
 * Free VIs for current PCI function. Any linked PIO buffers will be unlinked,
 * but not freed.
 */
#define MC_CMD_FREE_VIS 0x8c

#define MC_CMD_0x8c_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_FREE_VIS_IN msgrequest */
#define    MC_CMD_FREE_VIS_IN_LEN 0

/* MC_CMD_FREE_VIS_OUT msgresponse */
#define    MC_CMD_FREE_VIS_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_SRIOV_CFG
 * Get SRIOV config for this PF.
 */
#define MC_CMD_GET_SRIOV_CFG 0xba

#define MC_CMD_0xba_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xbb_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x8d_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_VI_ALLOC_INFO_IN msgrequest */
#define    MC_CMD_GET_VI_ALLOC_INFO_IN_LEN 0

/* MC_CMD_GET_VI_ALLOC_INFO_OUT msgresponse */
#define    MC_CMD_GET_VI_ALLOC_INFO_OUT_LEN 12
/* The number of VIs allocated on this function */
#define       MC_CMD_GET_VI_ALLOC_INFO_OUT_VI_COUNT_OFST 0
/* The base absolute VI number allocated to this function. Required to
 * correctly interpret wakeup events.
 */
#define       MC_CMD_GET_VI_ALLOC_INFO_OUT_VI_BASE_OFST 4
/* Function's port vi_shift value (always 0 on Huntington) */
#define       MC_CMD_GET_VI_ALLOC_INFO_OUT_VI_SHIFT_OFST 8


/***********************************/
/* MC_CMD_DUMP_VI_STATE
 * For CmdClient use. Dump pertinent information on a specific absolute VI.
 */
#define MC_CMD_DUMP_VI_STATE 0x8e

#define MC_CMD_0x8e_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x8f_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

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

#define MC_CMD_0x90_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

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

#define MC_CMD_0xb0_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xb1_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xbc_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xbd_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x91_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xbe_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_CAPABILITIES_IN msgrequest */
#define    MC_CMD_GET_CAPABILITIES_IN_LEN 0

/* MC_CMD_GET_CAPABILITIES_OUT msgresponse */
#define    MC_CMD_GET_CAPABILITIES_OUT_LEN 20
/* First word of flags. */
#define       MC_CMD_GET_CAPABILITIES_OUT_FLAGS1_OFST 0
#define        MC_CMD_GET_CAPABILITIES_OUT_VPORT_RECONFIGURE_LBN 3
#define        MC_CMD_GET_CAPABILITIES_OUT_VPORT_RECONFIGURE_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_STRIPING_LBN 4
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_STRIPING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_VADAPTOR_QUERY_LBN 5
#define        MC_CMD_GET_CAPABILITIES_OUT_VADAPTOR_QUERY_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_EVB_PORT_VLAN_RESTRICT_LBN 6
#define        MC_CMD_GET_CAPABILITIES_OUT_EVB_PORT_VLAN_RESTRICT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_DRV_ATTACH_PREBOOT_LBN 7
#define        MC_CMD_GET_CAPABILITIES_OUT_DRV_ATTACH_PREBOOT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_FORCE_EVENT_MERGING_LBN 8
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_FORCE_EVENT_MERGING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_SET_MAC_ENHANCED_LBN 9
#define        MC_CMD_GET_CAPABILITIES_OUT_SET_MAC_ENHANCED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_UNKNOWN_UCAST_DST_FILTER_ALWAYS_MULTI_RECIPIENT_LBN 10
#define        MC_CMD_GET_CAPABILITIES_OUT_UNKNOWN_UCAST_DST_FILTER_ALWAYS_MULTI_RECIPIENT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_VADAPTOR_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED_LBN 11
#define        MC_CMD_GET_CAPABILITIES_OUT_VADAPTOR_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_MAC_SECURITY_FILTERING_LBN 12
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_MAC_SECURITY_FILTERING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_ADDITIONAL_RSS_MODES_LBN 13
#define        MC_CMD_GET_CAPABILITIES_OUT_ADDITIONAL_RSS_MODES_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_QBB_LBN 14
#define        MC_CMD_GET_CAPABILITIES_OUT_QBB_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_PACKED_STREAM_VAR_BUFFERS_LBN 15
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_PACKED_STREAM_VAR_BUFFERS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_RSS_LIMITED_LBN 16
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_RSS_LIMITED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_PACKED_STREAM_LBN 17
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_PACKED_STREAM_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_INCLUDE_FCS_LBN 18
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_INCLUDE_FCS_WIDTH 1
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
#define        MC_CMD_GET_CAPABILITIES_OUT_PM_AND_RXDP_COUNTERS_LBN 27
#define        MC_CMD_GET_CAPABILITIES_OUT_PM_AND_RXDP_COUNTERS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_DISABLE_SCATTER_LBN 28
#define        MC_CMD_GET_CAPABILITIES_OUT_RX_DISABLE_SCATTER_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_MCAST_UDP_LOOPBACK_LBN 29
#define        MC_CMD_GET_CAPABILITIES_OUT_TX_MCAST_UDP_LOOPBACK_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_EVB_LBN 30
#define        MC_CMD_GET_CAPABILITIES_OUT_EVB_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_OUT_VXLAN_NVGRE_LBN 31
#define        MC_CMD_GET_CAPABILITIES_OUT_VXLAN_NVGRE_WIDTH 1
/* RxDPCPU firmware id. */
#define       MC_CMD_GET_CAPABILITIES_OUT_RX_DPCPU_FW_ID_OFST 4
#define       MC_CMD_GET_CAPABILITIES_OUT_RX_DPCPU_FW_ID_LEN 2
/* enum: Standard RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP  0x0
/* enum: Low latency RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_LOW_LATENCY  0x1
/* enum: Packed stream RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_PACKED_STREAM  0x2
/* enum: BIST RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_BIST  0x10a
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
/* enum: RXDP Test firmware image 9 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXDP_TEST_FW_DOORBELL_DELAY  0x10b
/* TxDPCPU firmware id. */
#define       MC_CMD_GET_CAPABILITIES_OUT_TX_DPCPU_FW_ID_OFST 6
#define       MC_CMD_GET_CAPABILITIES_OUT_TX_DPCPU_FW_ID_LEN 2
/* enum: Standard TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP  0x0
/* enum: Low latency TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_LOW_LATENCY  0x1
/* enum: High packet rate TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_HIGH_PACKET_RATE  0x3
/* enum: BIST TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_BIST  0x12d
/* enum: TXDP Test firmware image 1 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_TEST_FW_TSO_EDIT  0x101
/* enum: TXDP Test firmware image 2 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_TEST_FW_PACKET_EDITS  0x102
/* enum: TXDP CSR bus test firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXDP_TEST_FW_CSR  0x103
#define       MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_OFST 8
#define       MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_LEN 2
#define        MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_REV_LBN 0
#define        MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_REV_WIDTH 12
#define        MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_TYPE_LBN 12
#define        MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_VERSION_TYPE_WIDTH 4
/* enum: reserved value - do not use (may indicate alternative interpretation
 * of REV field in future)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_RESERVED  0x0
/* enum: Trivial RX PD firmware for early Huntington development (Huntington
 * development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_FIRST_PKT  0x1
/* enum: RX PD firmware with approximately Siena-compatible behaviour
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_SIENA_COMPAT  0x2
/* enum: Virtual switching (full feature) RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_VSWITCH  0x3
/* enum: siena_compat variant RX PD firmware using PM rather than MAC
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_SIENA_COMPAT_PM  0x4
/* enum: Low latency RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_LOW_LATENCY  0x5
/* enum: Packed stream RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_PACKED_STREAM  0x6
/* enum: RX PD firmware handling layer 2 only for high packet rate performance
 * tests (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_LAYER2_PERF  0x7
/* enum: Rules engine RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_RULES_ENGINE  0x8
/* enum: RX PD firmware for GUE parsing prototype (Medford development only) */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_TESTFW_GUE_PROTOTYPE  0xe
/* enum: RX PD firmware parsing but not filtering network overlay tunnel
 * encapsulations (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_RXPD_FW_TYPE_TESTFW_ENCAP_PARSING_ONLY  0xf
#define       MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_OFST 10
#define       MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_LEN 2
#define        MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_REV_LBN 0
#define        MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_REV_WIDTH 12
#define        MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_TYPE_LBN 12
#define        MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_VERSION_TYPE_WIDTH 4
/* enum: reserved value - do not use (may indicate alternative interpretation
 * of REV field in future)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_RESERVED  0x0
/* enum: Trivial TX PD firmware for early Huntington development (Huntington
 * development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_FIRST_PKT  0x1
/* enum: TX PD firmware with approximately Siena-compatible behaviour
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_SIENA_COMPAT  0x2
/* enum: Virtual switching (full feature) TX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_VSWITCH  0x3
/* enum: siena_compat variant TX PD firmware using PM rather than MAC
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_SIENA_COMPAT_PM  0x4
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_LOW_LATENCY  0x5 /* enum */
/* enum: TX PD firmware handling layer 2 only for high packet rate performance
 * tests (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_LAYER2_PERF  0x7
/* enum: Rules engine TX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_RULES_ENGINE  0x8
/* enum: RX PD firmware for GUE parsing prototype (Medford development only) */
#define          MC_CMD_GET_CAPABILITIES_OUT_TXPD_FW_TYPE_TESTFW_GUE_PROTOTYPE  0xe
/* Hardware capabilities of NIC */
#define       MC_CMD_GET_CAPABILITIES_OUT_HW_CAPABILITIES_OFST 12
/* Licensed capabilities */
#define       MC_CMD_GET_CAPABILITIES_OUT_LICENSE_CAPABILITIES_OFST 16

/* MC_CMD_GET_CAPABILITIES_V2_IN msgrequest */
#define    MC_CMD_GET_CAPABILITIES_V2_IN_LEN 0

/* MC_CMD_GET_CAPABILITIES_V2_OUT msgresponse */
#define    MC_CMD_GET_CAPABILITIES_V2_OUT_LEN 72
/* First word of flags. */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_FLAGS1_OFST 0
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_VPORT_RECONFIGURE_LBN 3
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_VPORT_RECONFIGURE_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_STRIPING_LBN 4
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_STRIPING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_VADAPTOR_QUERY_LBN 5
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_VADAPTOR_QUERY_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_EVB_PORT_VLAN_RESTRICT_LBN 6
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_EVB_PORT_VLAN_RESTRICT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_DRV_ATTACH_PREBOOT_LBN 7
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_DRV_ATTACH_PREBOOT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_FORCE_EVENT_MERGING_LBN 8
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_FORCE_EVENT_MERGING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_SET_MAC_ENHANCED_LBN 9
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_SET_MAC_ENHANCED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_UNKNOWN_UCAST_DST_FILTER_ALWAYS_MULTI_RECIPIENT_LBN 10
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_UNKNOWN_UCAST_DST_FILTER_ALWAYS_MULTI_RECIPIENT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_VADAPTOR_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED_LBN 11
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_VADAPTOR_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_MAC_SECURITY_FILTERING_LBN 12
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_MAC_SECURITY_FILTERING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_ADDITIONAL_RSS_MODES_LBN 13
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_ADDITIONAL_RSS_MODES_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_QBB_LBN 14
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_QBB_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_PACKED_STREAM_VAR_BUFFERS_LBN 15
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_PACKED_STREAM_VAR_BUFFERS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_RSS_LIMITED_LBN 16
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_RSS_LIMITED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_PACKED_STREAM_LBN 17
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_PACKED_STREAM_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_INCLUDE_FCS_LBN 18
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_INCLUDE_FCS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_VLAN_INSERTION_LBN 19
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_VLAN_INSERTION_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_VLAN_STRIPPING_LBN 20
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_VLAN_STRIPPING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TSO_LBN 21
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TSO_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_PREFIX_LEN_0_LBN 22
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_PREFIX_LEN_0_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_PREFIX_LEN_14_LBN 23
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_PREFIX_LEN_14_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_TIMESTAMP_LBN 24
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_TIMESTAMP_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_BATCHING_LBN 25
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_BATCHING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_MCAST_FILTER_CHAINING_LBN 26
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_MCAST_FILTER_CHAINING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_PM_AND_RXDP_COUNTERS_LBN 27
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_PM_AND_RXDP_COUNTERS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_DISABLE_SCATTER_LBN 28
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_DISABLE_SCATTER_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_MCAST_UDP_LOOPBACK_LBN 29
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_MCAST_UDP_LOOPBACK_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_EVB_LBN 30
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_EVB_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_VXLAN_NVGRE_LBN 31
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_VXLAN_NVGRE_WIDTH 1
/* RxDPCPU firmware id. */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_RX_DPCPU_FW_ID_OFST 4
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_RX_DPCPU_FW_ID_LEN 2
/* enum: Standard RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP  0x0
/* enum: Low latency RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_LOW_LATENCY  0x1
/* enum: Packed stream RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_PACKED_STREAM  0x2
/* enum: BIST RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_BIST  0x10a
/* enum: RXDP Test firmware image 1 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_FW_TO_MC_CUT_THROUGH  0x101
/* enum: RXDP Test firmware image 2 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_FW_TO_MC_STORE_FORWARD  0x102
/* enum: RXDP Test firmware image 3 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_FW_TO_MC_STORE_FORWARD_FIRST  0x103
/* enum: RXDP Test firmware image 4 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_EVERY_EVENT_BATCHABLE  0x104
/* enum: RXDP Test firmware image 5 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_BACKPRESSURE  0x105
/* enum: RXDP Test firmware image 6 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_FW_PACKET_EDITS  0x106
/* enum: RXDP Test firmware image 7 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_FW_RX_HDR_SPLIT  0x107
/* enum: RXDP Test firmware image 8 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_FW_DISABLE_DL  0x108
/* enum: RXDP Test firmware image 9 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXDP_TEST_FW_DOORBELL_DELAY  0x10b
/* TxDPCPU firmware id. */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_TX_DPCPU_FW_ID_OFST 6
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_TX_DPCPU_FW_ID_LEN 2
/* enum: Standard TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXDP  0x0
/* enum: Low latency TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXDP_LOW_LATENCY  0x1
/* enum: High packet rate TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXDP_HIGH_PACKET_RATE  0x3
/* enum: BIST TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXDP_BIST  0x12d
/* enum: TXDP Test firmware image 1 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXDP_TEST_FW_TSO_EDIT  0x101
/* enum: TXDP Test firmware image 2 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXDP_TEST_FW_PACKET_EDITS  0x102
/* enum: TXDP CSR bus test firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXDP_TEST_FW_CSR  0x103
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_VERSION_OFST 8
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_VERSION_LEN 2
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_VERSION_REV_LBN 0
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_VERSION_REV_WIDTH 12
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_VERSION_TYPE_LBN 12
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_VERSION_TYPE_WIDTH 4
/* enum: reserved value - do not use (may indicate alternative interpretation
 * of REV field in future)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_RESERVED  0x0
/* enum: Trivial RX PD firmware for early Huntington development (Huntington
 * development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_FIRST_PKT  0x1
/* enum: RX PD firmware with approximately Siena-compatible behaviour
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_SIENA_COMPAT  0x2
/* enum: Virtual switching (full feature) RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_VSWITCH  0x3
/* enum: siena_compat variant RX PD firmware using PM rather than MAC
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_SIENA_COMPAT_PM  0x4
/* enum: Low latency RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_LOW_LATENCY  0x5
/* enum: Packed stream RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_PACKED_STREAM  0x6
/* enum: RX PD firmware handling layer 2 only for high packet rate performance
 * tests (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_LAYER2_PERF  0x7
/* enum: Rules engine RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_RULES_ENGINE  0x8
/* enum: RX PD firmware for GUE parsing prototype (Medford development only) */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_TESTFW_GUE_PROTOTYPE  0xe
/* enum: RX PD firmware parsing but not filtering network overlay tunnel
 * encapsulations (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_RXPD_FW_TYPE_TESTFW_ENCAP_PARSING_ONLY  0xf
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_VERSION_OFST 10
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_VERSION_LEN 2
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_VERSION_REV_LBN 0
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_VERSION_REV_WIDTH 12
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_VERSION_TYPE_LBN 12
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_VERSION_TYPE_WIDTH 4
/* enum: reserved value - do not use (may indicate alternative interpretation
 * of REV field in future)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_RESERVED  0x0
/* enum: Trivial TX PD firmware for early Huntington development (Huntington
 * development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_FIRST_PKT  0x1
/* enum: TX PD firmware with approximately Siena-compatible behaviour
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_SIENA_COMPAT  0x2
/* enum: Virtual switching (full feature) TX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_VSWITCH  0x3
/* enum: siena_compat variant TX PD firmware using PM rather than MAC
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_SIENA_COMPAT_PM  0x4
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_LOW_LATENCY  0x5 /* enum */
/* enum: TX PD firmware handling layer 2 only for high packet rate performance
 * tests (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_LAYER2_PERF  0x7
/* enum: Rules engine TX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_RULES_ENGINE  0x8
/* enum: RX PD firmware for GUE parsing prototype (Medford development only) */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_TXPD_FW_TYPE_TESTFW_GUE_PROTOTYPE  0xe
/* Hardware capabilities of NIC */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_HW_CAPABILITIES_OFST 12
/* Licensed capabilities */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_LICENSE_CAPABILITIES_OFST 16
/* Second word of flags. Not present on older firmware (check the length). */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_FLAGS2_OFST 20
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TSO_V2_LBN 0
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TSO_V2_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TSO_V2_ENCAP_LBN 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TSO_V2_ENCAP_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_EVQ_TIMER_CTRL_LBN 2
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_EVQ_TIMER_CTRL_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_EVENT_CUT_THROUGH_LBN 3
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_EVENT_CUT_THROUGH_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_CUT_THROUGH_LBN 4
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_CUT_THROUGH_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_VFIFO_ULL_MODE_LBN 5
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_VFIFO_ULL_MODE_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_MAC_STATS_40G_TX_SIZE_BINS_LBN 6
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_MAC_STATS_40G_TX_SIZE_BINS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_INIT_EVQ_V2_LBN 7
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_INIT_EVQ_V2_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_MAC_TIMESTAMPING_LBN 8
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_MAC_TIMESTAMPING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TIMESTAMP_LBN 9
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TIMESTAMP_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_SNIFF_LBN 10
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_RX_SNIFF_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_SNIFF_LBN 11
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_TX_SNIFF_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_NVRAM_UPDATE_REPORT_VERIFY_RESULT_LBN 12
#define        MC_CMD_GET_CAPABILITIES_V2_OUT_NVRAM_UPDATE_REPORT_VERIFY_RESULT_WIDTH 1
/* Number of FATSOv2 contexts per datapath supported by this NIC. Not present
 * on older firmware (check the length).
 */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TSO_V2_N_CONTEXTS_OFST 24
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_TX_TSO_V2_N_CONTEXTS_LEN 2
/* One byte per PF containing the number of the external port assigned to this
 * PF, indexed by PF number. Special values indicate that a PF is either not
 * present or not assigned.
 */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_PFS_TO_PORTS_ASSIGNMENT_OFST 26
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_PFS_TO_PORTS_ASSIGNMENT_LEN 1
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_PFS_TO_PORTS_ASSIGNMENT_NUM 16
/* enum: The caller is not permitted to access information on this PF. */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_ACCESS_NOT_PERMITTED  0xff
/* enum: PF does not exist. */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_PF_NOT_PRESENT  0xfe
/* enum: PF does exist but is not assigned to any external port. */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_PF_NOT_ASSIGNED  0xfd
/* enum: This value indicates that PF is assigned, but it cannot be expressed
 * in this field. It is intended for a possible future situation where a more
 * complex scheme of PFs to ports mapping is being used. The future driver
 * should look for a new field supporting the new scheme. The current/old
 * driver should treat this value as PF_NOT_ASSIGNED.
 */
#define          MC_CMD_GET_CAPABILITIES_V2_OUT_INCOMPATIBLE_ASSIGNMENT  0xfc
/* One byte per PF containing the number of its VFs, indexed by PF number. A
 * special value indicates that a PF is not present.
 */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_NUM_VFS_PER_PF_OFST 42
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_NUM_VFS_PER_PF_LEN 1
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_NUM_VFS_PER_PF_NUM 16
/* enum: The caller is not permitted to access information on this PF. */
/*               MC_CMD_GET_CAPABILITIES_V2_OUT_ACCESS_NOT_PERMITTED  0xff */
/* enum: PF does not exist. */
/*               MC_CMD_GET_CAPABILITIES_V2_OUT_PF_NOT_PRESENT  0xfe */
/* Number of VIs available for each external port */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_NUM_VIS_PER_PORT_OFST 58
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_NUM_VIS_PER_PORT_LEN 2
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_NUM_VIS_PER_PORT_NUM 4
/* Size of RX descriptor cache expressed as binary logarithm The actual size
 * equals (2 ^ RX_DESC_CACHE_SIZE)
 */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_RX_DESC_CACHE_SIZE_OFST 66
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_RX_DESC_CACHE_SIZE_LEN 1
/* Size of TX descriptor cache expressed as binary logarithm The actual size
 * equals (2 ^ TX_DESC_CACHE_SIZE)
 */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_TX_DESC_CACHE_SIZE_OFST 67
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_TX_DESC_CACHE_SIZE_LEN 1
/* Total number of available PIO buffers */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_NUM_PIO_BUFFS_OFST 68
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_NUM_PIO_BUFFS_LEN 2
/* Size of a single PIO buffer */
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_SIZE_PIO_BUFF_OFST 70
#define       MC_CMD_GET_CAPABILITIES_V2_OUT_SIZE_PIO_BUFF_LEN 2

/* MC_CMD_GET_CAPABILITIES_V3_OUT msgresponse */
#define    MC_CMD_GET_CAPABILITIES_V3_OUT_LEN 73
/* First word of flags. */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_FLAGS1_OFST 0
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_VPORT_RECONFIGURE_LBN 3
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_VPORT_RECONFIGURE_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_STRIPING_LBN 4
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_STRIPING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_VADAPTOR_QUERY_LBN 5
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_VADAPTOR_QUERY_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_EVB_PORT_VLAN_RESTRICT_LBN 6
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_EVB_PORT_VLAN_RESTRICT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_DRV_ATTACH_PREBOOT_LBN 7
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_DRV_ATTACH_PREBOOT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_FORCE_EVENT_MERGING_LBN 8
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_FORCE_EVENT_MERGING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_SET_MAC_ENHANCED_LBN 9
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_SET_MAC_ENHANCED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_UNKNOWN_UCAST_DST_FILTER_ALWAYS_MULTI_RECIPIENT_LBN 10
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_UNKNOWN_UCAST_DST_FILTER_ALWAYS_MULTI_RECIPIENT_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_VADAPTOR_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED_LBN 11
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_VADAPTOR_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_MAC_SECURITY_FILTERING_LBN 12
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_MAC_SECURITY_FILTERING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_ADDITIONAL_RSS_MODES_LBN 13
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_ADDITIONAL_RSS_MODES_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_QBB_LBN 14
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_QBB_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_PACKED_STREAM_VAR_BUFFERS_LBN 15
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_PACKED_STREAM_VAR_BUFFERS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_RSS_LIMITED_LBN 16
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_RSS_LIMITED_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_PACKED_STREAM_LBN 17
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_PACKED_STREAM_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_INCLUDE_FCS_LBN 18
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_INCLUDE_FCS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_VLAN_INSERTION_LBN 19
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_VLAN_INSERTION_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_VLAN_STRIPPING_LBN 20
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_VLAN_STRIPPING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TSO_LBN 21
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TSO_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_PREFIX_LEN_0_LBN 22
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_PREFIX_LEN_0_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_PREFIX_LEN_14_LBN 23
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_PREFIX_LEN_14_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_TIMESTAMP_LBN 24
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_TIMESTAMP_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_BATCHING_LBN 25
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_BATCHING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_MCAST_FILTER_CHAINING_LBN 26
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_MCAST_FILTER_CHAINING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_PM_AND_RXDP_COUNTERS_LBN 27
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_PM_AND_RXDP_COUNTERS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_DISABLE_SCATTER_LBN 28
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_DISABLE_SCATTER_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_MCAST_UDP_LOOPBACK_LBN 29
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_MCAST_UDP_LOOPBACK_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_EVB_LBN 30
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_EVB_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_VXLAN_NVGRE_LBN 31
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_VXLAN_NVGRE_WIDTH 1
/* RxDPCPU firmware id. */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_RX_DPCPU_FW_ID_OFST 4
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_RX_DPCPU_FW_ID_LEN 2
/* enum: Standard RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP  0x0
/* enum: Low latency RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_LOW_LATENCY  0x1
/* enum: Packed stream RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_PACKED_STREAM  0x2
/* enum: BIST RXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_BIST  0x10a
/* enum: RXDP Test firmware image 1 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_FW_TO_MC_CUT_THROUGH  0x101
/* enum: RXDP Test firmware image 2 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_FW_TO_MC_STORE_FORWARD  0x102
/* enum: RXDP Test firmware image 3 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_FW_TO_MC_STORE_FORWARD_FIRST  0x103
/* enum: RXDP Test firmware image 4 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_EVERY_EVENT_BATCHABLE  0x104
/* enum: RXDP Test firmware image 5 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_BACKPRESSURE  0x105
/* enum: RXDP Test firmware image 6 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_FW_PACKET_EDITS  0x106
/* enum: RXDP Test firmware image 7 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_FW_RX_HDR_SPLIT  0x107
/* enum: RXDP Test firmware image 8 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_FW_DISABLE_DL  0x108
/* enum: RXDP Test firmware image 9 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXDP_TEST_FW_DOORBELL_DELAY  0x10b
/* TxDPCPU firmware id. */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_TX_DPCPU_FW_ID_OFST 6
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_TX_DPCPU_FW_ID_LEN 2
/* enum: Standard TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXDP  0x0
/* enum: Low latency TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXDP_LOW_LATENCY  0x1
/* enum: High packet rate TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXDP_HIGH_PACKET_RATE  0x3
/* enum: BIST TXDP firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXDP_BIST  0x12d
/* enum: TXDP Test firmware image 1 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXDP_TEST_FW_TSO_EDIT  0x101
/* enum: TXDP Test firmware image 2 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXDP_TEST_FW_PACKET_EDITS  0x102
/* enum: TXDP CSR bus test firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXDP_TEST_FW_CSR  0x103
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_VERSION_OFST 8
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_VERSION_LEN 2
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_VERSION_REV_LBN 0
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_VERSION_REV_WIDTH 12
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_VERSION_TYPE_LBN 12
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_VERSION_TYPE_WIDTH 4
/* enum: reserved value - do not use (may indicate alternative interpretation
 * of REV field in future)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_RESERVED  0x0
/* enum: Trivial RX PD firmware for early Huntington development (Huntington
 * development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_FIRST_PKT  0x1
/* enum: RX PD firmware with approximately Siena-compatible behaviour
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_SIENA_COMPAT  0x2
/* enum: Virtual switching (full feature) RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_VSWITCH  0x3
/* enum: siena_compat variant RX PD firmware using PM rather than MAC
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_SIENA_COMPAT_PM  0x4
/* enum: Low latency RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_LOW_LATENCY  0x5
/* enum: Packed stream RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_PACKED_STREAM  0x6
/* enum: RX PD firmware handling layer 2 only for high packet rate performance
 * tests (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_LAYER2_PERF  0x7
/* enum: Rules engine RX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_RULES_ENGINE  0x8
/* enum: RX PD firmware for GUE parsing prototype (Medford development only) */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_TESTFW_GUE_PROTOTYPE  0xe
/* enum: RX PD firmware parsing but not filtering network overlay tunnel
 * encapsulations (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_RXPD_FW_TYPE_TESTFW_ENCAP_PARSING_ONLY  0xf
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_VERSION_OFST 10
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_VERSION_LEN 2
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_VERSION_REV_LBN 0
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_VERSION_REV_WIDTH 12
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_VERSION_TYPE_LBN 12
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_VERSION_TYPE_WIDTH 4
/* enum: reserved value - do not use (may indicate alternative interpretation
 * of REV field in future)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_RESERVED  0x0
/* enum: Trivial TX PD firmware for early Huntington development (Huntington
 * development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_FIRST_PKT  0x1
/* enum: TX PD firmware with approximately Siena-compatible behaviour
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_SIENA_COMPAT  0x2
/* enum: Virtual switching (full feature) TX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_VSWITCH  0x3
/* enum: siena_compat variant TX PD firmware using PM rather than MAC
 * (Huntington development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_SIENA_COMPAT_PM  0x4
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_LOW_LATENCY  0x5 /* enum */
/* enum: TX PD firmware handling layer 2 only for high packet rate performance
 * tests (Medford development only)
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_LAYER2_PERF  0x7
/* enum: Rules engine TX PD production firmware */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_RULES_ENGINE  0x8
/* enum: RX PD firmware for GUE parsing prototype (Medford development only) */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_TXPD_FW_TYPE_TESTFW_GUE_PROTOTYPE  0xe
/* Hardware capabilities of NIC */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_HW_CAPABILITIES_OFST 12
/* Licensed capabilities */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_LICENSE_CAPABILITIES_OFST 16
/* Second word of flags. Not present on older firmware (check the length). */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_FLAGS2_OFST 20
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TSO_V2_LBN 0
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TSO_V2_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TSO_V2_ENCAP_LBN 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TSO_V2_ENCAP_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_EVQ_TIMER_CTRL_LBN 2
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_EVQ_TIMER_CTRL_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_EVENT_CUT_THROUGH_LBN 3
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_EVENT_CUT_THROUGH_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_CUT_THROUGH_LBN 4
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_CUT_THROUGH_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_VFIFO_ULL_MODE_LBN 5
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_VFIFO_ULL_MODE_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_MAC_STATS_40G_TX_SIZE_BINS_LBN 6
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_MAC_STATS_40G_TX_SIZE_BINS_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_INIT_EVQ_V2_LBN 7
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_INIT_EVQ_V2_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_MAC_TIMESTAMPING_LBN 8
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_MAC_TIMESTAMPING_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TIMESTAMP_LBN 9
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TIMESTAMP_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_SNIFF_LBN 10
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_RX_SNIFF_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_SNIFF_LBN 11
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_TX_SNIFF_WIDTH 1
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_NVRAM_UPDATE_REPORT_VERIFY_RESULT_LBN 12
#define        MC_CMD_GET_CAPABILITIES_V3_OUT_NVRAM_UPDATE_REPORT_VERIFY_RESULT_WIDTH 1
/* Number of FATSOv2 contexts per datapath supported by this NIC. Not present
 * on older firmware (check the length).
 */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TSO_V2_N_CONTEXTS_OFST 24
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_TX_TSO_V2_N_CONTEXTS_LEN 2
/* One byte per PF containing the number of the external port assigned to this
 * PF, indexed by PF number. Special values indicate that a PF is either not
 * present or not assigned.
 */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_PFS_TO_PORTS_ASSIGNMENT_OFST 26
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_PFS_TO_PORTS_ASSIGNMENT_LEN 1
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_PFS_TO_PORTS_ASSIGNMENT_NUM 16
/* enum: The caller is not permitted to access information on this PF. */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_ACCESS_NOT_PERMITTED  0xff
/* enum: PF does not exist. */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_PF_NOT_PRESENT  0xfe
/* enum: PF does exist but is not assigned to any external port. */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_PF_NOT_ASSIGNED  0xfd
/* enum: This value indicates that PF is assigned, but it cannot be expressed
 * in this field. It is intended for a possible future situation where a more
 * complex scheme of PFs to ports mapping is being used. The future driver
 * should look for a new field supporting the new scheme. The current/old
 * driver should treat this value as PF_NOT_ASSIGNED.
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_INCOMPATIBLE_ASSIGNMENT  0xfc
/* One byte per PF containing the number of its VFs, indexed by PF number. A
 * special value indicates that a PF is not present.
 */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_NUM_VFS_PER_PF_OFST 42
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_NUM_VFS_PER_PF_LEN 1
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_NUM_VFS_PER_PF_NUM 16
/* enum: The caller is not permitted to access information on this PF. */
/*               MC_CMD_GET_CAPABILITIES_V3_OUT_ACCESS_NOT_PERMITTED  0xff */
/* enum: PF does not exist. */
/*               MC_CMD_GET_CAPABILITIES_V3_OUT_PF_NOT_PRESENT  0xfe */
/* Number of VIs available for each external port */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_NUM_VIS_PER_PORT_OFST 58
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_NUM_VIS_PER_PORT_LEN 2
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_NUM_VIS_PER_PORT_NUM 4
/* Size of RX descriptor cache expressed as binary logarithm The actual size
 * equals (2 ^ RX_DESC_CACHE_SIZE)
 */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_RX_DESC_CACHE_SIZE_OFST 66
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_RX_DESC_CACHE_SIZE_LEN 1
/* Size of TX descriptor cache expressed as binary logarithm The actual size
 * equals (2 ^ TX_DESC_CACHE_SIZE)
 */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_TX_DESC_CACHE_SIZE_OFST 67
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_TX_DESC_CACHE_SIZE_LEN 1
/* Total number of available PIO buffers */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_NUM_PIO_BUFFS_OFST 68
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_NUM_PIO_BUFFS_LEN 2
/* Size of a single PIO buffer */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_SIZE_PIO_BUFF_OFST 70
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_SIZE_PIO_BUFF_LEN 2
/* On chips later than Medford the amount of address space assigned to each VI
 * is configurable. This is a global setting that the driver must query to
 * discover the VI to address mapping. Cut-through PIO (CTPIO) is not available
 * with 8k VI windows.
 */
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_OFST 72
#define       MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_LEN 1
/* enum: Each VI occupies 8k as on Huntington and Medford. PIO is at offset 4k.
 * CTPIO is not mapped.
 */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_8K   0x0
/* enum: Each VI occupies 16k. PIO is at offset 4k. CTPIO is at offset 12k. */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_16K  0x1
/* enum: Each VI occupies 64k. PIO is at offset 4k. CTPIO is at offset 12k. */
#define          MC_CMD_GET_CAPABILITIES_V3_OUT_VI_WINDOW_MODE_64K  0x2


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

#define MC_CMD_0xb2_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xb3_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xb4_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_TCM_BUCKET_INIT_IN msgrequest */
#define    MC_CMD_TCM_BUCKET_INIT_IN_LEN 8
/* the bucket id */
#define       MC_CMD_TCM_BUCKET_INIT_IN_BUCKET_OFST 0
/* the rate in mbps */
#define       MC_CMD_TCM_BUCKET_INIT_IN_RATE_OFST 4

/* MC_CMD_TCM_BUCKET_INIT_EXT_IN msgrequest */
#define    MC_CMD_TCM_BUCKET_INIT_EXT_IN_LEN 12
/* the bucket id */
#define       MC_CMD_TCM_BUCKET_INIT_EXT_IN_BUCKET_OFST 0
/* the rate in mbps */
#define       MC_CMD_TCM_BUCKET_INIT_EXT_IN_RATE_OFST 4
/* the desired maximum fill level */
#define       MC_CMD_TCM_BUCKET_INIT_EXT_IN_MAX_FILL_OFST 8

/* MC_CMD_TCM_BUCKET_INIT_OUT msgresponse */
#define    MC_CMD_TCM_BUCKET_INIT_OUT_LEN 0


/***********************************/
/* MC_CMD_TCM_TXQ_INIT
 * Initialise txq in pacer with given options or set options
 */
#define MC_CMD_TCM_TXQ_INIT 0xb5

#define MC_CMD_0xb5_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_TCM_TXQ_INIT_IN msgrequest */
#define    MC_CMD_TCM_TXQ_INIT_IN_LEN 28
/* the txq id */
#define       MC_CMD_TCM_TXQ_INIT_IN_QID_OFST 0
/* the static priority associated with the txq */
#define       MC_CMD_TCM_TXQ_INIT_IN_LABEL_OFST 4
/* bitmask of the priority queues this txq is inserted into when inserted. */
#define       MC_CMD_TCM_TXQ_INIT_IN_PQ_FLAGS_OFST 8
#define        MC_CMD_TCM_TXQ_INIT_IN_PQ_FLAG_GUARANTEED_LBN 0
#define        MC_CMD_TCM_TXQ_INIT_IN_PQ_FLAG_GUARANTEED_WIDTH 1
#define        MC_CMD_TCM_TXQ_INIT_IN_PQ_FLAG_NORMAL_LBN 1
#define        MC_CMD_TCM_TXQ_INIT_IN_PQ_FLAG_NORMAL_WIDTH 1
#define        MC_CMD_TCM_TXQ_INIT_IN_PQ_FLAG_LOW_LBN 2
#define        MC_CMD_TCM_TXQ_INIT_IN_PQ_FLAG_LOW_WIDTH 1
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

/* MC_CMD_TCM_TXQ_INIT_EXT_IN msgrequest */
#define    MC_CMD_TCM_TXQ_INIT_EXT_IN_LEN 32
/* the txq id */
#define       MC_CMD_TCM_TXQ_INIT_EXT_IN_QID_OFST 0
/* the static priority associated with the txq */
#define       MC_CMD_TCM_TXQ_INIT_EXT_IN_LABEL_NORMAL_OFST 4
/* bitmask of the priority queues this txq is inserted into when inserted. */
#define       MC_CMD_TCM_TXQ_INIT_EXT_IN_PQ_FLAGS_OFST 8
#define        MC_CMD_TCM_TXQ_INIT_EXT_IN_PQ_FLAG_GUARANTEED_LBN 0
#define        MC_CMD_TCM_TXQ_INIT_EXT_IN_PQ_FLAG_GUARANTEED_WIDTH 1
#define        MC_CMD_TCM_TXQ_INIT_EXT_IN_PQ_FLAG_NORMAL_LBN 1
#define        MC_CMD_TCM_TXQ_INIT_EXT_IN_PQ_FLAG_NORMAL_WIDTH 1
#define        MC_CMD_TCM_TXQ_INIT_EXT_IN_PQ_FLAG_LOW_LBN 2
#define        MC_CMD_TCM_TXQ_INIT_EXT_IN_PQ_FLAG_LOW_WIDTH 1
/* the reaction point (RP) bucket */
#define       MC_CMD_TCM_TXQ_INIT_EXT_IN_RP_BKT_OFST 12
/* an already reserved bucket (typically set to bucket associated with outer
 * vswitch)
 */
#define       MC_CMD_TCM_TXQ_INIT_EXT_IN_MAX_BKT1_OFST 16
/* an already reserved bucket (typically set to bucket associated with inner
 * vswitch)
 */
#define       MC_CMD_TCM_TXQ_INIT_EXT_IN_MAX_BKT2_OFST 20
/* the min bucket (typically for ETS/minimum bandwidth) */
#define       MC_CMD_TCM_TXQ_INIT_EXT_IN_MIN_BKT_OFST 24
/* the static priority associated with the txq */
#define       MC_CMD_TCM_TXQ_INIT_EXT_IN_LABEL_GUARANTEED_OFST 28

/* MC_CMD_TCM_TXQ_INIT_OUT msgresponse */
#define    MC_CMD_TCM_TXQ_INIT_OUT_LEN 0


/***********************************/
/* MC_CMD_LINK_PIOBUF
 * Link a push I/O buffer to a TxQ
 */
#define MC_CMD_LINK_PIOBUF 0x92

#define MC_CMD_0x92_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

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

#define MC_CMD_0x93_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

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

#define MC_CMD_0x94_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* enum: VEPA (obsolete) */
#define          MC_CMD_VSWITCH_ALLOC_IN_VSWITCH_TYPE_VEPA  0x3
/* enum: MUX */
#define          MC_CMD_VSWITCH_ALLOC_IN_VSWITCH_TYPE_MUX  0x4
/* enum: Snapper specific; semantics TBD */
#define          MC_CMD_VSWITCH_ALLOC_IN_VSWITCH_TYPE_TEST  0x5
/* Flags controlling v-port creation */
#define       MC_CMD_VSWITCH_ALLOC_IN_FLAGS_OFST 8
#define        MC_CMD_VSWITCH_ALLOC_IN_FLAG_AUTO_PORT_LBN 0
#define        MC_CMD_VSWITCH_ALLOC_IN_FLAG_AUTO_PORT_WIDTH 1
/* The number of VLAN tags to allow for attached v-ports. For VLAN aggregators,
 * this must be one or greated, and the attached v-ports must have exactly this
 * number of tags. For other v-switch types, this must be zero of greater, and
 * is an upper limit on the number of VLAN tags for attached v-ports. An error
 * will be returned if existing configuration means we can't support attached
 * v-ports with this number of tags.
 */
#define       MC_CMD_VSWITCH_ALLOC_IN_NUM_VLAN_TAGS_OFST 12

/* MC_CMD_VSWITCH_ALLOC_OUT msgresponse */
#define    MC_CMD_VSWITCH_ALLOC_OUT_LEN 0


/***********************************/
/* MC_CMD_VSWITCH_FREE
 * de-allocate a v-switch.
 */
#define MC_CMD_VSWITCH_FREE 0x95

#define MC_CMD_0x95_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_VSWITCH_FREE_IN msgrequest */
#define    MC_CMD_VSWITCH_FREE_IN_LEN 4
/* The port to which the v-switch is connected. */
#define       MC_CMD_VSWITCH_FREE_IN_UPSTREAM_PORT_ID_OFST 0

/* MC_CMD_VSWITCH_FREE_OUT msgresponse */
#define    MC_CMD_VSWITCH_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_VSWITCH_QUERY
 * read some config of v-switch. For now this command is an empty placeholder.
 * It may be used to check if a v-switch is connected to a given EVB port (if
 * not, then the command returns ENOENT).
 */
#define MC_CMD_VSWITCH_QUERY 0x63

#define MC_CMD_0x63_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_VSWITCH_QUERY_IN msgrequest */
#define    MC_CMD_VSWITCH_QUERY_IN_LEN 4
/* The port to which the v-switch is connected. */
#define       MC_CMD_VSWITCH_QUERY_IN_UPSTREAM_PORT_ID_OFST 0

/* MC_CMD_VSWITCH_QUERY_OUT msgresponse */
#define    MC_CMD_VSWITCH_QUERY_OUT_LEN 0


/***********************************/
/* MC_CMD_VPORT_ALLOC
 * allocate a v-port.
 */
#define MC_CMD_VPORT_ALLOC 0x96

#define MC_CMD_0x96_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
#define        MC_CMD_VPORT_ALLOC_IN_FLAG_VLAN_RESTRICT_LBN 1
#define        MC_CMD_VPORT_ALLOC_IN_FLAG_VLAN_RESTRICT_WIDTH 1
/* The number of VLAN tags to insert/remove. An error will be returned if
 * incompatible with the number of VLAN tags specified for the upstream
 * v-switch.
 */
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

#define MC_CMD_0x97_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x98_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_VADAPTOR_ALLOC_IN msgrequest */
#define    MC_CMD_VADAPTOR_ALLOC_IN_LEN 30
/* The port to connect to the v-adaptor's port. */
#define       MC_CMD_VADAPTOR_ALLOC_IN_UPSTREAM_PORT_ID_OFST 0
/* Flags controlling v-adaptor creation */
#define       MC_CMD_VADAPTOR_ALLOC_IN_FLAGS_OFST 8
#define        MC_CMD_VADAPTOR_ALLOC_IN_FLAG_AUTO_VADAPTOR_LBN 0
#define        MC_CMD_VADAPTOR_ALLOC_IN_FLAG_AUTO_VADAPTOR_WIDTH 1
#define        MC_CMD_VADAPTOR_ALLOC_IN_FLAG_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED_LBN 1
#define        MC_CMD_VADAPTOR_ALLOC_IN_FLAG_PERMIT_SET_MAC_WHEN_FILTERS_INSTALLED_WIDTH 1
/* The number of VLAN tags to strip on receive */
#define       MC_CMD_VADAPTOR_ALLOC_IN_NUM_VLANS_OFST 12
/* The number of VLAN tags to transparently insert/remove. */
#define       MC_CMD_VADAPTOR_ALLOC_IN_NUM_VLAN_TAGS_OFST 16
/* The actual VLAN tags to insert/remove */
#define       MC_CMD_VADAPTOR_ALLOC_IN_VLAN_TAGS_OFST 20
#define        MC_CMD_VADAPTOR_ALLOC_IN_VLAN_TAG_0_LBN 0
#define        MC_CMD_VADAPTOR_ALLOC_IN_VLAN_TAG_0_WIDTH 16
#define        MC_CMD_VADAPTOR_ALLOC_IN_VLAN_TAG_1_LBN 16
#define        MC_CMD_VADAPTOR_ALLOC_IN_VLAN_TAG_1_WIDTH 16
/* The MAC address to assign to this v-adaptor */
#define       MC_CMD_VADAPTOR_ALLOC_IN_MACADDR_OFST 24
#define       MC_CMD_VADAPTOR_ALLOC_IN_MACADDR_LEN 6
/* enum: Derive the MAC address from the upstream port */
#define          MC_CMD_VADAPTOR_ALLOC_IN_AUTO_MAC  0x0

/* MC_CMD_VADAPTOR_ALLOC_OUT msgresponse */
#define    MC_CMD_VADAPTOR_ALLOC_OUT_LEN 0


/***********************************/
/* MC_CMD_VADAPTOR_FREE
 * de-allocate a v-adaptor.
 */
#define MC_CMD_VADAPTOR_FREE 0x99

#define MC_CMD_0x99_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_VADAPTOR_FREE_IN msgrequest */
#define    MC_CMD_VADAPTOR_FREE_IN_LEN 4
/* The port to which the v-adaptor is connected. */
#define       MC_CMD_VADAPTOR_FREE_IN_UPSTREAM_PORT_ID_OFST 0

/* MC_CMD_VADAPTOR_FREE_OUT msgresponse */
#define    MC_CMD_VADAPTOR_FREE_OUT_LEN 0


/***********************************/
/* MC_CMD_VADAPTOR_SET_MAC
 * assign a new MAC address to a v-adaptor.
 */
#define MC_CMD_VADAPTOR_SET_MAC 0x5d

#define MC_CMD_0x5d_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_VADAPTOR_SET_MAC_IN msgrequest */
#define    MC_CMD_VADAPTOR_SET_MAC_IN_LEN 10
/* The port to which the v-adaptor is connected. */
#define       MC_CMD_VADAPTOR_SET_MAC_IN_UPSTREAM_PORT_ID_OFST 0
/* The new MAC address to assign to this v-adaptor */
#define       MC_CMD_VADAPTOR_SET_MAC_IN_MACADDR_OFST 4
#define       MC_CMD_VADAPTOR_SET_MAC_IN_MACADDR_LEN 6

/* MC_CMD_VADAPTOR_SET_MAC_OUT msgresponse */
#define    MC_CMD_VADAPTOR_SET_MAC_OUT_LEN 0


/***********************************/
/* MC_CMD_VADAPTOR_GET_MAC
 * read the MAC address assigned to a v-adaptor.
 */
#define MC_CMD_VADAPTOR_GET_MAC 0x5e

#define MC_CMD_0x5e_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_VADAPTOR_GET_MAC_IN msgrequest */
#define    MC_CMD_VADAPTOR_GET_MAC_IN_LEN 4
/* The port to which the v-adaptor is connected. */
#define       MC_CMD_VADAPTOR_GET_MAC_IN_UPSTREAM_PORT_ID_OFST 0

/* MC_CMD_VADAPTOR_GET_MAC_OUT msgresponse */
#define    MC_CMD_VADAPTOR_GET_MAC_OUT_LEN 6
/* The MAC address assigned to this v-adaptor */
#define       MC_CMD_VADAPTOR_GET_MAC_OUT_MACADDR_OFST 0
#define       MC_CMD_VADAPTOR_GET_MAC_OUT_MACADDR_LEN 6


/***********************************/
/* MC_CMD_VADAPTOR_QUERY
 * read some config of v-adaptor.
 */
#define MC_CMD_VADAPTOR_QUERY 0x61

#define MC_CMD_0x61_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_VADAPTOR_QUERY_IN msgrequest */
#define    MC_CMD_VADAPTOR_QUERY_IN_LEN 4
/* The port to which the v-adaptor is connected. */
#define       MC_CMD_VADAPTOR_QUERY_IN_UPSTREAM_PORT_ID_OFST 0

/* MC_CMD_VADAPTOR_QUERY_OUT msgresponse */
#define    MC_CMD_VADAPTOR_QUERY_OUT_LEN 12
/* The EVB port flags as defined at MC_CMD_VPORT_ALLOC. */
#define       MC_CMD_VADAPTOR_QUERY_OUT_PORT_FLAGS_OFST 0
/* The v-adaptor flags as defined at MC_CMD_VADAPTOR_ALLOC. */
#define       MC_CMD_VADAPTOR_QUERY_OUT_VADAPTOR_FLAGS_OFST 4
/* The number of VLAN tags that may still be added */
#define       MC_CMD_VADAPTOR_QUERY_OUT_NUM_AVAILABLE_VLAN_TAGS_OFST 8


/***********************************/
/* MC_CMD_EVB_PORT_ASSIGN
 * assign a port to a PCI function.
 */
#define MC_CMD_EVB_PORT_ASSIGN 0x9a

#define MC_CMD_0x9a_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0x9b_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0x9c_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

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

#define MC_CMD_0x9d_PRIVILEGE_CTG SRIOV_CTG_ONLOAD

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

#define MC_CMD_0x9e_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* The handle of the new RSS context. This should be considered opaque to the
 * host, although a value of 0xFFFFFFFF is guaranteed never to be a valid
 * handle.
 */
#define       MC_CMD_RSS_CONTEXT_ALLOC_OUT_RSS_CONTEXT_ID_OFST 0
/* enum: guaranteed invalid RSS context handle value */
#define          MC_CMD_RSS_CONTEXT_ALLOC_OUT_RSS_CONTEXT_ID_INVALID  0xffffffff


/***********************************/
/* MC_CMD_RSS_CONTEXT_FREE
 * Free an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_FREE 0x9f

#define MC_CMD_0x9f_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xa0_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xa1_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xa2_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xa3_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xe1_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_RSS_CONTEXT_SET_FLAGS_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_LEN 8
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_RSS_CONTEXT_ID_OFST 0
/* Hash control flags. The _EN bits are always supported, but new modes are
 * available when ADDITIONAL_RSS_MODES is reported by MC_CMD_GET_CAPABILITIES:
 * in this case, the MODE fields may be set to non-zero values, and will take
 * effect regardless of the settings of the _EN flags. See the RSS_MODE
 * structure for the meaning of the mode bits. Drivers must check the
 * capability before trying to set any _MODE fields, as older firmware will
 * reject any attempt to set the FLAGS field to a value > 0xff with EINVAL. In
 * the case where all the _MODE flags are zero, the _EN flags take effect,
 * providing backward compatibility for existing drivers. (Setting all _MODE
 * *and* all _EN flags to zero is valid, to disable RSS spreading for that
 * particular packet type.)
 */
#define       MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_FLAGS_OFST 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_IPV4_EN_LBN 0
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_IPV4_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_TCPV4_EN_LBN 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_TCPV4_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_IPV6_EN_LBN 2
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_IPV6_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_TCPV6_EN_LBN 3
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TOEPLITZ_TCPV6_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_RESERVED_LBN 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_RESERVED_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TCP_IPV4_RSS_MODE_LBN 8
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TCP_IPV4_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_UDP_IPV4_RSS_MODE_LBN 12
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_UDP_IPV4_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_OTHER_IPV4_RSS_MODE_LBN 16
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_OTHER_IPV4_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TCP_IPV6_RSS_MODE_LBN 20
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_TCP_IPV6_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_UDP_IPV6_RSS_MODE_LBN 24
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_UDP_IPV6_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_OTHER_IPV6_RSS_MODE_LBN 28
#define        MC_CMD_RSS_CONTEXT_SET_FLAGS_IN_OTHER_IPV6_RSS_MODE_WIDTH 4

/* MC_CMD_RSS_CONTEXT_SET_FLAGS_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_SET_FLAGS_OUT_LEN 0


/***********************************/
/* MC_CMD_RSS_CONTEXT_GET_FLAGS
 * Get various control flags for an RSS context.
 */
#define MC_CMD_RSS_CONTEXT_GET_FLAGS 0xe2

#define MC_CMD_0xe2_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_RSS_CONTEXT_GET_FLAGS_IN msgrequest */
#define    MC_CMD_RSS_CONTEXT_GET_FLAGS_IN_LEN 4
/* The handle of the RSS context */
#define       MC_CMD_RSS_CONTEXT_GET_FLAGS_IN_RSS_CONTEXT_ID_OFST 0

/* MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT msgresponse */
#define    MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_LEN 8
/* Hash control flags. If all _MODE bits are zero (which will always be true
 * for older firmware which does not report the ADDITIONAL_RSS_MODES
 * capability), the _EN bits report the state. If any _MODE bits are non-zero
 * (which will only be true when the firmware reports ADDITIONAL_RSS_MODES)
 * then the _EN bits should be disregarded, although the _MODE flags are
 * guaranteed to be consistent with the _EN flags for a freshly-allocated RSS
 * context and in the case where the _EN flags were used in the SET. This
 * provides backward compatibility: old drivers will not be attempting to
 * derive any meaning from the _MODE bits (and can never set them to any value
 * not representable by the _EN bits); new drivers can always determine the
 * mode by looking only at the _MODE bits; the value returned by a GET can
 * always be used for a SET regardless of old/new driver vs. old/new firmware.
 */
#define       MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_FLAGS_OFST 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV4_EN_LBN 0
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV4_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV4_EN_LBN 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV4_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV6_EN_LBN 2
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_IPV6_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV6_EN_LBN 3
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TOEPLITZ_TCPV6_EN_WIDTH 1
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_RESERVED_LBN 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_RESERVED_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TCP_IPV4_RSS_MODE_LBN 8
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TCP_IPV4_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_UDP_IPV4_RSS_MODE_LBN 12
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_UDP_IPV4_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_OTHER_IPV4_RSS_MODE_LBN 16
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_OTHER_IPV4_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TCP_IPV6_RSS_MODE_LBN 20
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_TCP_IPV6_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_UDP_IPV6_RSS_MODE_LBN 24
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_UDP_IPV6_RSS_MODE_WIDTH 4
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_OTHER_IPV6_RSS_MODE_LBN 28
#define        MC_CMD_RSS_CONTEXT_GET_FLAGS_OUT_OTHER_IPV6_RSS_MODE_WIDTH 4


/***********************************/
/* MC_CMD_DOT1P_MAPPING_ALLOC
 * Allocate a .1p mapping.
 */
#define MC_CMD_DOT1P_MAPPING_ALLOC 0xa4

#define MC_CMD_0xa4_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
/* The handle of the new .1p mapping. This should be considered opaque to the
 * host, although a value of 0xFFFFFFFF is guaranteed never to be a valid
 * handle.
 */
#define       MC_CMD_DOT1P_MAPPING_ALLOC_OUT_DOT1P_MAPPING_ID_OFST 0
/* enum: guaranteed invalid .1p mapping handle value */
#define          MC_CMD_DOT1P_MAPPING_ALLOC_OUT_DOT1P_MAPPING_ID_INVALID  0xffffffff


/***********************************/
/* MC_CMD_DOT1P_MAPPING_FREE
 * Free a .1p mapping.
 */
#define MC_CMD_DOT1P_MAPPING_FREE 0xa5

#define MC_CMD_0xa5_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xa6_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xa7_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xbf_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xc0_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* MC_CMD_VPORT_ADD_MAC_ADDRESS
 * Add a MAC address to a v-port
 */
#define MC_CMD_VPORT_ADD_MAC_ADDRESS 0xa8

#define MC_CMD_0xa8_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xa9_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xaa_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* MC_CMD_VPORT_RECONFIGURE
 * Replace VLAN tags and/or MAC addresses of an existing v-port. If the v-port
 * has already been passed to another function (v-port's user), then that
 * function will be reset before applying the changes.
 */
#define MC_CMD_VPORT_RECONFIGURE 0xeb

#define MC_CMD_0xeb_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_VPORT_RECONFIGURE_IN msgrequest */
#define    MC_CMD_VPORT_RECONFIGURE_IN_LEN 44
/* The handle of the v-port */
#define       MC_CMD_VPORT_RECONFIGURE_IN_VPORT_ID_OFST 0
/* Flags requesting what should be changed. */
#define       MC_CMD_VPORT_RECONFIGURE_IN_FLAGS_OFST 4
#define        MC_CMD_VPORT_RECONFIGURE_IN_REPLACE_VLAN_TAGS_LBN 0
#define        MC_CMD_VPORT_RECONFIGURE_IN_REPLACE_VLAN_TAGS_WIDTH 1
#define        MC_CMD_VPORT_RECONFIGURE_IN_REPLACE_MACADDRS_LBN 1
#define        MC_CMD_VPORT_RECONFIGURE_IN_REPLACE_MACADDRS_WIDTH 1
/* The number of VLAN tags to insert/remove. An error will be returned if
 * incompatible with the number of VLAN tags specified for the upstream
 * v-switch.
 */
#define       MC_CMD_VPORT_RECONFIGURE_IN_NUM_VLAN_TAGS_OFST 8
/* The actual VLAN tags to insert/remove */
#define       MC_CMD_VPORT_RECONFIGURE_IN_VLAN_TAGS_OFST 12
#define        MC_CMD_VPORT_RECONFIGURE_IN_VLAN_TAG_0_LBN 0
#define        MC_CMD_VPORT_RECONFIGURE_IN_VLAN_TAG_0_WIDTH 16
#define        MC_CMD_VPORT_RECONFIGURE_IN_VLAN_TAG_1_LBN 16
#define        MC_CMD_VPORT_RECONFIGURE_IN_VLAN_TAG_1_WIDTH 16
/* The number of MAC addresses to add */
#define       MC_CMD_VPORT_RECONFIGURE_IN_NUM_MACADDRS_OFST 16
/* MAC addresses to add */
#define       MC_CMD_VPORT_RECONFIGURE_IN_MACADDRS_OFST 20
#define       MC_CMD_VPORT_RECONFIGURE_IN_MACADDRS_LEN 6
#define       MC_CMD_VPORT_RECONFIGURE_IN_MACADDRS_NUM 4

/* MC_CMD_VPORT_RECONFIGURE_OUT msgresponse */
#define    MC_CMD_VPORT_RECONFIGURE_OUT_LEN 4
#define       MC_CMD_VPORT_RECONFIGURE_OUT_FLAGS_OFST 0
#define        MC_CMD_VPORT_RECONFIGURE_OUT_RESET_DONE_LBN 0
#define        MC_CMD_VPORT_RECONFIGURE_OUT_RESET_DONE_WIDTH 1


/***********************************/
/* MC_CMD_EVB_PORT_QUERY
 * read some config of v-port.
 */
#define MC_CMD_EVB_PORT_QUERY 0x62

#define MC_CMD_0x62_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_EVB_PORT_QUERY_IN msgrequest */
#define    MC_CMD_EVB_PORT_QUERY_IN_LEN 4
/* The handle of the v-port */
#define       MC_CMD_EVB_PORT_QUERY_IN_PORT_ID_OFST 0

/* MC_CMD_EVB_PORT_QUERY_OUT msgresponse */
#define    MC_CMD_EVB_PORT_QUERY_OUT_LEN 8
/* The EVB port flags as defined at MC_CMD_VPORT_ALLOC. */
#define       MC_CMD_EVB_PORT_QUERY_OUT_PORT_FLAGS_OFST 0
/* The number of VLAN tags that may be used on a v-adaptor connected to this
 * EVB port.
 */
#define       MC_CMD_EVB_PORT_QUERY_OUT_NUM_AVAILABLE_VLAN_TAGS_OFST 4


/***********************************/
/* MC_CMD_DUMP_BUFTBL_ENTRIES
 * Dump buffer table entries, mainly for command client debug use. Dumps
 * absolute entries, and does not use chunk handles. All entries must be in
 * range, and used for q page mapping, Although the latter restriction may be
 * lifted in future.
 */
#define MC_CMD_DUMP_BUFTBL_ENTRIES 0xab

#define MC_CMD_0xab_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xc1_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_SET_RXDP_CONFIG_IN msgrequest */
#define    MC_CMD_SET_RXDP_CONFIG_IN_LEN 4
#define       MC_CMD_SET_RXDP_CONFIG_IN_DATA_OFST 0
#define        MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_DMA_LBN 0
#define        MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_DMA_WIDTH 1
#define        MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_LEN_LBN 1
#define        MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_LEN_WIDTH 2
/* enum: pad to 64 bytes */
#define          MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_64  0x0
/* enum: pad to 128 bytes (Medford only) */
#define          MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_128  0x1
/* enum: pad to 256 bytes (Medford only) */
#define          MC_CMD_SET_RXDP_CONFIG_IN_PAD_HOST_256   0x2

/* MC_CMD_SET_RXDP_CONFIG_OUT msgresponse */
#define    MC_CMD_SET_RXDP_CONFIG_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_RXDP_CONFIG
 * Get global RXDP configuration settings
 */
#define MC_CMD_GET_RXDP_CONFIG 0xc2

#define MC_CMD_0xc2_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_RXDP_CONFIG_IN msgrequest */
#define    MC_CMD_GET_RXDP_CONFIG_IN_LEN 0

/* MC_CMD_GET_RXDP_CONFIG_OUT msgresponse */
#define    MC_CMD_GET_RXDP_CONFIG_OUT_LEN 4
#define       MC_CMD_GET_RXDP_CONFIG_OUT_DATA_OFST 0
#define        MC_CMD_GET_RXDP_CONFIG_OUT_PAD_HOST_DMA_LBN 0
#define        MC_CMD_GET_RXDP_CONFIG_OUT_PAD_HOST_DMA_WIDTH 1
#define        MC_CMD_GET_RXDP_CONFIG_OUT_PAD_HOST_LEN_LBN 1
#define        MC_CMD_GET_RXDP_CONFIG_OUT_PAD_HOST_LEN_WIDTH 2
/*             Enum values, see field(s): */
/*                MC_CMD_SET_RXDP_CONFIG/MC_CMD_SET_RXDP_CONFIG_IN/PAD_HOST_LEN */


/***********************************/
/* MC_CMD_GET_CLOCK
 * Return the system and PDCPU clock frequencies.
 */
#define MC_CMD_GET_CLOCK 0xac

#define MC_CMD_0xac_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xad_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_SET_CLOCK_IN msgrequest */
#define    MC_CMD_SET_CLOCK_IN_LEN 28
/* Requested frequency in MHz for system clock domain */
#define       MC_CMD_SET_CLOCK_IN_SYS_FREQ_OFST 0
/* enum: Leave the system clock domain frequency unchanged */
#define          MC_CMD_SET_CLOCK_IN_SYS_DOMAIN_DONT_CHANGE  0x0
/* Requested frequency in MHz for inter-core clock domain */
#define       MC_CMD_SET_CLOCK_IN_ICORE_FREQ_OFST 4
/* enum: Leave the inter-core clock domain frequency unchanged */
#define          MC_CMD_SET_CLOCK_IN_ICORE_DOMAIN_DONT_CHANGE  0x0
/* Requested frequency in MHz for DPCPU clock domain */
#define       MC_CMD_SET_CLOCK_IN_DPCPU_FREQ_OFST 8
/* enum: Leave the DPCPU clock domain frequency unchanged */
#define          MC_CMD_SET_CLOCK_IN_DPCPU_DOMAIN_DONT_CHANGE  0x0
/* Requested frequency in MHz for PCS clock domain */
#define       MC_CMD_SET_CLOCK_IN_PCS_FREQ_OFST 12
/* enum: Leave the PCS clock domain frequency unchanged */
#define          MC_CMD_SET_CLOCK_IN_PCS_DOMAIN_DONT_CHANGE  0x0
/* Requested frequency in MHz for MC clock domain */
#define       MC_CMD_SET_CLOCK_IN_MC_FREQ_OFST 16
/* enum: Leave the MC clock domain frequency unchanged */
#define          MC_CMD_SET_CLOCK_IN_MC_DOMAIN_DONT_CHANGE  0x0
/* Requested frequency in MHz for rmon clock domain */
#define       MC_CMD_SET_CLOCK_IN_RMON_FREQ_OFST 20
/* enum: Leave the rmon clock domain frequency unchanged */
#define          MC_CMD_SET_CLOCK_IN_RMON_DOMAIN_DONT_CHANGE  0x0
/* Requested frequency in MHz for vswitch clock domain */
#define       MC_CMD_SET_CLOCK_IN_VSWITCH_FREQ_OFST 24
/* enum: Leave the vswitch clock domain frequency unchanged */
#define          MC_CMD_SET_CLOCK_IN_VSWITCH_DOMAIN_DONT_CHANGE  0x0

/* MC_CMD_SET_CLOCK_OUT msgresponse */
#define    MC_CMD_SET_CLOCK_OUT_LEN 28
/* Resulting system frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_SYS_FREQ_OFST 0
/* enum: The system clock domain doesn't exist */
#define          MC_CMD_SET_CLOCK_OUT_SYS_DOMAIN_UNSUPPORTED  0x0
/* Resulting inter-core frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_ICORE_FREQ_OFST 4
/* enum: The inter-core clock domain doesn't exist / isn't used */
#define          MC_CMD_SET_CLOCK_OUT_ICORE_DOMAIN_UNSUPPORTED  0x0
/* Resulting DPCPU frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_DPCPU_FREQ_OFST 8
/* enum: The dpcpu clock domain doesn't exist */
#define          MC_CMD_SET_CLOCK_OUT_DPCPU_DOMAIN_UNSUPPORTED  0x0
/* Resulting PCS frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_PCS_FREQ_OFST 12
/* enum: The PCS clock domain doesn't exist / isn't controlled */
#define          MC_CMD_SET_CLOCK_OUT_PCS_DOMAIN_UNSUPPORTED  0x0
/* Resulting MC frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_MC_FREQ_OFST 16
/* enum: The MC clock domain doesn't exist / isn't controlled */
#define          MC_CMD_SET_CLOCK_OUT_MC_DOMAIN_UNSUPPORTED  0x0
/* Resulting rmon frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_RMON_FREQ_OFST 20
/* enum: The rmon clock domain doesn't exist / isn't controlled */
#define          MC_CMD_SET_CLOCK_OUT_RMON_DOMAIN_UNSUPPORTED  0x0
/* Resulting vswitch frequency in MHz */
#define       MC_CMD_SET_CLOCK_OUT_VSWITCH_FREQ_OFST 24
/* enum: The vswitch clock domain doesn't exist / isn't controlled */
#define          MC_CMD_SET_CLOCK_OUT_VSWITCH_DOMAIN_UNSUPPORTED  0x0


/***********************************/
/* MC_CMD_DPCPU_RPC
 * Send an arbitrary DPCPU message.
 */
#define MC_CMD_DPCPU_RPC 0xae

#define MC_CMD_0xae_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_DPCPU_RPC_IN msgrequest */
#define    MC_CMD_DPCPU_RPC_IN_LEN 36
#define       MC_CMD_DPCPU_RPC_IN_CPU_OFST 0
/* enum: RxDPCPU0 */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_RX0  0x0
/* enum: TxDPCPU0 */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_TX0  0x1
/* enum: TxDPCPU1 */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_TX1  0x2
/* enum: RxDPCPU1 (Medford only) */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_RX1   0x3
/* enum: RxDPCPU (will be for the calling function; for now, just an alias of
 * DPCPU_RX0)
 */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_RX   0x80
/* enum: TxDPCPU (will be for the calling function; for now, just an alias of
 * DPCPU_TX0)
 */
#define          MC_CMD_DPCPU_RPC_IN_DPCPU_TX   0x81
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

#define MC_CMD_0xe3_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_TRIGGER_INTERRUPT_IN msgrequest */
#define    MC_CMD_TRIGGER_INTERRUPT_IN_LEN 4
/* Interrupt level relative to base for function. */
#define       MC_CMD_TRIGGER_INTERRUPT_IN_INTR_LEVEL_OFST 0

/* MC_CMD_TRIGGER_INTERRUPT_OUT msgresponse */
#define    MC_CMD_TRIGGER_INTERRUPT_OUT_LEN 0


/***********************************/
/* MC_CMD_SHMBOOT_OP
 * Special operations to support (for now) shmboot.
 */
#define MC_CMD_SHMBOOT_OP 0xe6

#define MC_CMD_0xe6_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_SHMBOOT_OP_IN msgrequest */
#define    MC_CMD_SHMBOOT_OP_IN_LEN 4
/* Identifies the operation to perform */
#define       MC_CMD_SHMBOOT_OP_IN_SHMBOOT_OP_OFST 0
/* enum: Copy slave_data section to the slave core. (Greenport only) */
#define          MC_CMD_SHMBOOT_OP_IN_PUSH_SLAVE_DATA  0x0

/* MC_CMD_SHMBOOT_OP_OUT msgresponse */
#define    MC_CMD_SHMBOOT_OP_OUT_LEN 0


/***********************************/
/* MC_CMD_CAP_BLK_READ
 * Read multiple 64bit words from capture block memory
 */
#define MC_CMD_CAP_BLK_READ 0xe7

#define MC_CMD_0xe7_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CAP_BLK_READ_IN msgrequest */
#define    MC_CMD_CAP_BLK_READ_IN_LEN 12
#define       MC_CMD_CAP_BLK_READ_IN_CAP_REG_OFST 0
#define       MC_CMD_CAP_BLK_READ_IN_ADDR_OFST 4
#define       MC_CMD_CAP_BLK_READ_IN_COUNT_OFST 8

/* MC_CMD_CAP_BLK_READ_OUT msgresponse */
#define    MC_CMD_CAP_BLK_READ_OUT_LENMIN 8
#define    MC_CMD_CAP_BLK_READ_OUT_LENMAX 248
#define    MC_CMD_CAP_BLK_READ_OUT_LEN(num) (0+8*(num))
#define       MC_CMD_CAP_BLK_READ_OUT_BUFFER_OFST 0
#define       MC_CMD_CAP_BLK_READ_OUT_BUFFER_LEN 8
#define       MC_CMD_CAP_BLK_READ_OUT_BUFFER_LO_OFST 0
#define       MC_CMD_CAP_BLK_READ_OUT_BUFFER_HI_OFST 4
#define       MC_CMD_CAP_BLK_READ_OUT_BUFFER_MINNUM 1
#define       MC_CMD_CAP_BLK_READ_OUT_BUFFER_MAXNUM 31


/***********************************/
/* MC_CMD_DUMP_DO
 * Take a dump of the DUT state
 */
#define MC_CMD_DUMP_DO 0xe8

#define MC_CMD_0xe8_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
/* enum: The uart port this command was received over (if using a uart
 * transport)
 */
#define          MC_CMD_DUMP_DO_IN_UART_PORT_SRC  0xff
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

#define MC_CMD_0xe9_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xea_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xec_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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

#define MC_CMD_0xed_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_ENABLE_OFFLINE_BIST_IN msgrequest */
#define    MC_CMD_ENABLE_OFFLINE_BIST_IN_LEN 0

/* MC_CMD_ENABLE_OFFLINE_BIST_OUT msgresponse */
#define    MC_CMD_ENABLE_OFFLINE_BIST_OUT_LEN 0


/***********************************/
/* MC_CMD_UART_SEND_DATA
 * Send checksummed[sic] block of data over the uart. Response is a placeholder
 * should we wish to make this reliable; currently requests are fire-and-
 * forget.
 */
#define MC_CMD_UART_SEND_DATA 0xee

#define MC_CMD_0xee_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_UART_SEND_DATA_OUT msgrequest */
#define    MC_CMD_UART_SEND_DATA_OUT_LENMIN 16
#define    MC_CMD_UART_SEND_DATA_OUT_LENMAX 252
#define    MC_CMD_UART_SEND_DATA_OUT_LEN(num) (16+1*(num))
/* CRC32 over OFFSET, LENGTH, RESERVED, DATA */
#define       MC_CMD_UART_SEND_DATA_OUT_CHECKSUM_OFST 0
/* Offset at which to write the data */
#define       MC_CMD_UART_SEND_DATA_OUT_OFFSET_OFST 4
/* Length of data */
#define       MC_CMD_UART_SEND_DATA_OUT_LENGTH_OFST 8
/* Reserved for future use */
#define       MC_CMD_UART_SEND_DATA_OUT_RESERVED_OFST 12
#define       MC_CMD_UART_SEND_DATA_OUT_DATA_OFST 16
#define       MC_CMD_UART_SEND_DATA_OUT_DATA_LEN 1
#define       MC_CMD_UART_SEND_DATA_OUT_DATA_MINNUM 0
#define       MC_CMD_UART_SEND_DATA_OUT_DATA_MAXNUM 236

/* MC_CMD_UART_SEND_DATA_IN msgresponse */
#define    MC_CMD_UART_SEND_DATA_IN_LEN 0


/***********************************/
/* MC_CMD_UART_RECV_DATA
 * Request checksummed[sic] block of data over the uart. Only a placeholder,
 * subject to change and not currently implemented.
 */
#define MC_CMD_UART_RECV_DATA 0xef

#define MC_CMD_0xef_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_UART_RECV_DATA_OUT msgrequest */
#define    MC_CMD_UART_RECV_DATA_OUT_LEN 16
/* CRC32 over OFFSET, LENGTH, RESERVED */
#define       MC_CMD_UART_RECV_DATA_OUT_CHECKSUM_OFST 0
/* Offset from which to read the data */
#define       MC_CMD_UART_RECV_DATA_OUT_OFFSET_OFST 4
/* Length of data */
#define       MC_CMD_UART_RECV_DATA_OUT_LENGTH_OFST 8
/* Reserved for future use */
#define       MC_CMD_UART_RECV_DATA_OUT_RESERVED_OFST 12

/* MC_CMD_UART_RECV_DATA_IN msgresponse */
#define    MC_CMD_UART_RECV_DATA_IN_LENMIN 16
#define    MC_CMD_UART_RECV_DATA_IN_LENMAX 252
#define    MC_CMD_UART_RECV_DATA_IN_LEN(num) (16+1*(num))
/* CRC32 over RESERVED1, RESERVED2, RESERVED3, DATA */
#define       MC_CMD_UART_RECV_DATA_IN_CHECKSUM_OFST 0
/* Offset at which to write the data */
#define       MC_CMD_UART_RECV_DATA_IN_RESERVED1_OFST 4
/* Length of data */
#define       MC_CMD_UART_RECV_DATA_IN_RESERVED2_OFST 8
/* Reserved for future use */
#define       MC_CMD_UART_RECV_DATA_IN_RESERVED3_OFST 12
#define       MC_CMD_UART_RECV_DATA_IN_DATA_OFST 16
#define       MC_CMD_UART_RECV_DATA_IN_DATA_LEN 1
#define       MC_CMD_UART_RECV_DATA_IN_DATA_MINNUM 0
#define       MC_CMD_UART_RECV_DATA_IN_DATA_MAXNUM 236


/***********************************/
/* MC_CMD_READ_FUSES
 * Read data programmed into the device One-Time-Programmable (OTP) Fuses
 */
#define MC_CMD_READ_FUSES 0xf0

#define MC_CMD_0xf0_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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

#define MC_CMD_0xf1_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
/* enum: Start KR Serdes Eye diagram plot on a given lane. Lane must have valid
 * signal.
 */
#define          MC_CMD_KR_TUNE_IN_START_EYE_PLOT  0x5
/* enum: Poll KR Serdes Eye diagram plot. Returns one row of BER data. The
 * caller should call this command repeatedly after starting eye plot, until no
 * more data is returned.
 */
#define          MC_CMD_KR_TUNE_IN_POLL_EYE_PLOT  0x6
/* enum: Read Figure Of Merit (eye quality, higher is better). */
#define          MC_CMD_KR_TUNE_IN_READ_FOM  0x7
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
/* enum: Attenuation (0-15, Huntington) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_ATT  0x0
/* enum: CTLE Boost (0-15, Huntington) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_BOOST  0x1
/* enum: Edge DFE Tap1 (Huntington - 0 - max negative, 64 - zero, 127 - max
 * positive, Medford - 0-31)
 */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP1  0x2
/* enum: Edge DFE Tap2 (Huntington - 0 - max negative, 32 - zero, 63 - max
 * positive, Medford - 0-31)
 */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP2  0x3
/* enum: Edge DFE Tap3 (Huntington - 0 - max negative, 32 - zero, 63 - max
 * positive, Medford - 0-16)
 */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP3  0x4
/* enum: Edge DFE Tap4 (Huntington - 0 - max negative, 32 - zero, 63 - max
 * positive, Medford - 0-16)
 */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP4  0x5
/* enum: Edge DFE Tap5 (Huntington - 0 - max negative, 32 - zero, 63 - max
 * positive, Medford - 0-16)
 */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_TAP5  0x6
/* enum: Edge DFE DLEV (0-128 for Medford) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_EDFE_DLEV  0x7
/* enum: Variable Gain Amplifier (0-15, Medford) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_VGA  0x8
/* enum: CTLE EQ Capacitor (0-15, Medford) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_CTLE_EQC  0x9
/* enum: CTLE EQ Resistor (0-7, Medford) */
#define          MC_CMD_KR_TUNE_RXEQ_GET_OUT_CTLE_EQRES  0xa
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

/* MC_CMD_KR_TUNE_TXEQ_GET_IN msgrequest */
#define    MC_CMD_KR_TUNE_TXEQ_GET_IN_LEN 4
/* Requested operation */
#define       MC_CMD_KR_TUNE_TXEQ_GET_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_TXEQ_GET_IN_KR_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_TXEQ_GET_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_TXEQ_GET_IN_KR_TUNE_RSVD_LEN 3

/* MC_CMD_KR_TUNE_TXEQ_GET_OUT msgresponse */
#define    MC_CMD_KR_TUNE_TXEQ_GET_OUT_LENMIN 4
#define    MC_CMD_KR_TUNE_TXEQ_GET_OUT_LENMAX 252
#define    MC_CMD_KR_TUNE_TXEQ_GET_OUT_LEN(num) (0+4*(num))
/* TXEQ Parameter */
#define       MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_OFST 0
#define       MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_LEN 4
#define       MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_MINNUM 1
#define       MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_MAXNUM 63
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_ID_LBN 0
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_ID_WIDTH 8
/* enum: TX Amplitude (Huntington, Medford) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_LEV  0x0
/* enum: De-Emphasis Tap1 Magnitude (0-7) (Huntington) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_MODE  0x1
/* enum: De-Emphasis Tap1 Fine */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_DTLEV  0x2
/* enum: De-Emphasis Tap2 Magnitude (0-6) (Huntington) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_D2  0x3
/* enum: De-Emphasis Tap2 Fine (Huntington) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_D2TLEV  0x4
/* enum: Pre-Emphasis Magnitude (Huntington) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_E  0x5
/* enum: Pre-Emphasis Fine (Huntington) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_ETLEV  0x6
/* enum: TX Slew Rate Coarse control (Huntington) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_PREDRV_DLY  0x7
/* enum: TX Slew Rate Fine control (Huntington) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_SR_SET  0x8
/* enum: TX Termination Impedance control (Huntington) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_RT_SET  0x9
/* enum: TX Amplitude Fine control (Medford) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TX_LEV_FINE  0xa
/* enum: Pre-shoot Tap (Medford) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TAP_ADV  0xb
/* enum: De-emphasis Tap (Medford) */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_TAP_DLY  0xc
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_LANE_LBN 8
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_LANE_WIDTH 3
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_LANE_0  0x0 /* enum */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_LANE_1  0x1 /* enum */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_LANE_2  0x2 /* enum */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_LANE_3  0x3 /* enum */
#define          MC_CMD_KR_TUNE_TXEQ_GET_OUT_LANE_ALL  0x4 /* enum */
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_RESERVED_LBN 11
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_RESERVED_WIDTH 5
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_INITIAL_LBN 16
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_PARAM_INITIAL_WIDTH 8
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_RESERVED2_LBN 24
#define        MC_CMD_KR_TUNE_TXEQ_GET_OUT_RESERVED2_WIDTH 8

/* MC_CMD_KR_TUNE_TXEQ_SET_IN msgrequest */
#define    MC_CMD_KR_TUNE_TXEQ_SET_IN_LENMIN 8
#define    MC_CMD_KR_TUNE_TXEQ_SET_IN_LENMAX 252
#define    MC_CMD_KR_TUNE_TXEQ_SET_IN_LEN(num) (4+4*(num))
/* Requested operation */
#define       MC_CMD_KR_TUNE_TXEQ_SET_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_TXEQ_SET_IN_KR_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_TXEQ_SET_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_TXEQ_SET_IN_KR_TUNE_RSVD_LEN 3
/* TXEQ Parameter */
#define       MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_OFST 4
#define       MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_LEN 4
#define       MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_MINNUM 1
#define       MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_MAXNUM 62
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_ID_LBN 0
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_ID_WIDTH 8
/*             Enum values, see field(s): */
/*                MC_CMD_KR_TUNE_TXEQ_GET_OUT/PARAM_ID */
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_LANE_LBN 8
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_LANE_WIDTH 3
/*             Enum values, see field(s): */
/*                MC_CMD_KR_TUNE_TXEQ_GET_OUT/PARAM_LANE */
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_RESERVED_LBN 11
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_RESERVED_WIDTH 5
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_INITIAL_LBN 16
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_PARAM_INITIAL_WIDTH 8
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_RESERVED2_LBN 24
#define        MC_CMD_KR_TUNE_TXEQ_SET_IN_RESERVED2_WIDTH 8

/* MC_CMD_KR_TUNE_TXEQ_SET_OUT msgresponse */
#define    MC_CMD_KR_TUNE_TXEQ_SET_OUT_LEN 0

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

/* MC_CMD_KR_TUNE_START_EYE_PLOT_IN msgrequest */
#define    MC_CMD_KR_TUNE_START_EYE_PLOT_IN_LEN 8
/* Requested operation */
#define       MC_CMD_KR_TUNE_START_EYE_PLOT_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_START_EYE_PLOT_IN_KR_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_START_EYE_PLOT_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_START_EYE_PLOT_IN_KR_TUNE_RSVD_LEN 3
#define       MC_CMD_KR_TUNE_START_EYE_PLOT_IN_LANE_OFST 4

/* MC_CMD_KR_TUNE_START_EYE_PLOT_OUT msgresponse */
#define    MC_CMD_KR_TUNE_START_EYE_PLOT_OUT_LEN 0

/* MC_CMD_KR_TUNE_POLL_EYE_PLOT_IN msgrequest */
#define    MC_CMD_KR_TUNE_POLL_EYE_PLOT_IN_LEN 4
/* Requested operation */
#define       MC_CMD_KR_TUNE_POLL_EYE_PLOT_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_POLL_EYE_PLOT_IN_KR_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_POLL_EYE_PLOT_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_POLL_EYE_PLOT_IN_KR_TUNE_RSVD_LEN 3

/* MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT msgresponse */
#define    MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT_LENMIN 0
#define    MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT_LENMAX 252
#define    MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT_LEN(num) (0+2*(num))
#define       MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT_SAMPLES_OFST 0
#define       MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT_SAMPLES_LEN 2
#define       MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT_SAMPLES_MINNUM 0
#define       MC_CMD_KR_TUNE_POLL_EYE_PLOT_OUT_SAMPLES_MAXNUM 126

/* MC_CMD_KR_TUNE_READ_FOM_IN msgrequest */
#define    MC_CMD_KR_TUNE_READ_FOM_IN_LEN 8
/* Requested operation */
#define       MC_CMD_KR_TUNE_READ_FOM_IN_KR_TUNE_OP_OFST 0
#define       MC_CMD_KR_TUNE_READ_FOM_IN_KR_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_KR_TUNE_READ_FOM_IN_KR_TUNE_RSVD_OFST 1
#define       MC_CMD_KR_TUNE_READ_FOM_IN_KR_TUNE_RSVD_LEN 3
#define       MC_CMD_KR_TUNE_READ_FOM_IN_LANE_OFST 4

/* MC_CMD_KR_TUNE_READ_FOM_OUT msgresponse */
#define    MC_CMD_KR_TUNE_READ_FOM_OUT_LEN 4
#define       MC_CMD_KR_TUNE_READ_FOM_OUT_FOM_OFST 0


/***********************************/
/* MC_CMD_PCIE_TUNE
 * Get or set PCIE Serdes RXEQ and TX Driver settings
 */
#define MC_CMD_PCIE_TUNE 0xf2

#define MC_CMD_0xf2_PRIVILEGE_CTG SRIOV_CTG_ADMIN

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
/* enum: Start PCIe Serdes Eye diagram plot on a given lane. */
#define          MC_CMD_PCIE_TUNE_IN_START_EYE_PLOT  0x5
/* enum: Poll PCIe Serdes Eye diagram plot. Returns one row of BER data. The
 * caller should call this command repeatedly after starting eye plot, until no
 * more data is returned.
 */
#define          MC_CMD_PCIE_TUNE_IN_POLL_EYE_PLOT  0x6
/* enum: Enable the SERDES BIST and set it to generate a 200MHz square wave */
#define          MC_CMD_PCIE_TUNE_IN_BIST_SQUARE_WAVE  0x7
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
/* enum: DFE DLev */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_DFE_DLEV  0x7
/* enum: Figure of Merit */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_FOM  0x8
/* enum: CTLE EQ Capacitor (HF Gain) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_CTLE_EQC  0x9
/* enum: CTLE EQ Resistor (DC Gain) */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_CTLE_EQRES  0xa
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_LANE_LBN 8
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_LANE_WIDTH 5
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_0  0x0 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_1  0x1 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_2  0x2 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_3  0x3 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_4  0x4 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_5  0x5 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_6  0x6 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_7  0x7 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_8  0x8 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_9  0x9 /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_10  0xa /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_11  0xb /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_12  0xc /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_13  0xd /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_14  0xe /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_15  0xf /* enum */
#define          MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_LANE_ALL  0x10 /* enum */
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_AUTOCAL_LBN 13
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_AUTOCAL_WIDTH 1
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_RESERVED_LBN 14
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_RESERVED_WIDTH 10
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_CURRENT_LBN 24
#define        MC_CMD_PCIE_TUNE_RXEQ_GET_OUT_PARAM_CURRENT_WIDTH 8

/* MC_CMD_PCIE_TUNE_RXEQ_SET_IN msgrequest */
#define    MC_CMD_PCIE_TUNE_RXEQ_SET_IN_LENMIN 8
#define    MC_CMD_PCIE_TUNE_RXEQ_SET_IN_LENMAX 252
#define    MC_CMD_PCIE_TUNE_RXEQ_SET_IN_LEN(num) (4+4*(num))
/* Requested operation */
#define       MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PCIE_TUNE_OP_OFST 0
#define       MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PCIE_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PCIE_TUNE_RSVD_OFST 1
#define       MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PCIE_TUNE_RSVD_LEN 3
/* RXEQ Parameter */
#define       MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_OFST 4
#define       MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_LEN 4
#define       MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_MINNUM 1
#define       MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_MAXNUM 62
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_ID_LBN 0
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_ID_WIDTH 8
/*             Enum values, see field(s): */
/*                MC_CMD_PCIE_TUNE_RXEQ_GET_OUT/PARAM_ID */
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_LANE_LBN 8
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_LANE_WIDTH 5
/*             Enum values, see field(s): */
/*                MC_CMD_PCIE_TUNE_RXEQ_GET_OUT/PARAM_LANE */
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_AUTOCAL_LBN 13
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_AUTOCAL_WIDTH 1
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_RESERVED_LBN 14
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_RESERVED_WIDTH 2
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_INITIAL_LBN 16
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_PARAM_INITIAL_WIDTH 8
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_RESERVED2_LBN 24
#define        MC_CMD_PCIE_TUNE_RXEQ_SET_IN_RESERVED2_WIDTH 8

/* MC_CMD_PCIE_TUNE_RXEQ_SET_OUT msgresponse */
#define    MC_CMD_PCIE_TUNE_RXEQ_SET_OUT_LEN 0

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

/* MC_CMD_PCIE_TUNE_START_EYE_PLOT_IN msgrequest */
#define    MC_CMD_PCIE_TUNE_START_EYE_PLOT_IN_LEN 8
/* Requested operation */
#define       MC_CMD_PCIE_TUNE_START_EYE_PLOT_IN_PCIE_TUNE_OP_OFST 0
#define       MC_CMD_PCIE_TUNE_START_EYE_PLOT_IN_PCIE_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_PCIE_TUNE_START_EYE_PLOT_IN_PCIE_TUNE_RSVD_OFST 1
#define       MC_CMD_PCIE_TUNE_START_EYE_PLOT_IN_PCIE_TUNE_RSVD_LEN 3
#define       MC_CMD_PCIE_TUNE_START_EYE_PLOT_IN_LANE_OFST 4

/* MC_CMD_PCIE_TUNE_START_EYE_PLOT_OUT msgresponse */
#define    MC_CMD_PCIE_TUNE_START_EYE_PLOT_OUT_LEN 0

/* MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_IN msgrequest */
#define    MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_IN_LEN 4
/* Requested operation */
#define       MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_IN_PCIE_TUNE_OP_OFST 0
#define       MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_IN_PCIE_TUNE_OP_LEN 1
/* Align the arguments to 32 bits */
#define       MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_IN_PCIE_TUNE_RSVD_OFST 1
#define       MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_IN_PCIE_TUNE_RSVD_LEN 3

/* MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_OUT msgresponse */
#define    MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_OUT_LENMIN 0
#define    MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_OUT_LENMAX 252
#define    MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_OUT_LEN(num) (0+2*(num))
#define       MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_OUT_SAMPLES_OFST 0
#define       MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_OUT_SAMPLES_LEN 2
#define       MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_OUT_SAMPLES_MINNUM 0
#define       MC_CMD_PCIE_TUNE_POLL_EYE_PLOT_OUT_SAMPLES_MAXNUM 126

/* MC_CMD_PCIE_TUNE_BIST_SQUARE_WAVE_IN msgrequest */
#define    MC_CMD_PCIE_TUNE_BIST_SQUARE_WAVE_IN_LEN 0

/* MC_CMD_PCIE_TUNE_BIST_SQUARE_WAVE_OUT msgrequest */
#define    MC_CMD_PCIE_TUNE_BIST_SQUARE_WAVE_OUT_LEN 0


/***********************************/
/* MC_CMD_LICENSING
 * Operations on the NVRAM_PARTITION_TYPE_LICENSE application license partition
 * - not used for V3 licensing
 */
#define MC_CMD_LICENSING 0xf3

#define MC_CMD_0xf3_PRIVILEGE_CTG SRIOV_CTG_GENERAL

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
/* MC_CMD_LICENSING_V3
 * Operations on the NVRAM_PARTITION_TYPE_LICENSE application license partition
 * - V3 licensing (Medford)
 */
#define MC_CMD_LICENSING_V3 0xd0

#define MC_CMD_0xd0_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_LICENSING_V3_IN msgrequest */
#define    MC_CMD_LICENSING_V3_IN_LEN 4
/* identifies the type of operation requested */
#define       MC_CMD_LICENSING_V3_IN_OP_OFST 0
/* enum: re-read and apply licenses after a license key partition update; note
 * that this operation returns a zero-length response
 */
#define          MC_CMD_LICENSING_V3_IN_OP_UPDATE_LICENSE  0x0
/* enum: report counts of installed licenses Returns EAGAIN if license
 * processing (updating) has been started but not yet completed.
 */
#define          MC_CMD_LICENSING_V3_IN_OP_REPORT_LICENSE  0x1

/* MC_CMD_LICENSING_V3_OUT msgresponse */
#define    MC_CMD_LICENSING_V3_OUT_LEN 88
/* count of keys which are valid */
#define       MC_CMD_LICENSING_V3_OUT_VALID_KEYS_OFST 0
/* sum of UNVERIFIABLE_KEYS + WRONG_NODE_KEYS (for compatibility with
 * MC_CMD_FC_OP_LICENSE)
 */
#define       MC_CMD_LICENSING_V3_OUT_INVALID_KEYS_OFST 4
/* count of keys which are invalid due to being unverifiable */
#define       MC_CMD_LICENSING_V3_OUT_UNVERIFIABLE_KEYS_OFST 8
/* count of keys which are invalid due to being for the wrong node */
#define       MC_CMD_LICENSING_V3_OUT_WRONG_NODE_KEYS_OFST 12
/* licensing state (for diagnostics; the exact meaning of the bits in this
 * field are private to the firmware)
 */
#define       MC_CMD_LICENSING_V3_OUT_LICENSING_STATE_OFST 16
/* licensing subsystem self-test report (for manftest) */
#define       MC_CMD_LICENSING_V3_OUT_LICENSING_SELF_TEST_OFST 20
/* enum: licensing subsystem self-test failed */
#define          MC_CMD_LICENSING_V3_OUT_SELF_TEST_FAIL  0x0
/* enum: licensing subsystem self-test passed */
#define          MC_CMD_LICENSING_V3_OUT_SELF_TEST_PASS  0x1
/* bitmask of licensed applications */
#define       MC_CMD_LICENSING_V3_OUT_LICENSED_APPS_OFST 24
#define       MC_CMD_LICENSING_V3_OUT_LICENSED_APPS_LEN 8
#define       MC_CMD_LICENSING_V3_OUT_LICENSED_APPS_LO_OFST 24
#define       MC_CMD_LICENSING_V3_OUT_LICENSED_APPS_HI_OFST 28
/* reserved for future use */
#define       MC_CMD_LICENSING_V3_OUT_RESERVED_0_OFST 32
#define       MC_CMD_LICENSING_V3_OUT_RESERVED_0_LEN 24
/* bitmask of licensed features */
#define       MC_CMD_LICENSING_V3_OUT_LICENSED_FEATURES_OFST 56
#define       MC_CMD_LICENSING_V3_OUT_LICENSED_FEATURES_LEN 8
#define       MC_CMD_LICENSING_V3_OUT_LICENSED_FEATURES_LO_OFST 56
#define       MC_CMD_LICENSING_V3_OUT_LICENSED_FEATURES_HI_OFST 60
/* reserved for future use */
#define       MC_CMD_LICENSING_V3_OUT_RESERVED_1_OFST 64
#define       MC_CMD_LICENSING_V3_OUT_RESERVED_1_LEN 24


/***********************************/
/* MC_CMD_LICENSING_GET_ID_V3
 * Get ID and type from the NVRAM_PARTITION_TYPE_LICENSE application license
 * partition - V3 licensing (Medford)
 */
#define MC_CMD_LICENSING_GET_ID_V3 0xd1

#define MC_CMD_0xd1_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_LICENSING_GET_ID_V3_IN msgrequest */
#define    MC_CMD_LICENSING_GET_ID_V3_IN_LEN 0

/* MC_CMD_LICENSING_GET_ID_V3_OUT msgresponse */
#define    MC_CMD_LICENSING_GET_ID_V3_OUT_LENMIN 8
#define    MC_CMD_LICENSING_GET_ID_V3_OUT_LENMAX 252
#define    MC_CMD_LICENSING_GET_ID_V3_OUT_LEN(num) (8+1*(num))
/* type of license (eg 3) */
#define       MC_CMD_LICENSING_GET_ID_V3_OUT_LICENSE_TYPE_OFST 0
/* length of the license ID (in bytes) */
#define       MC_CMD_LICENSING_GET_ID_V3_OUT_LICENSE_ID_LENGTH_OFST 4
/* the unique license ID of the adapter */
#define       MC_CMD_LICENSING_GET_ID_V3_OUT_LICENSE_ID_OFST 8
#define       MC_CMD_LICENSING_GET_ID_V3_OUT_LICENSE_ID_LEN 1
#define       MC_CMD_LICENSING_GET_ID_V3_OUT_LICENSE_ID_MINNUM 0
#define       MC_CMD_LICENSING_GET_ID_V3_OUT_LICENSE_ID_MAXNUM 244


/***********************************/
/* MC_CMD_MC2MC_PROXY
 * Execute an arbitrary MCDI command on the slave MC of a dual-core device.
 * This will fail on a single-core system.
 */
#define MC_CMD_MC2MC_PROXY 0xf4

#define MC_CMD_0xf4_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_MC2MC_PROXY_IN msgrequest */
#define    MC_CMD_MC2MC_PROXY_IN_LEN 0

/* MC_CMD_MC2MC_PROXY_OUT msgresponse */
#define    MC_CMD_MC2MC_PROXY_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_LICENSED_APP_STATE
 * Query the state of an individual licensed application. (Note that the actual
 * state may be invalidated by the MC_CMD_LICENSING OP_UPDATE_LICENSE operation
 * or a reboot of the MC.) Not used for V3 licensing
 */
#define MC_CMD_GET_LICENSED_APP_STATE 0xf5

#define MC_CMD_0xf5_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_LICENSED_APP_STATE_IN msgrequest */
#define    MC_CMD_GET_LICENSED_APP_STATE_IN_LEN 4
/* application ID to query (LICENSED_APP_ID_xxx) */
#define       MC_CMD_GET_LICENSED_APP_STATE_IN_APP_ID_OFST 0

/* MC_CMD_GET_LICENSED_APP_STATE_OUT msgresponse */
#define    MC_CMD_GET_LICENSED_APP_STATE_OUT_LEN 4
/* state of this application */
#define       MC_CMD_GET_LICENSED_APP_STATE_OUT_STATE_OFST 0
/* enum: no (or invalid) license is present for the application */
#define          MC_CMD_GET_LICENSED_APP_STATE_OUT_NOT_LICENSED  0x0
/* enum: a valid license is present for the application */
#define          MC_CMD_GET_LICENSED_APP_STATE_OUT_LICENSED  0x1


/***********************************/
/* MC_CMD_GET_LICENSED_V3_APP_STATE
 * Query the state of an individual licensed application. (Note that the actual
 * state may be invalidated by the MC_CMD_LICENSING_V3 OP_UPDATE_LICENSE
 * operation or a reboot of the MC.) Used for V3 licensing (Medford)
 */
#define MC_CMD_GET_LICENSED_V3_APP_STATE 0xd2

#define MC_CMD_0xd2_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_LICENSED_V3_APP_STATE_IN msgrequest */
#define    MC_CMD_GET_LICENSED_V3_APP_STATE_IN_LEN 8
/* application ID to query (LICENSED_V3_APPS_xxx) expressed as a single bit
 * mask
 */
#define       MC_CMD_GET_LICENSED_V3_APP_STATE_IN_APP_ID_OFST 0
#define       MC_CMD_GET_LICENSED_V3_APP_STATE_IN_APP_ID_LEN 8
#define       MC_CMD_GET_LICENSED_V3_APP_STATE_IN_APP_ID_LO_OFST 0
#define       MC_CMD_GET_LICENSED_V3_APP_STATE_IN_APP_ID_HI_OFST 4

/* MC_CMD_GET_LICENSED_V3_APP_STATE_OUT msgresponse */
#define    MC_CMD_GET_LICENSED_V3_APP_STATE_OUT_LEN 4
/* state of this application */
#define       MC_CMD_GET_LICENSED_V3_APP_STATE_OUT_STATE_OFST 0
/* enum: no (or invalid) license is present for the application */
#define          MC_CMD_GET_LICENSED_V3_APP_STATE_OUT_NOT_LICENSED  0x0
/* enum: a valid license is present for the application */
#define          MC_CMD_GET_LICENSED_V3_APP_STATE_OUT_LICENSED  0x1


/***********************************/
/* MC_CMD_GET_LICENSED_V3_FEATURE_STATES
 * Query the state of one or more licensed features. (Note that the actual
 * state may be invalidated by the MC_CMD_LICENSING_V3 OP_UPDATE_LICENSE
 * operation or a reboot of the MC.) Used for V3 licensing (Medford)
 */
#define MC_CMD_GET_LICENSED_V3_FEATURE_STATES 0xd3

#define MC_CMD_0xd3_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_LICENSED_V3_FEATURE_STATES_IN msgrequest */
#define    MC_CMD_GET_LICENSED_V3_FEATURE_STATES_IN_LEN 8
/* features to query (LICENSED_V3_FEATURES_xxx) expressed as a mask with one or
 * more bits set
 */
#define       MC_CMD_GET_LICENSED_V3_FEATURE_STATES_IN_FEATURES_OFST 0
#define       MC_CMD_GET_LICENSED_V3_FEATURE_STATES_IN_FEATURES_LEN 8
#define       MC_CMD_GET_LICENSED_V3_FEATURE_STATES_IN_FEATURES_LO_OFST 0
#define       MC_CMD_GET_LICENSED_V3_FEATURE_STATES_IN_FEATURES_HI_OFST 4

/* MC_CMD_GET_LICENSED_V3_FEATURE_STATES_OUT msgresponse */
#define    MC_CMD_GET_LICENSED_V3_FEATURE_STATES_OUT_LEN 8
/* states of these features - bit set for licensed, clear for not licensed */
#define       MC_CMD_GET_LICENSED_V3_FEATURE_STATES_OUT_STATES_OFST 0
#define       MC_CMD_GET_LICENSED_V3_FEATURE_STATES_OUT_STATES_LEN 8
#define       MC_CMD_GET_LICENSED_V3_FEATURE_STATES_OUT_STATES_LO_OFST 0
#define       MC_CMD_GET_LICENSED_V3_FEATURE_STATES_OUT_STATES_HI_OFST 4


/***********************************/
/* MC_CMD_LICENSED_APP_OP
 * Perform an action for an individual licensed application - not used for V3
 * licensing.
 */
#define MC_CMD_LICENSED_APP_OP 0xf6

#define MC_CMD_0xf6_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_LICENSED_APP_OP_IN msgrequest */
#define    MC_CMD_LICENSED_APP_OP_IN_LENMIN 8
#define    MC_CMD_LICENSED_APP_OP_IN_LENMAX 252
#define    MC_CMD_LICENSED_APP_OP_IN_LEN(num) (8+4*(num))
/* application ID */
#define       MC_CMD_LICENSED_APP_OP_IN_APP_ID_OFST 0
/* the type of operation requested */
#define       MC_CMD_LICENSED_APP_OP_IN_OP_OFST 4
/* enum: validate application */
#define          MC_CMD_LICENSED_APP_OP_IN_OP_VALIDATE  0x0
/* enum: mask application */
#define          MC_CMD_LICENSED_APP_OP_IN_OP_MASK  0x1
/* arguments specific to this particular operation */
#define       MC_CMD_LICENSED_APP_OP_IN_ARGS_OFST 8
#define       MC_CMD_LICENSED_APP_OP_IN_ARGS_LEN 4
#define       MC_CMD_LICENSED_APP_OP_IN_ARGS_MINNUM 0
#define       MC_CMD_LICENSED_APP_OP_IN_ARGS_MAXNUM 61

/* MC_CMD_LICENSED_APP_OP_OUT msgresponse */
#define    MC_CMD_LICENSED_APP_OP_OUT_LENMIN 0
#define    MC_CMD_LICENSED_APP_OP_OUT_LENMAX 252
#define    MC_CMD_LICENSED_APP_OP_OUT_LEN(num) (0+4*(num))
/* result specific to this particular operation */
#define       MC_CMD_LICENSED_APP_OP_OUT_RESULT_OFST 0
#define       MC_CMD_LICENSED_APP_OP_OUT_RESULT_LEN 4
#define       MC_CMD_LICENSED_APP_OP_OUT_RESULT_MINNUM 0
#define       MC_CMD_LICENSED_APP_OP_OUT_RESULT_MAXNUM 63

/* MC_CMD_LICENSED_APP_OP_VALIDATE_IN msgrequest */
#define    MC_CMD_LICENSED_APP_OP_VALIDATE_IN_LEN 72
/* application ID */
#define       MC_CMD_LICENSED_APP_OP_VALIDATE_IN_APP_ID_OFST 0
/* the type of operation requested */
#define       MC_CMD_LICENSED_APP_OP_VALIDATE_IN_OP_OFST 4
/* validation challenge */
#define       MC_CMD_LICENSED_APP_OP_VALIDATE_IN_CHALLENGE_OFST 8
#define       MC_CMD_LICENSED_APP_OP_VALIDATE_IN_CHALLENGE_LEN 64

/* MC_CMD_LICENSED_APP_OP_VALIDATE_OUT msgresponse */
#define    MC_CMD_LICENSED_APP_OP_VALIDATE_OUT_LEN 68
/* feature expiry (time_t) */
#define       MC_CMD_LICENSED_APP_OP_VALIDATE_OUT_EXPIRY_OFST 0
/* validation response */
#define       MC_CMD_LICENSED_APP_OP_VALIDATE_OUT_RESPONSE_OFST 4
#define       MC_CMD_LICENSED_APP_OP_VALIDATE_OUT_RESPONSE_LEN 64

/* MC_CMD_LICENSED_APP_OP_MASK_IN msgrequest */
#define    MC_CMD_LICENSED_APP_OP_MASK_IN_LEN 12
/* application ID */
#define       MC_CMD_LICENSED_APP_OP_MASK_IN_APP_ID_OFST 0
/* the type of operation requested */
#define       MC_CMD_LICENSED_APP_OP_MASK_IN_OP_OFST 4
/* flag */
#define       MC_CMD_LICENSED_APP_OP_MASK_IN_FLAG_OFST 8

/* MC_CMD_LICENSED_APP_OP_MASK_OUT msgresponse */
#define    MC_CMD_LICENSED_APP_OP_MASK_OUT_LEN 0


/***********************************/
/* MC_CMD_LICENSED_V3_VALIDATE_APP
 * Perform validation for an individual licensed application - V3 licensing
 * (Medford)
 */
#define MC_CMD_LICENSED_V3_VALIDATE_APP 0xd4

#define MC_CMD_0xd4_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_LICENSED_V3_VALIDATE_APP_IN msgrequest */
#define    MC_CMD_LICENSED_V3_VALIDATE_APP_IN_LEN 56
/* challenge for validation (384 bits) */
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_IN_CHALLENGE_OFST 0
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_IN_CHALLENGE_LEN 48
/* application ID expressed as a single bit mask */
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_IN_APP_ID_OFST 48
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_IN_APP_ID_LEN 8
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_IN_APP_ID_LO_OFST 48
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_IN_APP_ID_HI_OFST 52

/* MC_CMD_LICENSED_V3_VALIDATE_APP_OUT msgresponse */
#define    MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_LEN 116
/* validation response to challenge in the form of ECDSA signature consisting
 * of two 384-bit integers, r and s, in big-endian order. The signature signs a
 * SHA-384 digest of a message constructed from the concatenation of the input
 * message and the remaining fields of this output message, e.g. challenge[48
 * bytes] ... expiry_time[4 bytes] ...
 */
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_RESPONSE_OFST 0
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_RESPONSE_LEN 96
/* application expiry time */
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_EXPIRY_TIME_OFST 96
/* application expiry units */
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_EXPIRY_UNITS_OFST 100
/* enum: expiry units are accounting units */
#define          MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_EXPIRY_UNIT_ACC  0x0
/* enum: expiry units are calendar days */
#define          MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_EXPIRY_UNIT_DAYS  0x1
/* base MAC address of the NIC stored in NVRAM (note that this is a constant
 * value for a given NIC regardless which function is calling, effectively this
 * is PF0 base MAC address)
 */
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_BASE_MACADDR_OFST 104
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_BASE_MACADDR_LEN 6
/* MAC address of v-adaptor associated with the client. If no such v-adapator
 * exists, then the field is filled with 0xFF.
 */
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_VADAPTOR_MACADDR_OFST 110
#define       MC_CMD_LICENSED_V3_VALIDATE_APP_OUT_VADAPTOR_MACADDR_LEN 6


/***********************************/
/* MC_CMD_LICENSED_V3_MASK_FEATURES
 * Mask features - V3 licensing (Medford)
 */
#define MC_CMD_LICENSED_V3_MASK_FEATURES 0xd5

#define MC_CMD_0xd5_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_LICENSED_V3_MASK_FEATURES_IN msgrequest */
#define    MC_CMD_LICENSED_V3_MASK_FEATURES_IN_LEN 12
/* mask to be applied to features to be changed */
#define       MC_CMD_LICENSED_V3_MASK_FEATURES_IN_MASK_OFST 0
#define       MC_CMD_LICENSED_V3_MASK_FEATURES_IN_MASK_LEN 8
#define       MC_CMD_LICENSED_V3_MASK_FEATURES_IN_MASK_LO_OFST 0
#define       MC_CMD_LICENSED_V3_MASK_FEATURES_IN_MASK_HI_OFST 4
/* whether to turn on or turn off the masked features */
#define       MC_CMD_LICENSED_V3_MASK_FEATURES_IN_FLAG_OFST 8
/* enum: turn the features off */
#define          MC_CMD_LICENSED_V3_MASK_FEATURES_IN_OFF  0x0
/* enum: turn the features back on */
#define          MC_CMD_LICENSED_V3_MASK_FEATURES_IN_ON  0x1

/* MC_CMD_LICENSED_V3_MASK_FEATURES_OUT msgresponse */
#define    MC_CMD_LICENSED_V3_MASK_FEATURES_OUT_LEN 0


/***********************************/
/* MC_CMD_LICENSING_V3_TEMPORARY
 * Perform operations to support installation of a single temporary license in
 * the adapter, in addition to those found in the licensing partition. See
 * SF-116124-SW for an overview of how this could be used. The license is
 * stored in MC persistent data and so will survive a MC reboot, but will be
 * erased when the adapter is power cycled
 */
#define MC_CMD_LICENSING_V3_TEMPORARY 0xd6

#define MC_CMD_0xd6_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_LICENSING_V3_TEMPORARY_IN msgrequest */
#define    MC_CMD_LICENSING_V3_TEMPORARY_IN_LEN 4
/* operation code */
#define       MC_CMD_LICENSING_V3_TEMPORARY_IN_OP_OFST 0
/* enum: install a new license, overwriting any existing temporary license.
 * This is an asynchronous operation owing to the time taken to validate an
 * ECDSA license
 */
#define          MC_CMD_LICENSING_V3_TEMPORARY_SET  0x0
/* enum: clear the license immediately rather than waiting for the next power
 * cycle
 */
#define          MC_CMD_LICENSING_V3_TEMPORARY_CLEAR  0x1
/* enum: get the status of the asynchronous MC_CMD_LICENSING_V3_TEMPORARY_SET
 * operation
 */
#define          MC_CMD_LICENSING_V3_TEMPORARY_STATUS  0x2

/* MC_CMD_LICENSING_V3_TEMPORARY_IN_SET msgrequest */
#define    MC_CMD_LICENSING_V3_TEMPORARY_IN_SET_LEN 164
#define       MC_CMD_LICENSING_V3_TEMPORARY_IN_SET_OP_OFST 0
/* ECDSA license and signature */
#define       MC_CMD_LICENSING_V3_TEMPORARY_IN_SET_LICENSE_OFST 4
#define       MC_CMD_LICENSING_V3_TEMPORARY_IN_SET_LICENSE_LEN 160

/* MC_CMD_LICENSING_V3_TEMPORARY_IN_CLEAR msgrequest */
#define    MC_CMD_LICENSING_V3_TEMPORARY_IN_CLEAR_LEN 4
#define       MC_CMD_LICENSING_V3_TEMPORARY_IN_CLEAR_OP_OFST 0

/* MC_CMD_LICENSING_V3_TEMPORARY_IN_STATUS msgrequest */
#define    MC_CMD_LICENSING_V3_TEMPORARY_IN_STATUS_LEN 4
#define       MC_CMD_LICENSING_V3_TEMPORARY_IN_STATUS_OP_OFST 0

/* MC_CMD_LICENSING_V3_TEMPORARY_OUT_STATUS msgresponse */
#define    MC_CMD_LICENSING_V3_TEMPORARY_OUT_STATUS_LEN 12
/* status code */
#define       MC_CMD_LICENSING_V3_TEMPORARY_OUT_STATUS_STATUS_OFST 0
/* enum: finished validating and installing license */
#define          MC_CMD_LICENSING_V3_TEMPORARY_STATUS_OK  0x0
/* enum: license validation and installation in progress */
#define          MC_CMD_LICENSING_V3_TEMPORARY_STATUS_IN_PROGRESS  0x1
/* enum: licensing error. More specific error messages are not provided to
 * avoid exposing details of the licensing system to the client
 */
#define          MC_CMD_LICENSING_V3_TEMPORARY_STATUS_ERROR  0x2
/* bitmask of licensed features */
#define       MC_CMD_LICENSING_V3_TEMPORARY_OUT_STATUS_LICENSED_FEATURES_OFST 4
#define       MC_CMD_LICENSING_V3_TEMPORARY_OUT_STATUS_LICENSED_FEATURES_LEN 8
#define       MC_CMD_LICENSING_V3_TEMPORARY_OUT_STATUS_LICENSED_FEATURES_LO_OFST 4
#define       MC_CMD_LICENSING_V3_TEMPORARY_OUT_STATUS_LICENSED_FEATURES_HI_OFST 8


/***********************************/
/* MC_CMD_SET_PORT_SNIFF_CONFIG
 * Configure RX port sniffing for the physical port associated with the calling
 * function. Only a privileged function may change the port sniffing
 * configuration. A copy of all traffic delivered to the host (non-promiscuous
 * mode) or all traffic arriving at the port (promiscuous mode) may be
 * delivered to a specific queue, or a set of queues with RSS.
 */
#define MC_CMD_SET_PORT_SNIFF_CONFIG 0xf7

#define MC_CMD_0xf7_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_SET_PORT_SNIFF_CONFIG_IN msgrequest */
#define    MC_CMD_SET_PORT_SNIFF_CONFIG_IN_LEN 16
/* configuration flags */
#define       MC_CMD_SET_PORT_SNIFF_CONFIG_IN_FLAGS_OFST 0
#define        MC_CMD_SET_PORT_SNIFF_CONFIG_IN_ENABLE_LBN 0
#define        MC_CMD_SET_PORT_SNIFF_CONFIG_IN_ENABLE_WIDTH 1
#define        MC_CMD_SET_PORT_SNIFF_CONFIG_IN_PROMISCUOUS_LBN 1
#define        MC_CMD_SET_PORT_SNIFF_CONFIG_IN_PROMISCUOUS_WIDTH 1
/* receive queue handle (for RSS mode, this is the base queue) */
#define       MC_CMD_SET_PORT_SNIFF_CONFIG_IN_RX_QUEUE_OFST 4
/* receive mode */
#define       MC_CMD_SET_PORT_SNIFF_CONFIG_IN_RX_MODE_OFST 8
/* enum: receive to just the specified queue */
#define          MC_CMD_SET_PORT_SNIFF_CONFIG_IN_RX_MODE_SIMPLE  0x0
/* enum: receive to multiple queues using RSS context */
#define          MC_CMD_SET_PORT_SNIFF_CONFIG_IN_RX_MODE_RSS  0x1
/* RSS context (for RX_MODE_RSS) as returned by MC_CMD_RSS_CONTEXT_ALLOC. Note
 * that these handles should be considered opaque to the host, although a value
 * of 0xFFFFFFFF is guaranteed never to be a valid handle.
 */
#define       MC_CMD_SET_PORT_SNIFF_CONFIG_IN_RX_CONTEXT_OFST 12

/* MC_CMD_SET_PORT_SNIFF_CONFIG_OUT msgresponse */
#define    MC_CMD_SET_PORT_SNIFF_CONFIG_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_PORT_SNIFF_CONFIG
 * Obtain the current RX port sniffing configuration for the physical port
 * associated with the calling function. Only a privileged function may read
 * the configuration.
 */
#define MC_CMD_GET_PORT_SNIFF_CONFIG 0xf8

#define MC_CMD_0xf8_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_GET_PORT_SNIFF_CONFIG_IN msgrequest */
#define    MC_CMD_GET_PORT_SNIFF_CONFIG_IN_LEN 0

/* MC_CMD_GET_PORT_SNIFF_CONFIG_OUT msgresponse */
#define    MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_LEN 16
/* configuration flags */
#define       MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_FLAGS_OFST 0
#define        MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_ENABLE_LBN 0
#define        MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_ENABLE_WIDTH 1
#define        MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_PROMISCUOUS_LBN 1
#define        MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_PROMISCUOUS_WIDTH 1
/* receiving queue handle (for RSS mode, this is the base queue) */
#define       MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_RX_QUEUE_OFST 4
/* receive mode */
#define       MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_RX_MODE_OFST 8
/* enum: receiving to just the specified queue */
#define          MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_RX_MODE_SIMPLE  0x0
/* enum: receiving to multiple queues using RSS context */
#define          MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_RX_MODE_RSS  0x1
/* RSS context (for RX_MODE_RSS) */
#define       MC_CMD_GET_PORT_SNIFF_CONFIG_OUT_RX_CONTEXT_OFST 12


/***********************************/
/* MC_CMD_SET_PARSER_DISP_CONFIG
 * Change configuration related to the parser-dispatcher subsystem.
 */
#define MC_CMD_SET_PARSER_DISP_CONFIG 0xf9

#define MC_CMD_0xf9_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_SET_PARSER_DISP_CONFIG_IN msgrequest */
#define    MC_CMD_SET_PARSER_DISP_CONFIG_IN_LENMIN 12
#define    MC_CMD_SET_PARSER_DISP_CONFIG_IN_LENMAX 252
#define    MC_CMD_SET_PARSER_DISP_CONFIG_IN_LEN(num) (8+4*(num))
/* the type of configuration setting to change */
#define       MC_CMD_SET_PARSER_DISP_CONFIG_IN_TYPE_OFST 0
/* enum: Per-TXQ enable for multicast UDP destination lookup for possible
 * internal loopback. (ENTITY is a queue handle, VALUE is a single boolean.)
 */
#define          MC_CMD_SET_PARSER_DISP_CONFIG_IN_TXQ_MCAST_UDP_DST_LOOKUP_EN  0x0
/* enum: Per-v-adaptor enable for suppression of self-transmissions on the
 * internal loopback path. (ENTITY is an EVB_PORT_ID, VALUE is a single
 * boolean.)
 */
#define          MC_CMD_SET_PARSER_DISP_CONFIG_IN_VADAPTOR_SUPPRESS_SELF_TX  0x1
/* handle for the entity to update: queue handle, EVB port ID, etc. depending
 * on the type of configuration setting being changed
 */
#define       MC_CMD_SET_PARSER_DISP_CONFIG_IN_ENTITY_OFST 4
/* new value: the details depend on the type of configuration setting being
 * changed
 */
#define       MC_CMD_SET_PARSER_DISP_CONFIG_IN_VALUE_OFST 8
#define       MC_CMD_SET_PARSER_DISP_CONFIG_IN_VALUE_LEN 4
#define       MC_CMD_SET_PARSER_DISP_CONFIG_IN_VALUE_MINNUM 1
#define       MC_CMD_SET_PARSER_DISP_CONFIG_IN_VALUE_MAXNUM 61

/* MC_CMD_SET_PARSER_DISP_CONFIG_OUT msgresponse */
#define    MC_CMD_SET_PARSER_DISP_CONFIG_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_PARSER_DISP_CONFIG
 * Read configuration related to the parser-dispatcher subsystem.
 */
#define MC_CMD_GET_PARSER_DISP_CONFIG 0xfa

#define MC_CMD_0xfa_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_PARSER_DISP_CONFIG_IN msgrequest */
#define    MC_CMD_GET_PARSER_DISP_CONFIG_IN_LEN 8
/* the type of configuration setting to read */
#define       MC_CMD_GET_PARSER_DISP_CONFIG_IN_TYPE_OFST 0
/*            Enum values, see field(s): */
/*               MC_CMD_SET_PARSER_DISP_CONFIG/MC_CMD_SET_PARSER_DISP_CONFIG_IN/TYPE */
/* handle for the entity to query: queue handle, EVB port ID, etc. depending on
 * the type of configuration setting being read
 */
#define       MC_CMD_GET_PARSER_DISP_CONFIG_IN_ENTITY_OFST 4

/* MC_CMD_GET_PARSER_DISP_CONFIG_OUT msgresponse */
#define    MC_CMD_GET_PARSER_DISP_CONFIG_OUT_LENMIN 4
#define    MC_CMD_GET_PARSER_DISP_CONFIG_OUT_LENMAX 252
#define    MC_CMD_GET_PARSER_DISP_CONFIG_OUT_LEN(num) (0+4*(num))
/* current value: the details depend on the type of configuration setting being
 * read
 */
#define       MC_CMD_GET_PARSER_DISP_CONFIG_OUT_VALUE_OFST 0
#define       MC_CMD_GET_PARSER_DISP_CONFIG_OUT_VALUE_LEN 4
#define       MC_CMD_GET_PARSER_DISP_CONFIG_OUT_VALUE_MINNUM 1
#define       MC_CMD_GET_PARSER_DISP_CONFIG_OUT_VALUE_MAXNUM 63


/***********************************/
/* MC_CMD_SET_TX_PORT_SNIFF_CONFIG
 * Configure TX port sniffing for the physical port associated with the calling
 * function. Only a privileged function may change the port sniffing
 * configuration. A copy of all traffic transmitted through the port may be
 * delivered to a specific queue, or a set of queues with RSS. Note that these
 * packets are delivered with transmit timestamps in the packet prefix, not
 * receive timestamps, so it is likely that the queue(s) will need to be
 * dedicated as TX sniff receivers.
 */
#define MC_CMD_SET_TX_PORT_SNIFF_CONFIG 0xfb

#define MC_CMD_0xfb_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN msgrequest */
#define    MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_LEN 16
/* configuration flags */
#define       MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_FLAGS_OFST 0
#define        MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_ENABLE_LBN 0
#define        MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_ENABLE_WIDTH 1
/* receive queue handle (for RSS mode, this is the base queue) */
#define       MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_RX_QUEUE_OFST 4
/* receive mode */
#define       MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_RX_MODE_OFST 8
/* enum: receive to just the specified queue */
#define          MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_RX_MODE_SIMPLE  0x0
/* enum: receive to multiple queues using RSS context */
#define          MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_RX_MODE_RSS  0x1
/* RSS context (for RX_MODE_RSS) as returned by MC_CMD_RSS_CONTEXT_ALLOC. Note
 * that these handles should be considered opaque to the host, although a value
 * of 0xFFFFFFFF is guaranteed never to be a valid handle.
 */
#define       MC_CMD_SET_TX_PORT_SNIFF_CONFIG_IN_RX_CONTEXT_OFST 12

/* MC_CMD_SET_TX_PORT_SNIFF_CONFIG_OUT msgresponse */
#define    MC_CMD_SET_TX_PORT_SNIFF_CONFIG_OUT_LEN 0


/***********************************/
/* MC_CMD_GET_TX_PORT_SNIFF_CONFIG
 * Obtain the current TX port sniffing configuration for the physical port
 * associated with the calling function. Only a privileged function may read
 * the configuration.
 */
#define MC_CMD_GET_TX_PORT_SNIFF_CONFIG 0xfc

#define MC_CMD_0xfc_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_GET_TX_PORT_SNIFF_CONFIG_IN msgrequest */
#define    MC_CMD_GET_TX_PORT_SNIFF_CONFIG_IN_LEN 0

/* MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT msgresponse */
#define    MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_LEN 16
/* configuration flags */
#define       MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_FLAGS_OFST 0
#define        MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_ENABLE_LBN 0
#define        MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_ENABLE_WIDTH 1
/* receiving queue handle (for RSS mode, this is the base queue) */
#define       MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_RX_QUEUE_OFST 4
/* receive mode */
#define       MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_RX_MODE_OFST 8
/* enum: receiving to just the specified queue */
#define          MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_RX_MODE_SIMPLE  0x0
/* enum: receiving to multiple queues using RSS context */
#define          MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_RX_MODE_RSS  0x1
/* RSS context (for RX_MODE_RSS) */
#define       MC_CMD_GET_TX_PORT_SNIFF_CONFIG_OUT_RX_CONTEXT_OFST 12


/***********************************/
/* MC_CMD_RMON_STATS_RX_ERRORS
 * Per queue rx error stats.
 */
#define MC_CMD_RMON_STATS_RX_ERRORS 0xfe

#define MC_CMD_0xfe_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_RMON_STATS_RX_ERRORS_IN msgrequest */
#define    MC_CMD_RMON_STATS_RX_ERRORS_IN_LEN 8
/* The rx queue to get stats for. */
#define       MC_CMD_RMON_STATS_RX_ERRORS_IN_RX_QUEUE_OFST 0
#define       MC_CMD_RMON_STATS_RX_ERRORS_IN_FLAGS_OFST 4
#define        MC_CMD_RMON_STATS_RX_ERRORS_IN_RST_LBN 0
#define        MC_CMD_RMON_STATS_RX_ERRORS_IN_RST_WIDTH 1

/* MC_CMD_RMON_STATS_RX_ERRORS_OUT msgresponse */
#define    MC_CMD_RMON_STATS_RX_ERRORS_OUT_LEN 16
#define       MC_CMD_RMON_STATS_RX_ERRORS_OUT_CRC_ERRORS_OFST 0
#define       MC_CMD_RMON_STATS_RX_ERRORS_OUT_TRUNC_ERRORS_OFST 4
#define       MC_CMD_RMON_STATS_RX_ERRORS_OUT_RX_NO_DESC_DROPS_OFST 8
#define       MC_CMD_RMON_STATS_RX_ERRORS_OUT_RX_ABORT_OFST 12


/***********************************/
/* MC_CMD_GET_PCIE_RESOURCE_INFO
 * Find out about available PCIE resources
 */
#define MC_CMD_GET_PCIE_RESOURCE_INFO 0xfd

/* MC_CMD_GET_PCIE_RESOURCE_INFO_IN msgrequest */
#define    MC_CMD_GET_PCIE_RESOURCE_INFO_IN_LEN 0

/* MC_CMD_GET_PCIE_RESOURCE_INFO_OUT msgresponse */
#define    MC_CMD_GET_PCIE_RESOURCE_INFO_OUT_LEN 28
/* The maximum number of PFs the device can expose */
#define       MC_CMD_GET_PCIE_RESOURCE_INFO_OUT_MAX_PFS_OFST 0
/* The maximum number of VFs the device can expose in total */
#define       MC_CMD_GET_PCIE_RESOURCE_INFO_OUT_MAX_VFS_OFST 4
/* The maximum number of MSI-X vectors the device can provide in total */
#define       MC_CMD_GET_PCIE_RESOURCE_INFO_OUT_MAX_VECTORS_OFST 8
/* the number of MSI-X vectors the device will allocate by default to each PF
 */
#define       MC_CMD_GET_PCIE_RESOURCE_INFO_OUT_DEFAULT_PF_VECTORS_OFST 12
/* the number of MSI-X vectors the device will allocate by default to each VF
 */
#define       MC_CMD_GET_PCIE_RESOURCE_INFO_OUT_DEFAULT_VF_VECTORS_OFST 16
/* the maximum number of MSI-X vectors the device can allocate to any one PF */
#define       MC_CMD_GET_PCIE_RESOURCE_INFO_OUT_MAX_PF_VECTORS_OFST 20
/* the maximum number of MSI-X vectors the device can allocate to any one VF */
#define       MC_CMD_GET_PCIE_RESOURCE_INFO_OUT_MAX_VF_VECTORS_OFST 24


/***********************************/
/* MC_CMD_GET_PORT_MODES
 * Find out about available port modes
 */
#define MC_CMD_GET_PORT_MODES 0xff

#define MC_CMD_0xff_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_PORT_MODES_IN msgrequest */
#define    MC_CMD_GET_PORT_MODES_IN_LEN 0

/* MC_CMD_GET_PORT_MODES_OUT msgresponse */
#define    MC_CMD_GET_PORT_MODES_OUT_LEN 12
/* Bitmask of port modes available on the board (indexed by TLV_PORT_MODE_*) */
#define       MC_CMD_GET_PORT_MODES_OUT_MODES_OFST 0
/* Default (canonical) board mode */
#define       MC_CMD_GET_PORT_MODES_OUT_DEFAULT_MODE_OFST 4
/* Current board mode */
#define       MC_CMD_GET_PORT_MODES_OUT_CURRENT_MODE_OFST 8


/***********************************/
/* MC_CMD_READ_ATB
 * Sample voltages on the ATB
 */
#define MC_CMD_READ_ATB 0x100

#define MC_CMD_0x100_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_READ_ATB_IN msgrequest */
#define    MC_CMD_READ_ATB_IN_LEN 16
#define       MC_CMD_READ_ATB_IN_SIGNAL_BUS_OFST 0
#define          MC_CMD_READ_ATB_IN_BUS_CCOM  0x0 /* enum */
#define          MC_CMD_READ_ATB_IN_BUS_CKR  0x1 /* enum */
#define          MC_CMD_READ_ATB_IN_BUS_CPCIE  0x8 /* enum */
#define       MC_CMD_READ_ATB_IN_SIGNAL_EN_BITNO_OFST 4
#define       MC_CMD_READ_ATB_IN_SIGNAL_SEL_OFST 8
#define       MC_CMD_READ_ATB_IN_SETTLING_TIME_US_OFST 12

/* MC_CMD_READ_ATB_OUT msgresponse */
#define    MC_CMD_READ_ATB_OUT_LEN 4
#define       MC_CMD_READ_ATB_OUT_SAMPLE_MV_OFST 0


/***********************************/
/* MC_CMD_GET_WORKAROUNDS
 * Read the list of all implemented and all currently enabled workarounds. The
 * enums here must correspond with those in MC_CMD_WORKAROUND.
 */
#define MC_CMD_GET_WORKAROUNDS 0x59

#define MC_CMD_0x59_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_WORKAROUNDS_OUT msgresponse */
#define    MC_CMD_GET_WORKAROUNDS_OUT_LEN 8
/* Each workaround is represented by a single bit according to the enums below.
 */
#define       MC_CMD_GET_WORKAROUNDS_OUT_IMPLEMENTED_OFST 0
#define       MC_CMD_GET_WORKAROUNDS_OUT_ENABLED_OFST 4
/* enum: Bug 17230 work around. */
#define          MC_CMD_GET_WORKAROUNDS_OUT_BUG17230 0x2
/* enum: Bug 35388 work around (unsafe EVQ writes). */
#define          MC_CMD_GET_WORKAROUNDS_OUT_BUG35388 0x4
/* enum: Bug35017 workaround (A64 tables must be identity map) */
#define          MC_CMD_GET_WORKAROUNDS_OUT_BUG35017 0x8
/* enum: Bug 41750 present (MC_CMD_TRIGGER_INTERRUPT won't work) */
#define          MC_CMD_GET_WORKAROUNDS_OUT_BUG41750 0x10
/* enum: Bug 42008 present (Interrupts can overtake associated events). Caution
 * - before adding code that queries this workaround, remember that there's
 * released Monza firmware that doesn't understand MC_CMD_WORKAROUND_BUG42008,
 * and will hence (incorrectly) report that the bug doesn't exist.
 */
#define          MC_CMD_GET_WORKAROUNDS_OUT_BUG42008 0x20
/* enum: Bug 26807 features present in firmware (multicast filter chaining) */
#define          MC_CMD_GET_WORKAROUNDS_OUT_BUG26807 0x40
/* enum: Bug 61265 work around (broken EVQ TMR writes). */
#define          MC_CMD_GET_WORKAROUNDS_OUT_BUG61265 0x80


/***********************************/
/* MC_CMD_PRIVILEGE_MASK
 * Read/set privileges of an arbitrary PCIe function
 */
#define MC_CMD_PRIVILEGE_MASK 0x5a

#define MC_CMD_0x5a_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_PRIVILEGE_MASK_IN msgrequest */
#define    MC_CMD_PRIVILEGE_MASK_IN_LEN 8
/* The target function to have its mask read or set e.g. PF 0 = 0xFFFF0000, VF
 * 1,3 = 0x00030001
 */
#define       MC_CMD_PRIVILEGE_MASK_IN_FUNCTION_OFST 0
#define        MC_CMD_PRIVILEGE_MASK_IN_FUNCTION_PF_LBN 0
#define        MC_CMD_PRIVILEGE_MASK_IN_FUNCTION_PF_WIDTH 16
#define        MC_CMD_PRIVILEGE_MASK_IN_FUNCTION_VF_LBN 16
#define        MC_CMD_PRIVILEGE_MASK_IN_FUNCTION_VF_WIDTH 16
#define          MC_CMD_PRIVILEGE_MASK_IN_VF_NULL  0xffff /* enum */
/* New privilege mask to be set. The mask will only be changed if the MSB is
 * set to 1.
 */
#define       MC_CMD_PRIVILEGE_MASK_IN_NEW_MASK_OFST 4
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_ADMIN             0x1 /* enum */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_LINK              0x2 /* enum */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_ONLOAD            0x4 /* enum */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_PTP               0x8 /* enum */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_INSECURE_FILTERS  0x10 /* enum */
/* enum: Deprecated. Equivalent to MAC_SPOOFING_TX combined with CHANGE_MAC. */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_MAC_SPOOFING      0x20
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_UNICAST           0x40 /* enum */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_MULTICAST         0x80 /* enum */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_BROADCAST         0x100 /* enum */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_ALL_MULTICAST     0x200 /* enum */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_PROMISCUOUS       0x400 /* enum */
/* enum: Allows to set the TX packets' source MAC address to any arbitrary MAC
 * adress.
 */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_MAC_SPOOFING_TX   0x800
/* enum: Privilege that allows a Function to change the MAC address configured
 * in its associated vAdapter/vPort.
 */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_CHANGE_MAC        0x1000
/* enum: Privilege that allows a Function to install filters that specify VLANs
 * that are not in the permit list for the associated vPort. This privilege is
 * primarily to support ESX where vPorts are created that restrict traffic to
 * only a set of permitted VLANs. See the vPort flag FLAG_VLAN_RESTRICT.
 */
#define          MC_CMD_PRIVILEGE_MASK_IN_GRP_UNRESTRICTED_VLAN  0x2000
/* enum: Set this bit to indicate that a new privilege mask is to be set,
 * otherwise the command will only read the existing mask.
 */
#define          MC_CMD_PRIVILEGE_MASK_IN_DO_CHANGE             0x80000000

/* MC_CMD_PRIVILEGE_MASK_OUT msgresponse */
#define    MC_CMD_PRIVILEGE_MASK_OUT_LEN 4
/* For an admin function, always all the privileges are reported. */
#define       MC_CMD_PRIVILEGE_MASK_OUT_OLD_MASK_OFST 0


/***********************************/
/* MC_CMD_LINK_STATE_MODE
 * Read/set link state mode of a VF
 */
#define MC_CMD_LINK_STATE_MODE 0x5c

#define MC_CMD_0x5c_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_LINK_STATE_MODE_IN msgrequest */
#define    MC_CMD_LINK_STATE_MODE_IN_LEN 8
/* The target function to have its link state mode read or set, must be a VF
 * e.g. VF 1,3 = 0x00030001
 */
#define       MC_CMD_LINK_STATE_MODE_IN_FUNCTION_OFST 0
#define        MC_CMD_LINK_STATE_MODE_IN_FUNCTION_PF_LBN 0
#define        MC_CMD_LINK_STATE_MODE_IN_FUNCTION_PF_WIDTH 16
#define        MC_CMD_LINK_STATE_MODE_IN_FUNCTION_VF_LBN 16
#define        MC_CMD_LINK_STATE_MODE_IN_FUNCTION_VF_WIDTH 16
/* New link state mode to be set */
#define       MC_CMD_LINK_STATE_MODE_IN_NEW_MODE_OFST 4
#define          MC_CMD_LINK_STATE_MODE_IN_LINK_STATE_AUTO       0x0 /* enum */
#define          MC_CMD_LINK_STATE_MODE_IN_LINK_STATE_UP         0x1 /* enum */
#define          MC_CMD_LINK_STATE_MODE_IN_LINK_STATE_DOWN       0x2 /* enum */
/* enum: Use this value to just read the existing setting without modifying it.
 */
#define          MC_CMD_LINK_STATE_MODE_IN_DO_NOT_CHANGE         0xffffffff

/* MC_CMD_LINK_STATE_MODE_OUT msgresponse */
#define    MC_CMD_LINK_STATE_MODE_OUT_LEN 4
#define       MC_CMD_LINK_STATE_MODE_OUT_OLD_MODE_OFST 0


/***********************************/
/* MC_CMD_GET_SNAPSHOT_LENGTH
 * Obtain the curent range of allowable values for the SNAPSHOT_LENGTH
 * parameter to MC_CMD_INIT_RXQ.
 */
#define MC_CMD_GET_SNAPSHOT_LENGTH 0x101

#define MC_CMD_0x101_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_SNAPSHOT_LENGTH_IN msgrequest */
#define    MC_CMD_GET_SNAPSHOT_LENGTH_IN_LEN 0

/* MC_CMD_GET_SNAPSHOT_LENGTH_OUT msgresponse */
#define    MC_CMD_GET_SNAPSHOT_LENGTH_OUT_LEN 8
/* Minimum acceptable snapshot length. */
#define       MC_CMD_GET_SNAPSHOT_LENGTH_OUT_RX_SNAPLEN_MIN_OFST 0
/* Maximum acceptable snapshot length. */
#define       MC_CMD_GET_SNAPSHOT_LENGTH_OUT_RX_SNAPLEN_MAX_OFST 4


/***********************************/
/* MC_CMD_FUSE_DIAGS
 * Additional fuse diagnostics
 */
#define MC_CMD_FUSE_DIAGS 0x102

#define MC_CMD_0x102_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_FUSE_DIAGS_IN msgrequest */
#define    MC_CMD_FUSE_DIAGS_IN_LEN 0

/* MC_CMD_FUSE_DIAGS_OUT msgresponse */
#define    MC_CMD_FUSE_DIAGS_OUT_LEN 48
/* Total number of mismatched bits between pairs in area 0 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA0_MISMATCH_BITS_OFST 0
/* Total number of unexpectedly clear (set in B but not A) bits in area 0 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA0_PAIR_A_BAD_BITS_OFST 4
/* Total number of unexpectedly clear (set in A but not B) bits in area 0 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA0_PAIR_B_BAD_BITS_OFST 8
/* Checksum of data after logical OR of pairs in area 0 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA0_CHECKSUM_OFST 12
/* Total number of mismatched bits between pairs in area 1 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA1_MISMATCH_BITS_OFST 16
/* Total number of unexpectedly clear (set in B but not A) bits in area 1 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA1_PAIR_A_BAD_BITS_OFST 20
/* Total number of unexpectedly clear (set in A but not B) bits in area 1 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA1_PAIR_B_BAD_BITS_OFST 24
/* Checksum of data after logical OR of pairs in area 1 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA1_CHECKSUM_OFST 28
/* Total number of mismatched bits between pairs in area 2 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA2_MISMATCH_BITS_OFST 32
/* Total number of unexpectedly clear (set in B but not A) bits in area 2 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA2_PAIR_A_BAD_BITS_OFST 36
/* Total number of unexpectedly clear (set in A but not B) bits in area 2 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA2_PAIR_B_BAD_BITS_OFST 40
/* Checksum of data after logical OR of pairs in area 2 */
#define       MC_CMD_FUSE_DIAGS_OUT_AREA2_CHECKSUM_OFST 44


/***********************************/
/* MC_CMD_PRIVILEGE_MODIFY
 * Modify the privileges of a set of PCIe functions. Note that this operation
 * only effects non-admin functions unless the admin privilege itself is
 * included in one of the masks provided.
 */
#define MC_CMD_PRIVILEGE_MODIFY 0x60

#define MC_CMD_0x60_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_PRIVILEGE_MODIFY_IN msgrequest */
#define    MC_CMD_PRIVILEGE_MODIFY_IN_LEN 16
/* The groups of functions to have their privilege masks modified. */
#define       MC_CMD_PRIVILEGE_MODIFY_IN_FN_GROUP_OFST 0
#define          MC_CMD_PRIVILEGE_MODIFY_IN_NONE       0x0 /* enum */
#define          MC_CMD_PRIVILEGE_MODIFY_IN_ALL        0x1 /* enum */
#define          MC_CMD_PRIVILEGE_MODIFY_IN_PFS_ONLY   0x2 /* enum */
#define          MC_CMD_PRIVILEGE_MODIFY_IN_VFS_ONLY   0x3 /* enum */
#define          MC_CMD_PRIVILEGE_MODIFY_IN_VFS_OF_PF  0x4 /* enum */
#define          MC_CMD_PRIVILEGE_MODIFY_IN_ONE        0x5 /* enum */
/* For VFS_OF_PF specify the PF, for ONE specify the target function */
#define       MC_CMD_PRIVILEGE_MODIFY_IN_FUNCTION_OFST 4
#define        MC_CMD_PRIVILEGE_MODIFY_IN_FUNCTION_PF_LBN 0
#define        MC_CMD_PRIVILEGE_MODIFY_IN_FUNCTION_PF_WIDTH 16
#define        MC_CMD_PRIVILEGE_MODIFY_IN_FUNCTION_VF_LBN 16
#define        MC_CMD_PRIVILEGE_MODIFY_IN_FUNCTION_VF_WIDTH 16
/* Privileges to be added to the target functions. For privilege definitions
 * refer to the command MC_CMD_PRIVILEGE_MASK
 */
#define       MC_CMD_PRIVILEGE_MODIFY_IN_ADD_MASK_OFST 8
/* Privileges to be removed from the target functions. For privilege
 * definitions refer to the command MC_CMD_PRIVILEGE_MASK
 */
#define       MC_CMD_PRIVILEGE_MODIFY_IN_REMOVE_MASK_OFST 12

/* MC_CMD_PRIVILEGE_MODIFY_OUT msgresponse */
#define    MC_CMD_PRIVILEGE_MODIFY_OUT_LEN 0


/***********************************/
/* MC_CMD_XPM_READ_BYTES
 * Read XPM memory
 */
#define MC_CMD_XPM_READ_BYTES 0x103

#define MC_CMD_0x103_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_READ_BYTES_IN msgrequest */
#define    MC_CMD_XPM_READ_BYTES_IN_LEN 8
/* Start address (byte) */
#define       MC_CMD_XPM_READ_BYTES_IN_ADDR_OFST 0
/* Count (bytes) */
#define       MC_CMD_XPM_READ_BYTES_IN_COUNT_OFST 4

/* MC_CMD_XPM_READ_BYTES_OUT msgresponse */
#define    MC_CMD_XPM_READ_BYTES_OUT_LENMIN 0
#define    MC_CMD_XPM_READ_BYTES_OUT_LENMAX 252
#define    MC_CMD_XPM_READ_BYTES_OUT_LEN(num) (0+1*(num))
/* Data */
#define       MC_CMD_XPM_READ_BYTES_OUT_DATA_OFST 0
#define       MC_CMD_XPM_READ_BYTES_OUT_DATA_LEN 1
#define       MC_CMD_XPM_READ_BYTES_OUT_DATA_MINNUM 0
#define       MC_CMD_XPM_READ_BYTES_OUT_DATA_MAXNUM 252


/***********************************/
/* MC_CMD_XPM_WRITE_BYTES
 * Write XPM memory
 */
#define MC_CMD_XPM_WRITE_BYTES 0x104

#define MC_CMD_0x104_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_WRITE_BYTES_IN msgrequest */
#define    MC_CMD_XPM_WRITE_BYTES_IN_LENMIN 8
#define    MC_CMD_XPM_WRITE_BYTES_IN_LENMAX 252
#define    MC_CMD_XPM_WRITE_BYTES_IN_LEN(num) (8+1*(num))
/* Start address (byte) */
#define       MC_CMD_XPM_WRITE_BYTES_IN_ADDR_OFST 0
/* Count (bytes) */
#define       MC_CMD_XPM_WRITE_BYTES_IN_COUNT_OFST 4
/* Data */
#define       MC_CMD_XPM_WRITE_BYTES_IN_DATA_OFST 8
#define       MC_CMD_XPM_WRITE_BYTES_IN_DATA_LEN 1
#define       MC_CMD_XPM_WRITE_BYTES_IN_DATA_MINNUM 0
#define       MC_CMD_XPM_WRITE_BYTES_IN_DATA_MAXNUM 244

/* MC_CMD_XPM_WRITE_BYTES_OUT msgresponse */
#define    MC_CMD_XPM_WRITE_BYTES_OUT_LEN 0


/***********************************/
/* MC_CMD_XPM_READ_SECTOR
 * Read XPM sector
 */
#define MC_CMD_XPM_READ_SECTOR 0x105

#define MC_CMD_0x105_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_READ_SECTOR_IN msgrequest */
#define    MC_CMD_XPM_READ_SECTOR_IN_LEN 8
/* Sector index */
#define       MC_CMD_XPM_READ_SECTOR_IN_INDEX_OFST 0
/* Sector size */
#define       MC_CMD_XPM_READ_SECTOR_IN_SIZE_OFST 4

/* MC_CMD_XPM_READ_SECTOR_OUT msgresponse */
#define    MC_CMD_XPM_READ_SECTOR_OUT_LENMIN 4
#define    MC_CMD_XPM_READ_SECTOR_OUT_LENMAX 36
#define    MC_CMD_XPM_READ_SECTOR_OUT_LEN(num) (4+1*(num))
/* Sector type */
#define       MC_CMD_XPM_READ_SECTOR_OUT_TYPE_OFST 0
#define          MC_CMD_XPM_READ_SECTOR_OUT_BLANK            0x0 /* enum */
#define          MC_CMD_XPM_READ_SECTOR_OUT_CRYPTO_KEY_128   0x1 /* enum */
#define          MC_CMD_XPM_READ_SECTOR_OUT_CRYPTO_KEY_256   0x2 /* enum */
#define          MC_CMD_XPM_READ_SECTOR_OUT_INVALID          0xff /* enum */
/* Sector data */
#define       MC_CMD_XPM_READ_SECTOR_OUT_DATA_OFST 4
#define       MC_CMD_XPM_READ_SECTOR_OUT_DATA_LEN 1
#define       MC_CMD_XPM_READ_SECTOR_OUT_DATA_MINNUM 0
#define       MC_CMD_XPM_READ_SECTOR_OUT_DATA_MAXNUM 32


/***********************************/
/* MC_CMD_XPM_WRITE_SECTOR
 * Write XPM sector
 */
#define MC_CMD_XPM_WRITE_SECTOR 0x106

#define MC_CMD_0x106_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_WRITE_SECTOR_IN msgrequest */
#define    MC_CMD_XPM_WRITE_SECTOR_IN_LENMIN 12
#define    MC_CMD_XPM_WRITE_SECTOR_IN_LENMAX 44
#define    MC_CMD_XPM_WRITE_SECTOR_IN_LEN(num) (12+1*(num))
/* If writing fails due to an uncorrectable error, try up to RETRIES following
 * sectors (or until no more space available). If 0, only one write attempt is
 * made. Note that uncorrectable errors are unlikely, thanks to XPM self-repair
 * mechanism.
 */
#define       MC_CMD_XPM_WRITE_SECTOR_IN_RETRIES_OFST 0
#define       MC_CMD_XPM_WRITE_SECTOR_IN_RETRIES_LEN 1
#define       MC_CMD_XPM_WRITE_SECTOR_IN_RESERVED_OFST 1
#define       MC_CMD_XPM_WRITE_SECTOR_IN_RESERVED_LEN 3
/* Sector type */
#define       MC_CMD_XPM_WRITE_SECTOR_IN_TYPE_OFST 4
/*            Enum values, see field(s): */
/*               MC_CMD_XPM_READ_SECTOR/MC_CMD_XPM_READ_SECTOR_OUT/TYPE */
/* Sector size */
#define       MC_CMD_XPM_WRITE_SECTOR_IN_SIZE_OFST 8
/* Sector data */
#define       MC_CMD_XPM_WRITE_SECTOR_IN_DATA_OFST 12
#define       MC_CMD_XPM_WRITE_SECTOR_IN_DATA_LEN 1
#define       MC_CMD_XPM_WRITE_SECTOR_IN_DATA_MINNUM 0
#define       MC_CMD_XPM_WRITE_SECTOR_IN_DATA_MAXNUM 32

/* MC_CMD_XPM_WRITE_SECTOR_OUT msgresponse */
#define    MC_CMD_XPM_WRITE_SECTOR_OUT_LEN 4
/* New sector index */
#define       MC_CMD_XPM_WRITE_SECTOR_OUT_INDEX_OFST 0


/***********************************/
/* MC_CMD_XPM_INVALIDATE_SECTOR
 * Invalidate XPM sector
 */
#define MC_CMD_XPM_INVALIDATE_SECTOR 0x107

#define MC_CMD_0x107_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_INVALIDATE_SECTOR_IN msgrequest */
#define    MC_CMD_XPM_INVALIDATE_SECTOR_IN_LEN 4
/* Sector index */
#define       MC_CMD_XPM_INVALIDATE_SECTOR_IN_INDEX_OFST 0

/* MC_CMD_XPM_INVALIDATE_SECTOR_OUT msgresponse */
#define    MC_CMD_XPM_INVALIDATE_SECTOR_OUT_LEN 0


/***********************************/
/* MC_CMD_XPM_BLANK_CHECK
 * Blank-check XPM memory and report bad locations
 */
#define MC_CMD_XPM_BLANK_CHECK 0x108

#define MC_CMD_0x108_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_BLANK_CHECK_IN msgrequest */
#define    MC_CMD_XPM_BLANK_CHECK_IN_LEN 8
/* Start address (byte) */
#define       MC_CMD_XPM_BLANK_CHECK_IN_ADDR_OFST 0
/* Count (bytes) */
#define       MC_CMD_XPM_BLANK_CHECK_IN_COUNT_OFST 4

/* MC_CMD_XPM_BLANK_CHECK_OUT msgresponse */
#define    MC_CMD_XPM_BLANK_CHECK_OUT_LENMIN 4
#define    MC_CMD_XPM_BLANK_CHECK_OUT_LENMAX 252
#define    MC_CMD_XPM_BLANK_CHECK_OUT_LEN(num) (4+2*(num))
/* Total number of bad (non-blank) locations */
#define       MC_CMD_XPM_BLANK_CHECK_OUT_BAD_COUNT_OFST 0
/* Addresses of bad locations (may be less than BAD_COUNT, if all cannot fit
 * into MCDI response)
 */
#define       MC_CMD_XPM_BLANK_CHECK_OUT_BAD_ADDR_OFST 4
#define       MC_CMD_XPM_BLANK_CHECK_OUT_BAD_ADDR_LEN 2
#define       MC_CMD_XPM_BLANK_CHECK_OUT_BAD_ADDR_MINNUM 0
#define       MC_CMD_XPM_BLANK_CHECK_OUT_BAD_ADDR_MAXNUM 124


/***********************************/
/* MC_CMD_XPM_REPAIR
 * Blank-check and repair XPM memory
 */
#define MC_CMD_XPM_REPAIR 0x109

#define MC_CMD_0x109_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_REPAIR_IN msgrequest */
#define    MC_CMD_XPM_REPAIR_IN_LEN 8
/* Start address (byte) */
#define       MC_CMD_XPM_REPAIR_IN_ADDR_OFST 0
/* Count (bytes) */
#define       MC_CMD_XPM_REPAIR_IN_COUNT_OFST 4

/* MC_CMD_XPM_REPAIR_OUT msgresponse */
#define    MC_CMD_XPM_REPAIR_OUT_LEN 0


/***********************************/
/* MC_CMD_XPM_DECODER_TEST
 * Test XPM memory address decoders for gross manufacturing defects. Can only
 * be performed on an unprogrammed part.
 */
#define MC_CMD_XPM_DECODER_TEST 0x10a

#define MC_CMD_0x10a_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_DECODER_TEST_IN msgrequest */
#define    MC_CMD_XPM_DECODER_TEST_IN_LEN 0

/* MC_CMD_XPM_DECODER_TEST_OUT msgresponse */
#define    MC_CMD_XPM_DECODER_TEST_OUT_LEN 0


/***********************************/
/* MC_CMD_XPM_WRITE_TEST
 * XPM memory write test. Test XPM write logic for gross manufacturing defects
 * by writing to a dedicated test row. There are 16 locations in the test row
 * and the test can only be performed on locations that have not been
 * previously used (i.e. can be run at most 16 times). The test will pick the
 * first available location to use, or fail with ENOSPC if none left.
 */
#define MC_CMD_XPM_WRITE_TEST 0x10b

#define MC_CMD_0x10b_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_WRITE_TEST_IN msgrequest */
#define    MC_CMD_XPM_WRITE_TEST_IN_LEN 0

/* MC_CMD_XPM_WRITE_TEST_OUT msgresponse */
#define    MC_CMD_XPM_WRITE_TEST_OUT_LEN 0


/***********************************/
/* MC_CMD_EXEC_SIGNED
 * Check the CMAC of the contents of IMEM and DMEM against the value supplied
 * and if correct begin execution from the start of IMEM. The caller supplies a
 * key ID, the length of IMEM and DMEM to validate and the expected CMAC. CMAC
 * computation runs from the start of IMEM, and from the start of DMEM + 16k,
 * to match flash booting. The command will respond with EINVAL if the CMAC
 * does match, otherwise it will respond with success before it jumps to IMEM.
 */
#define MC_CMD_EXEC_SIGNED 0x10c

#define MC_CMD_0x10c_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_EXEC_SIGNED_IN msgrequest */
#define    MC_CMD_EXEC_SIGNED_IN_LEN 28
/* the length of code to include in the CMAC */
#define       MC_CMD_EXEC_SIGNED_IN_CODELEN_OFST 0
/* the length of date to include in the CMAC */
#define       MC_CMD_EXEC_SIGNED_IN_DATALEN_OFST 4
/* the XPM sector containing the key to use */
#define       MC_CMD_EXEC_SIGNED_IN_KEYSECTOR_OFST 8
/* the expected CMAC value */
#define       MC_CMD_EXEC_SIGNED_IN_CMAC_OFST 12
#define       MC_CMD_EXEC_SIGNED_IN_CMAC_LEN 16

/* MC_CMD_EXEC_SIGNED_OUT msgresponse */
#define    MC_CMD_EXEC_SIGNED_OUT_LEN 0


/***********************************/
/* MC_CMD_PREPARE_SIGNED
 * Prepare to upload a signed image. This will scrub the specified length of
 * the data region, which must be at least as large as the DATALEN supplied to
 * MC_CMD_EXEC_SIGNED.
 */
#define MC_CMD_PREPARE_SIGNED 0x10d

#define MC_CMD_0x10d_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_PREPARE_SIGNED_IN msgrequest */
#define    MC_CMD_PREPARE_SIGNED_IN_LEN 4
/* the length of data area to clear */
#define       MC_CMD_PREPARE_SIGNED_IN_DATALEN_OFST 0

/* MC_CMD_PREPARE_SIGNED_OUT msgresponse */
#define    MC_CMD_PREPARE_SIGNED_OUT_LEN 0


/***********************************/
/* MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS
 * Configure UDP ports for tunnel encapsulation hardware acceleration. The
 * parser-dispatcher will attempt to parse traffic on these ports as tunnel
 * encapsulation PDUs and filter them using the tunnel encapsulation filter
 * chain rather than the standard filter chain. Note that this command can
 * cause all functions to see a reset. (Available on Medford only.)
 */
#define MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS 0x117

#define MC_CMD_0x117_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN msgrequest */
#define    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_LENMIN 4
#define    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_LENMAX 68
#define    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_LEN(num) (4+4*(num))
/* Flags */
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_FLAGS_OFST 0
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_FLAGS_LEN 2
#define        MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_UNLOADING_LBN 0
#define        MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_UNLOADING_WIDTH 1
/* The number of entries in the ENTRIES array */
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_NUM_ENTRIES_OFST 2
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_NUM_ENTRIES_LEN 2
/* Entries defining the UDP port to protocol mapping, each laid out as a
 * TUNNEL_ENCAP_UDP_PORT_ENTRY
 */
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_ENTRIES_OFST 4
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_ENTRIES_LEN 4
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_ENTRIES_MINNUM 0
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_IN_ENTRIES_MAXNUM 16

/* MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT msgresponse */
#define    MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT_LEN 2
/* Flags */
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT_FLAGS_OFST 0
#define       MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT_FLAGS_LEN 2
#define        MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT_RESETTING_LBN 0
#define        MC_CMD_SET_TUNNEL_ENCAP_UDP_PORTS_OUT_RESETTING_WIDTH 1

/* TUNNEL_ENCAP_UDP_PORT_ENTRY structuredef */
#define    TUNNEL_ENCAP_UDP_PORT_ENTRY_LEN 4
/* UDP port (the standard ports are named below but any port may be used) */
#define       TUNNEL_ENCAP_UDP_PORT_ENTRY_UDP_PORT_OFST 0
#define       TUNNEL_ENCAP_UDP_PORT_ENTRY_UDP_PORT_LEN 2
/* enum: the IANA allocated UDP port for VXLAN */
#define          TUNNEL_ENCAP_UDP_PORT_ENTRY_IANA_VXLAN_UDP_PORT  0x12b5
/* enum: the IANA allocated UDP port for Geneve */
#define          TUNNEL_ENCAP_UDP_PORT_ENTRY_IANA_GENEVE_UDP_PORT  0x17c1
#define       TUNNEL_ENCAP_UDP_PORT_ENTRY_UDP_PORT_LBN 0
#define       TUNNEL_ENCAP_UDP_PORT_ENTRY_UDP_PORT_WIDTH 16
/* tunnel encapsulation protocol (only those named below are supported) */
#define       TUNNEL_ENCAP_UDP_PORT_ENTRY_PROTOCOL_OFST 2
#define       TUNNEL_ENCAP_UDP_PORT_ENTRY_PROTOCOL_LEN 2
/* enum: VXLAN */
#define          TUNNEL_ENCAP_UDP_PORT_ENTRY_VXLAN  0x0
/* enum: Geneve */
#define          TUNNEL_ENCAP_UDP_PORT_ENTRY_GENEVE  0x1
#define       TUNNEL_ENCAP_UDP_PORT_ENTRY_PROTOCOL_LBN 16
#define       TUNNEL_ENCAP_UDP_PORT_ENTRY_PROTOCOL_WIDTH 16


/***********************************/
/* MC_CMD_RX_BALANCING
 * Configure a port upconverter to distribute the packets on both RX engines.
 * Packets are distributed based on a table with the destination vFIFO. The
 * index of the table is a hash of source and destination of IPV4 and VLAN
 * priority.
 */
#define MC_CMD_RX_BALANCING 0x118

#define MC_CMD_0x118_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_RX_BALANCING_IN msgrequest */
#define    MC_CMD_RX_BALANCING_IN_LEN 16
/* The RX port whose upconverter table will be modified */
#define       MC_CMD_RX_BALANCING_IN_PORT_OFST 0
/* The VLAN priority associated to the table index and vFIFO */
#define       MC_CMD_RX_BALANCING_IN_PRIORITY_OFST 4
/* The resulting bit of SRC^DST for indexing the table */
#define       MC_CMD_RX_BALANCING_IN_SRC_DST_OFST 8
/* The RX engine to which the vFIFO in the table entry will point to */
#define       MC_CMD_RX_BALANCING_IN_ENG_OFST 12

/* MC_CMD_RX_BALANCING_OUT msgresponse */
#define    MC_CMD_RX_BALANCING_OUT_LEN 0


/***********************************/
/* MC_CMD_NVRAM_PRIVATE_APPEND
 * Append a single TLV to the MC_USAGE_TLV partition. Returns MC_CMD_ERR_EEXIST
 * if the tag is already present.
 */
#define MC_CMD_NVRAM_PRIVATE_APPEND 0x11c

#define MC_CMD_0x11c_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_NVRAM_PRIVATE_APPEND_IN msgrequest */
#define    MC_CMD_NVRAM_PRIVATE_APPEND_IN_LENMIN 9
#define    MC_CMD_NVRAM_PRIVATE_APPEND_IN_LENMAX 252
#define    MC_CMD_NVRAM_PRIVATE_APPEND_IN_LEN(num) (8+1*(num))
/* The tag to be appended */
#define       MC_CMD_NVRAM_PRIVATE_APPEND_IN_TAG_OFST 0
/* The length of the data */
#define       MC_CMD_NVRAM_PRIVATE_APPEND_IN_LENGTH_OFST 4
/* The data to be contained in the TLV structure */
#define       MC_CMD_NVRAM_PRIVATE_APPEND_IN_DATA_BUFFER_OFST 8
#define       MC_CMD_NVRAM_PRIVATE_APPEND_IN_DATA_BUFFER_LEN 1
#define       MC_CMD_NVRAM_PRIVATE_APPEND_IN_DATA_BUFFER_MINNUM 1
#define       MC_CMD_NVRAM_PRIVATE_APPEND_IN_DATA_BUFFER_MAXNUM 244

/* MC_CMD_NVRAM_PRIVATE_APPEND_OUT msgresponse */
#define    MC_CMD_NVRAM_PRIVATE_APPEND_OUT_LEN 0


/***********************************/
/* MC_CMD_XPM_VERIFY_CONTENTS
 * Verify that the contents of the XPM memory is correct (Medford only). This
 * is used during manufacture to check that the XPM memory has been programmed
 * correctly at ATE.
 */
#define MC_CMD_XPM_VERIFY_CONTENTS 0x11b

#define MC_CMD_0x11b_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_XPM_VERIFY_CONTENTS_IN msgrequest */
#define    MC_CMD_XPM_VERIFY_CONTENTS_IN_LEN 4
/* Data type to be checked */
#define       MC_CMD_XPM_VERIFY_CONTENTS_IN_DATA_TYPE_OFST 0

/* MC_CMD_XPM_VERIFY_CONTENTS_OUT msgresponse */
#define    MC_CMD_XPM_VERIFY_CONTENTS_OUT_LENMIN 12
#define    MC_CMD_XPM_VERIFY_CONTENTS_OUT_LENMAX 252
#define    MC_CMD_XPM_VERIFY_CONTENTS_OUT_LEN(num) (12+1*(num))
/* Number of sectors found (test builds only) */
#define       MC_CMD_XPM_VERIFY_CONTENTS_OUT_NUM_SECTORS_OFST 0
/* Number of bytes found (test builds only) */
#define       MC_CMD_XPM_VERIFY_CONTENTS_OUT_NUM_BYTES_OFST 4
/* Length of signature */
#define       MC_CMD_XPM_VERIFY_CONTENTS_OUT_SIG_LENGTH_OFST 8
/* Signature */
#define       MC_CMD_XPM_VERIFY_CONTENTS_OUT_SIGNATURE_OFST 12
#define       MC_CMD_XPM_VERIFY_CONTENTS_OUT_SIGNATURE_LEN 1
#define       MC_CMD_XPM_VERIFY_CONTENTS_OUT_SIGNATURE_MINNUM 0
#define       MC_CMD_XPM_VERIFY_CONTENTS_OUT_SIGNATURE_MAXNUM 240


/***********************************/
/* MC_CMD_SET_EVQ_TMR
 * Update the timer load, timer reload and timer mode values for a given EVQ.
 * The requested timer values (in TMR_LOAD_REQ_NS and TMR_RELOAD_REQ_NS) will
 * be rounded up to the granularity supported by the hardware, then truncated
 * to the range supported by the hardware. The resulting value after the
 * rounding and truncation will be returned to the caller (in TMR_LOAD_ACT_NS
 * and TMR_RELOAD_ACT_NS).
 */
#define MC_CMD_SET_EVQ_TMR 0x120

#define MC_CMD_0x120_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_SET_EVQ_TMR_IN msgrequest */
#define    MC_CMD_SET_EVQ_TMR_IN_LEN 16
/* Function-relative queue instance */
#define       MC_CMD_SET_EVQ_TMR_IN_INSTANCE_OFST 0
/* Requested value for timer load (in nanoseconds) */
#define       MC_CMD_SET_EVQ_TMR_IN_TMR_LOAD_REQ_NS_OFST 4
/* Requested value for timer reload (in nanoseconds) */
#define       MC_CMD_SET_EVQ_TMR_IN_TMR_RELOAD_REQ_NS_OFST 8
/* Timer mode. Meanings as per EVQ_TMR_REG.TC_TIMER_VAL */
#define       MC_CMD_SET_EVQ_TMR_IN_TMR_MODE_OFST 12
#define          MC_CMD_SET_EVQ_TMR_IN_TIMER_MODE_DIS  0x0 /* enum */
#define          MC_CMD_SET_EVQ_TMR_IN_TIMER_MODE_IMMED_START  0x1 /* enum */
#define          MC_CMD_SET_EVQ_TMR_IN_TIMER_MODE_TRIG_START  0x2 /* enum */
#define          MC_CMD_SET_EVQ_TMR_IN_TIMER_MODE_INT_HLDOFF  0x3 /* enum */

/* MC_CMD_SET_EVQ_TMR_OUT msgresponse */
#define    MC_CMD_SET_EVQ_TMR_OUT_LEN 8
/* Actual value for timer load (in nanoseconds) */
#define       MC_CMD_SET_EVQ_TMR_OUT_TMR_LOAD_ACT_NS_OFST 0
/* Actual value for timer reload (in nanoseconds) */
#define       MC_CMD_SET_EVQ_TMR_OUT_TMR_RELOAD_ACT_NS_OFST 4


/***********************************/
/* MC_CMD_GET_EVQ_TMR_PROPERTIES
 * Query properties about the event queue timers.
 */
#define MC_CMD_GET_EVQ_TMR_PROPERTIES 0x122

#define MC_CMD_0x122_PRIVILEGE_CTG SRIOV_CTG_GENERAL

/* MC_CMD_GET_EVQ_TMR_PROPERTIES_IN msgrequest */
#define    MC_CMD_GET_EVQ_TMR_PROPERTIES_IN_LEN 0

/* MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT msgresponse */
#define    MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_LEN 36
/* Reserved for future use. */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_FLAGS_OFST 0
/* For timers updated via writes to EVQ_TMR_REG, this is the time interval (in
 * nanoseconds) for each increment of the timer load/reload count. The
 * requested duration of a timer is this value multiplied by the timer
 * load/reload count.
 */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_TMR_REG_NS_PER_COUNT_OFST 4
/* For timers updated via writes to EVQ_TMR_REG, this is the maximum value
 * allowed for timer load/reload counts.
 */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_TMR_REG_MAX_COUNT_OFST 8
/* For timers updated via writes to EVQ_TMR_REG, timer load/reload counts not a
 * multiple of this step size will be rounded in an implementation defined
 * manner.
 */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_TMR_REG_STEP_OFST 12
/* Maximum timer duration (in nanoseconds) for timers updated via MCDI. Only
 * meaningful if MC_CMD_SET_EVQ_TMR is implemented.
 */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_MCDI_TMR_MAX_NS_OFST 16
/* Timer durations requested via MCDI that are not a multiple of this step size
 * will be rounded up. Only meaningful if MC_CMD_SET_EVQ_TMR is implemented.
 */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_MCDI_TMR_STEP_NS_OFST 20
/* For timers updated using the bug35388 workaround, this is the time interval
 * (in nanoseconds) for each increment of the timer load/reload count. The
 * requested duration of a timer is this value multiplied by the timer
 * load/reload count. This field is only meaningful if the bug35388 workaround
 * is enabled.
 */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_BUG35388_TMR_NS_PER_COUNT_OFST 24
/* For timers updated using the bug35388 workaround, this is the maximum value
 * allowed for timer load/reload counts. This field is only meaningful if the
 * bug35388 workaround is enabled.
 */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_BUG35388_TMR_MAX_COUNT_OFST 28
/* For timers updated using the bug35388 workaround, timer load/reload counts
 * not a multiple of this step size will be rounded in an implementation
 * defined manner. This field is only meaningful if the bug35388 workaround is
 * enabled.
 */
#define       MC_CMD_GET_EVQ_TMR_PROPERTIES_OUT_BUG35388_TMR_STEP_OFST 32


/***********************************/
/* MC_CMD_ALLOCATE_TX_VFIFO_CP
 * When we use the TX_vFIFO_ULL mode, we can allocate common pools using the
 * non used switch buffers.
 */
#define MC_CMD_ALLOCATE_TX_VFIFO_CP 0x11d

#define MC_CMD_0x11d_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_ALLOCATE_TX_VFIFO_CP_IN msgrequest */
#define    MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_LEN 20
/* Desired instance. Must be set to a specific instance, which is a function
 * local queue index.
 */
#define       MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_INSTANCE_OFST 0
/* Will the common pool be used as TX_vFIFO_ULL (1) */
#define       MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_MODE_OFST 4
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_ENABLED       0x1 /* enum */
/* enum: Using this interface without TX_vFIFO_ULL is not supported for now */
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_DISABLED      0x0
/* Number of buffers to reserve for the common pool */
#define       MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_SIZE_OFST 8
/* TX datapath to which the Common Pool is connected to. */
#define       MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_INGRESS_OFST 12
/* enum: Extracts information from function */
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_USE_FUNCTION_VALUE          -0x1
/* Network port or RX Engine to which the common pool connects. */
#define       MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_EGRESS_OFST 16
/* enum: Extracts information from function */
/*               MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_USE_FUNCTION_VALUE          -0x1 */
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_PORT0          0x0 /* enum */
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_PORT1          0x1 /* enum */
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_PORT2          0x2 /* enum */
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_PORT3          0x3 /* enum */
/* enum: To enable Switch loopback with Rx engine 0 */
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_RX_ENGINE0     0x4
/* enum: To enable Switch loopback with Rx engine 1 */
#define          MC_CMD_ALLOCATE_TX_VFIFO_CP_IN_RX_ENGINE1     0x5

/* MC_CMD_ALLOCATE_TX_VFIFO_CP_OUT msgresponse */
#define    MC_CMD_ALLOCATE_TX_VFIFO_CP_OUT_LEN 4
/* ID of the common pool allocated */
#define       MC_CMD_ALLOCATE_TX_VFIFO_CP_OUT_CP_ID_OFST 0


/***********************************/
/* MC_CMD_ALLOCATE_TX_VFIFO_VFIFO
 * When we use the TX_vFIFO_ULL mode, we can allocate vFIFOs using the
 * previously allocated common pools.
 */
#define MC_CMD_ALLOCATE_TX_VFIFO_VFIFO 0x11e

#define MC_CMD_0x11e_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN msgrequest */
#define    MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_LEN 20
/* Common pool previously allocated to which the new vFIFO will be associated
 */
#define       MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_CP_OFST 0
/* Port or RX engine to associate the vFIFO egress */
#define       MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_EGRESS_OFST 4
/* enum: Extracts information from common pool */
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_USE_CP_VALUE   -0x1
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_PORT0          0x0 /* enum */
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_PORT1          0x1 /* enum */
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_PORT2          0x2 /* enum */
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_PORT3          0x3 /* enum */
/* enum: To enable Switch loopback with Rx engine 0 */
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_RX_ENGINE0     0x4
/* enum: To enable Switch loopback with Rx engine 1 */
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_RX_ENGINE1     0x5
/* Minimum number of buffers that the pool must have */
#define       MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_SIZE_OFST 8
/* enum: Do not check the space available */
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_NO_MINIMUM     0x0
/* Will the vFIFO be used as TX_vFIFO_ULL */
#define       MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_MODE_OFST 12
/* Network priority of the vFIFO,if applicable */
#define       MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_PRIORITY_OFST 16
/* enum: Search for the lowest unused priority */
#define          MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_IN_LOWEST_AVAILABLE  -0x1

/* MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_OUT msgresponse */
#define    MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_OUT_LEN 8
/* Short vFIFO ID */
#define       MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_OUT_VID_OFST 0
/* Network priority of the vFIFO */
#define       MC_CMD_ALLOCATE_TX_VFIFO_VFIFO_OUT_PRIORITY_OFST 4


/***********************************/
/* MC_CMD_TEARDOWN_TX_VFIFO_VF
 * This interface clears the configuration of the given vFIFO and leaves it
 * ready to be re-used.
 */
#define MC_CMD_TEARDOWN_TX_VFIFO_VF 0x11f

#define MC_CMD_0x11f_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_TEARDOWN_TX_VFIFO_VF_IN msgrequest */
#define    MC_CMD_TEARDOWN_TX_VFIFO_VF_IN_LEN 4
/* Short vFIFO ID */
#define       MC_CMD_TEARDOWN_TX_VFIFO_VF_IN_VFIFO_OFST 0

/* MC_CMD_TEARDOWN_TX_VFIFO_VF_OUT msgresponse */
#define    MC_CMD_TEARDOWN_TX_VFIFO_VF_OUT_LEN 0


/***********************************/
/* MC_CMD_DEALLOCATE_TX_VFIFO_CP
 * This interface clears the configuration of the given common pool and leaves
 * it ready to be re-used.
 */
#define MC_CMD_DEALLOCATE_TX_VFIFO_CP 0x121

#define MC_CMD_0x121_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_DEALLOCATE_TX_VFIFO_CP_IN msgrequest */
#define    MC_CMD_DEALLOCATE_TX_VFIFO_CP_IN_LEN 4
/* Common pool ID given when pool allocated */
#define       MC_CMD_DEALLOCATE_TX_VFIFO_CP_IN_POOL_ID_OFST 0

/* MC_CMD_DEALLOCATE_TX_VFIFO_CP_OUT msgresponse */
#define    MC_CMD_DEALLOCATE_TX_VFIFO_CP_OUT_LEN 0


/***********************************/
/* MC_CMD_SWITCH_GET_UNASSIGNED_BUFFERS
 * This interface allows the host to find out how many common pool buffers are
 * not yet assigned.
 */
#define MC_CMD_SWITCH_GET_UNASSIGNED_BUFFERS 0x124

#define MC_CMD_0x124_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_SWITCH_GET_UNASSIGNED_BUFFERS_IN msgrequest */
#define    MC_CMD_SWITCH_GET_UNASSIGNED_BUFFERS_IN_LEN 0

/* MC_CMD_SWITCH_GET_UNASSIGNED_BUFFERS_OUT msgresponse */
#define    MC_CMD_SWITCH_GET_UNASSIGNED_BUFFERS_OUT_LEN 8
/* Available buffers for the ENG to NET vFIFOs. */
#define       MC_CMD_SWITCH_GET_UNASSIGNED_BUFFERS_OUT_NET_OFST 0
/* Available buffers for the ENG to ENG and NET to ENG vFIFOs. */
#define       MC_CMD_SWITCH_GET_UNASSIGNED_BUFFERS_OUT_ENG_OFST 4


#endif /* MCDI_PCOL_H */
