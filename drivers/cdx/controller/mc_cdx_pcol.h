/* SPDX-License-Identifier: GPL-2.0
 *
 * Driver for AMD network controllers and boards
 *
 * Copyright (C) 2021, Xilinx, Inc.
 * Copyright (C) 2022-2023, Advanced Micro Devices, Inc.
 */

#ifndef MC_CDX_PCOL_H
#define MC_CDX_PCOL_H

/* The current version of the MCDI protocol. */
#define MCDI_PCOL_VERSION		2

/*
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
 * The client writes its request into MC shared memory, and rings the
 * doorbell. Each request is completed either by the MC writing
 * back into shared memory, or by writing out an event.
 *
 * All MCDI commands support completion by shared memory response. Each
 * request may also contain additional data (accounted for by HEADER.LEN),
 * and some responses may also contain additional data (again, accounted
 * for by HEADER.LEN).
 *
 * Some MCDI commands support completion by event, in which any associated
 * response data is included in the event.
 *
 * The protocol requires one response to be delivered for every request; a
 * request should not be sent unless the response for the previous request
 * has been received (either by polling shared memory, or by receiving
 * an event).
 */

/** Request/Response structure */
#define MCDI_HEADER_OFST		0
#define MCDI_HEADER_CODE_LBN		0
#define MCDI_HEADER_CODE_WIDTH		7
#define MCDI_HEADER_RESYNC_LBN		7
#define MCDI_HEADER_RESYNC_WIDTH	1
#define MCDI_HEADER_DATALEN_LBN		8
#define MCDI_HEADER_DATALEN_WIDTH	8
#define MCDI_HEADER_SEQ_LBN		16
#define MCDI_HEADER_SEQ_WIDTH		4
#define MCDI_HEADER_RSVD_LBN		20
#define MCDI_HEADER_RSVD_WIDTH		1
#define MCDI_HEADER_NOT_EPOCH_LBN	21
#define MCDI_HEADER_NOT_EPOCH_WIDTH	1
#define MCDI_HEADER_ERROR_LBN		22
#define MCDI_HEADER_ERROR_WIDTH		1
#define MCDI_HEADER_RESPONSE_LBN	23
#define MCDI_HEADER_RESPONSE_WIDTH	1
#define MCDI_HEADER_XFLAGS_LBN		24
#define MCDI_HEADER_XFLAGS_WIDTH	8
/* Request response using event */
#define MCDI_HEADER_XFLAGS_EVREQ	0x01
/* Request (and signal) early doorbell return */
#define MCDI_HEADER_XFLAGS_DBRET	0x02

/* Maximum number of payload bytes */
#define MCDI_CTL_SDU_LEN_MAX_V2		0x400

#define MCDI_CTL_SDU_LEN_MAX MCDI_CTL_SDU_LEN_MAX_V2

/*
 * The MC can generate events for two reasons:
 *   - To advance a shared memory request if XFLAGS_EVREQ was set
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

/*
 * the errno value may be followed by the (0-based) number of the
 * first argument that could not be processed.
 */
#define MC_CMD_ERR_ARG_OFST		4

/* MC_CMD_ERR MCDI error codes. */
/* Operation not permitted. */
#define MC_CMD_ERR_EPERM		0x1
/* Non-existent command target */
#define MC_CMD_ERR_ENOENT		0x2
/* assert() has killed the MC */
#define MC_CMD_ERR_EINTR		0x4
/* I/O failure */
#define MC_CMD_ERR_EIO			0x5
/* Already exists */
#define MC_CMD_ERR_EEXIST		0x6
/* Try again */
#define MC_CMD_ERR_EAGAIN		0xb
/* Out of memory */
#define MC_CMD_ERR_ENOMEM		0xc
/* Caller does not hold required locks */
#define MC_CMD_ERR_EACCES		0xd
/* Resource is currently unavailable (e.g. lock contention) */
#define MC_CMD_ERR_EBUSY		0x10
/* No such device */
#define MC_CMD_ERR_ENODEV		0x13
/* Invalid argument to target */
#define MC_CMD_ERR_EINVAL		0x16
/* No space */
#define MC_CMD_ERR_ENOSPC		0x1c
/* Read-only */
#define MC_CMD_ERR_EROFS		0x1e
/* Broken pipe */
#define MC_CMD_ERR_EPIPE		0x20
/* Out of range */
#define MC_CMD_ERR_ERANGE		0x22
/* Non-recursive resource is already acquired */
#define MC_CMD_ERR_EDEADLK		0x23
/* Operation not implemented */
#define MC_CMD_ERR_ENOSYS		0x26
/* Operation timed out */
#define MC_CMD_ERR_ETIME		0x3e
/* Link has been severed */
#define MC_CMD_ERR_ENOLINK		0x43
/* Protocol error */
#define MC_CMD_ERR_EPROTO		0x47
/* Bad message */
#define MC_CMD_ERR_EBADMSG		0x4a
/* Operation not supported */
#define MC_CMD_ERR_ENOTSUP		0x5f
/* Address not available */
#define MC_CMD_ERR_EADDRNOTAVAIL	0x63
/* Not connected */
#define MC_CMD_ERR_ENOTCONN		0x6b
/* Operation already in progress */
#define MC_CMD_ERR_EALREADY		0x72
/* Stale handle. The handle references resource that no longer exists */
#define MC_CMD_ERR_ESTALE		0x74
/* Resource allocation failed. */
#define MC_CMD_ERR_ALLOC_FAIL		0x1000
/* V-adaptor not found. */
#define MC_CMD_ERR_NO_VADAPTOR		0x1001
/* EVB port not found. */
#define MC_CMD_ERR_NO_EVB_PORT		0x1002
/* V-switch not found. */
#define MC_CMD_ERR_NO_VSWITCH		0x1003
/* Too many VLAN tags. */
#define MC_CMD_ERR_VLAN_LIMIT		0x1004
/* Bad PCI function number. */
#define MC_CMD_ERR_BAD_PCI_FUNC		0x1005
/* Invalid VLAN mode. */
#define MC_CMD_ERR_BAD_VLAN_MODE	0x1006
/* Invalid v-switch type. */
#define MC_CMD_ERR_BAD_VSWITCH_TYPE	0x1007
/* Invalid v-port type. */
#define MC_CMD_ERR_BAD_VPORT_TYPE	0x1008
/* MAC address exists. */
#define MC_CMD_ERR_MAC_EXIST		0x1009
/* Slave core not present */
#define MC_CMD_ERR_SLAVE_NOT_PRESENT	0x100a
/* The datapath is disabled. */
#define MC_CMD_ERR_DATAPATH_DISABLED	0x100b
/* The requesting client is not a function */
#define MC_CMD_ERR_CLIENT_NOT_FN	0x100c
/*
 * The requested operation might require the command to be passed between
 * MCs, and the transport doesn't support that. Should only ever been seen over
 * the UART.
 */
#define MC_CMD_ERR_NO_PRIVILEGE		0x1013
/*
 * Workaround 26807 could not be turned on/off because some functions
 * have already installed filters. See the comment at
 * MC_CMD_WORKAROUND_BUG26807. May also returned for other operations such as
 * sub-variant switching.
 */
#define MC_CMD_ERR_FILTERS_PRESENT	0x1014
/* The clock whose frequency you've attempted to set doesn't exist */
#define MC_CMD_ERR_NO_CLOCK		0x1015
/*
 * Returned by MC_CMD_TESTASSERT if the action that should have caused an
 * assertion failed to do so.
 */
#define MC_CMD_ERR_UNREACHABLE		0x1016
/*
 * This command needs to be processed in the background but there were no
 * resources to do so. Send it again after a command has completed.
 */
#define MC_CMD_ERR_QUEUE_FULL		0x1017
/*
 * The operation could not be completed because the PCIe link has gone
 * away. This error code is never expected to be returned over the TLP
 * transport.
 */
#define MC_CMD_ERR_NO_PCIE		0x1018
/*
 * The operation could not be completed because the datapath has gone
 * away. This is distinct from MC_CMD_ERR_DATAPATH_DISABLED in that the
 * datapath absence may be temporary
 */
#define MC_CMD_ERR_NO_DATAPATH		0x1019
/* The operation could not complete because some VIs are allocated */
#define MC_CMD_ERR_VIS_PRESENT		0x101a
/*
 * The operation could not complete because some PIO buffers are
 * allocated
 */
#define MC_CMD_ERR_PIOBUFS_PRESENT	0x101b

/***********************************/
/*
 * MC_CMD_CDX_BUS_ENUM_BUSES
 * CDX bus hosts devices (functions) that are implemented using the Composable
 * DMA subsystem and directly mapped into the memory space of the FGPA PSX
 * Application Processors (APUs). As such, they only apply to the PSX APU side,
 * not the host (PCIe). Unlike PCIe, these devices have no native configuration
 * space or enumeration mechanism, so this message set provides a minimal
 * interface for discovery and management (bus reset, FLR, BME) of such
 * devices. This command returns the number of CDX buses present in the system.
 */
#define MC_CMD_CDX_BUS_ENUM_BUSES				0x1
#define MC_CMD_CDX_BUS_ENUM_BUSES_MSGSET			0x1
#undef MC_CMD_0x1_PRIVILEGE_CTG

#define MC_CMD_0x1_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CDX_BUS_ENUM_BUSES_IN msgrequest */
#define MC_CMD_CDX_BUS_ENUM_BUSES_IN_LEN			0

/* MC_CMD_CDX_BUS_ENUM_BUSES_OUT msgresponse */
#define MC_CMD_CDX_BUS_ENUM_BUSES_OUT_LEN			4
/*
 * Number of CDX buses present in the system. Buses are numbered 0 to
 * BUS_COUNT-1
 */
#define MC_CMD_CDX_BUS_ENUM_BUSES_OUT_BUS_COUNT_OFST		0
#define MC_CMD_CDX_BUS_ENUM_BUSES_OUT_BUS_COUNT_LEN		4

/***********************************/
/*
 * MC_CMD_CDX_BUS_ENUM_DEVICES
 * Enumerate CDX bus devices on a given bus
 */
#define MC_CMD_CDX_BUS_ENUM_DEVICES				0x2
#define MC_CMD_CDX_BUS_ENUM_DEVICES_MSGSET			0x2
#undef MC_CMD_0x2_PRIVILEGE_CTG

#define MC_CMD_0x2_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CDX_BUS_ENUM_DEVICES_IN msgrequest */
#define MC_CMD_CDX_BUS_ENUM_DEVICES_IN_LEN			4
/*
 * Bus number to enumerate, in range 0 to BUS_COUNT-1, as returned by
 * MC_CMD_CDX_BUS_ENUM_BUSES_OUT
 */
#define MC_CMD_CDX_BUS_ENUM_DEVICES_IN_BUS_OFST			0
#define MC_CMD_CDX_BUS_ENUM_DEVICES_IN_BUS_LEN			4

/* MC_CMD_CDX_BUS_ENUM_DEVICES_OUT msgresponse */
#define MC_CMD_CDX_BUS_ENUM_DEVICES_OUT_LEN			4
/*
 * Number of devices present on the bus. Devices on the bus are numbered 0 to
 * DEVICE_COUNT-1. Returns EAGAIN if number of devices unknown or if the target
 * devices are not ready (e.g. undergoing a bus reset)
 */
#define MC_CMD_CDX_BUS_ENUM_DEVICES_OUT_DEVICE_COUNT_OFST	0
#define MC_CMD_CDX_BUS_ENUM_DEVICES_OUT_DEVICE_COUNT_LEN	4

/***********************************/
/*
 * MC_CMD_CDX_BUS_GET_DEVICE_CONFIG
 * Returns device identification and MMIO/MSI resource data for a CDX device.
 * The expected usage is for the caller to first retrieve the number of devices
 * on the bus using MC_CMD_BUS_ENUM_DEVICES, then loop through the range (0,
 * DEVICE_COUNT - 1), retrieving device resource data. May return EAGAIN if the
 * number of exposed devices or device resources change during enumeration (due
 * to e.g. a PL reload / bus reset), in which case the caller is expected to
 * restart the enumeration loop. MMIO addresses are specified in terms of bus
 * addresses (prior to any potential IOMMU translation). For versal-net, these
 * are equivalent to APU physical addresses. Implementation note - for this to
 * work, the implementation needs to keep state (generation count) per client.
 */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG					0x3
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_MSGSET					0x3
#undef MC_CMD_0x3_PRIVILEGE_CTG

#define MC_CMD_0x3_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_IN msgrequest */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_IN_LEN					8
/* Device bus number, in range 0 to BUS_COUNT-1 */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_IN_BUS_OFST				0
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_IN_BUS_LEN				4
/* Device number relative to the bus, in range 0 to DEVICE_COUNT-1 for that bus */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_IN_DEVICE_OFST				4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_IN_DEVICE_LEN				4

/* MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT msgresponse */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_LEN				88
/* 16-bit Vendor identifier, compliant with PCI-SIG VendorID assignment. */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_VENDOR_ID_OFST			0
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_VENDOR_ID_LEN			2
/* 16-bit Device ID assigned by the vendor */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_ID_OFST			2
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_ID_LEN			2
/*
 * 16-bit Subsystem Vendor ID, , compliant with PCI-SIG VendorID assignment.
 * For further device differentiation, as required. 0 if unused.
 */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_SUBSYS_VENDOR_ID_OFST		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_SUBSYS_VENDOR_ID_LEN		2
/*
 * 16-bit Subsystem Device ID assigned by the vendor. For further device
 * differentiation, as required. 0 if unused.
 */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_SUBSYS_DEVICE_ID_OFST		6
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_SUBSYS_DEVICE_ID_LEN		2
/* 24-bit Device Class code, compliant with PCI-SIG Device Class codes */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_CLASS_OFST			8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_CLASS_LEN			3
/* 8-bit vendor-assigned revision */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_REVISION_OFST		11
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_DEVICE_REVISION_LEN		1
/* Reserved (alignment) */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_RESERVED_OFST			12
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_RESERVED_LEN			4
/* MMIO region 0 base address (bus address), 0 if unused */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_OFST		16
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_LEN		8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_LO_OFST		16
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_LO_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_LO_LBN		128
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_LO_WIDTH		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_HI_OFST		20
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_HI_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_HI_LBN		160
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_BASE_HI_WIDTH		32
/* MMIO region 0 size, 0 if unused */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_OFST		24
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_LEN		8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_LO_OFST		24
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_LO_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_LO_LBN		192
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_LO_WIDTH		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_HI_OFST		28
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_HI_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_HI_LBN		224
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION0_SIZE_HI_WIDTH		32
/* MMIO region 1 base address (bus address), 0 if unused */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_OFST		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_LEN		8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_LO_OFST		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_LO_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_LO_LBN		256
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_LO_WIDTH		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_HI_OFST		36
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_HI_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_HI_LBN		288
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_BASE_HI_WIDTH		32
/* MMIO region 1 size, 0 if unused */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_OFST		40
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_LEN		8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_LO_OFST		40
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_LO_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_LO_LBN		320
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_LO_WIDTH		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_HI_OFST		44
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_HI_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_HI_LBN		352
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION1_SIZE_HI_WIDTH		32
/* MMIO region 2 base address (bus address), 0 if unused */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_OFST		48
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_LEN		8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_LO_OFST		48
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_LO_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_LO_LBN		384
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_LO_WIDTH		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_HI_OFST		52
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_HI_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_HI_LBN		416
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_BASE_HI_WIDTH		32
/* MMIO region 2 size, 0 if unused */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_OFST		56
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_LEN		8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_LO_OFST		56
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_LO_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_LO_LBN		448
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_LO_WIDTH		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_HI_OFST		60
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_HI_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_HI_LBN		480
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION2_SIZE_HI_WIDTH		32
/* MMIO region 3 base address (bus address), 0 if unused */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_OFST		64
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_LEN		8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_LO_OFST		64
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_LO_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_LO_LBN		512
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_LO_WIDTH		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_HI_OFST		68
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_HI_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_HI_LBN		544
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_BASE_HI_WIDTH		32
/* MMIO region 3 size, 0 if unused */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_OFST		72
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_LEN		8
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_LO_OFST		72
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_LO_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_LO_LBN		576
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_LO_WIDTH		32
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_HI_OFST		76
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_HI_LEN		4
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_HI_LBN		608
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MMIO_REGION3_SIZE_HI_WIDTH		32
/* MSI vector count */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MSI_COUNT_OFST			80
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_MSI_COUNT_LEN			4
/* Requester ID used by device (SMMU StreamID, GIC ITS DeviceID) */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_REQUESTER_ID_OFST			84
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_REQUESTER_ID_LEN			4

/* MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_V2 msgresponse */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_V2_LEN				92
/* Requester ID used by device for GIC ITS DeviceID */
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_V2_REQUESTER_DEVICE_ID_OFST	88
#define MC_CMD_CDX_BUS_GET_DEVICE_CONFIG_OUT_V2_REQUESTER_DEVICE_ID_LEN		4

/***********************************/
/*
 * MC_CMD_CDX_BUS_DOWN
 * Asserting reset on the CDX bus causes all devices on the bus to be quiesced.
 * DMA bus mastering is disabled and any pending DMA request are flushed. Once
 * the response is returned, the devices are guaranteed to no longer issue DMA
 * requests or raise MSI interrupts. Further device MMIO accesses may have
 * undefined results. While the bus reset is asserted, any of the enumeration
 * or device configuration MCDIs will fail with EAGAIN. It is only legal to
 * reload the relevant PL region containing CDX devices if the corresponding CDX
 * bus is in reset. Depending on the implementation, the firmware may or may
 * not enforce this restriction and it is up to the caller to make sure this
 * requirement is satisfied.
 */
#define MC_CMD_CDX_BUS_DOWN					0x4
#define MC_CMD_CDX_BUS_DOWN_MSGSET			0x4

/* MC_CMD_CDX_BUS_DOWN_IN msgrequest */
#define MC_CMD_CDX_BUS_DOWN_IN_LEN			4
/* Bus number to put in reset, in range 0 to BUS_COUNT-1 */
#define MC_CMD_CDX_BUS_DOWN_IN_BUS_OFST		0
#define MC_CMD_CDX_BUS_DOWN_IN_BUS_LEN		4

/*
 * MC_CMD_CDX_BUS_DOWN_OUT msgresponse: The bus is quiesced, no further
 * upstream traffic for devices on this bus.
 */
#define MC_CMD_CDX_BUS_DOWN_OUT_LEN			0

/***********************************/
/*
 * MC_CMD_CDX_BUS_UP
 * After bus reset is de-asserted, devices are in a state which is functionally
 * equivalent to each device having been reset with MC_CMD_CDX_DEVICE_RESET. In
 * other words, device logic is reset in a hardware-specific way, MMIO accesses
 * are forwarded to the device, DMA bus mastering is disabled and needs to be
 * re-enabled with MC_CMD_CDX_DEVICE_DMA_ENABLE once the driver is ready to
 * start servicing DMA. If the underlying number of devices or device resources
 * changed (e.g. if PL was reloaded) while the bus was in reset, the bus driver
 * is expected to re-enumerate the bus. Returns EALREADY if the bus was already
 * up before the call.
 */
#define MC_CMD_CDX_BUS_UP					0x5
#define MC_CMD_CDX_BUS_UP_MSGSET			0x5

/* MC_CMD_CDX_BUS_UP_IN msgrequest */
#define MC_CMD_CDX_BUS_UP_IN_LEN			4
/* Bus number to take out of reset, in range 0 to BUS_COUNT-1 */
#define MC_CMD_CDX_BUS_UP_IN_BUS_OFST		0
#define MC_CMD_CDX_BUS_UP_IN_BUS_LEN		4

/* MC_CMD_CDX_BUS_UP_OUT msgresponse: The bus can now be enumerated. */
#define MC_CMD_CDX_BUS_UP_OUT_LEN			0

/***********************************/
/*
 * MC_CMD_CDX_DEVICE_RESET
 * After this call completes, device DMA and interrupts are quiesced, devices
 * logic is reset in a hardware-specific way and DMA bus mastering is disabled.
 */
#define MC_CMD_CDX_DEVICE_RESET				0x6
#define MC_CMD_CDX_DEVICE_RESET_MSGSET			0x6
#undef MC_CMD_0x6_PRIVILEGE_CTG

#define MC_CMD_0x6_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CDX_DEVICE_RESET_IN msgrequest */
#define MC_CMD_CDX_DEVICE_RESET_IN_LEN			8
/* Device bus number, in range 0 to BUS_COUNT-1 */
#define MC_CMD_CDX_DEVICE_RESET_IN_BUS_OFST		0
#define MC_CMD_CDX_DEVICE_RESET_IN_BUS_LEN		4
/* Device number relative to the bus, in range 0 to DEVICE_COUNT-1 for that bus */
#define MC_CMD_CDX_DEVICE_RESET_IN_DEVICE_OFST		4
#define MC_CMD_CDX_DEVICE_RESET_IN_DEVICE_LEN		4

/*
 * MC_CMD_CDX_DEVICE_RESET_OUT msgresponse: The device is quiesced and all
 * pending device initiated DMA has completed.
 */
#define MC_CMD_CDX_DEVICE_RESET_OUT_LEN 0

/***********************************/
/*
 * MC_CMD_CDX_DEVICE_CONTROL_SET
 * If BUS_MASTER is set to disabled, device DMA and interrupts are quiesced.
 * Pending DMA requests and MSI interrupts are flushed and no further DMA or
 * interrupts are issued after this command returns. If BUS_MASTER is set to
 * enabled, device is allowed to initiate DMA. Whether interrupts are enabled
 * also depends on the value of MSI_ENABLE bit. Note that, in this case, the
 * device may start DMA before the host receives and processes the MCDI
 * response. MSI_ENABLE masks or unmasks device interrupts only. Note that for
 * interrupts to be delivered to the host, both BUS_MASTER and MSI_ENABLE needs
 * to be set. MMIO_REGIONS_ENABLE enables or disables host accesses to device
 * MMIO regions. Note that an implementation is allowed to permanently set this
 * bit to 1, in which case MC_CMD_CDX_DEVICE_CONTROL_GET will always return 1
 * for this bit, regardless of the value set here.
 */
#define MC_CMD_CDX_DEVICE_CONTROL_SET					0x7
#define MC_CMD_CDX_DEVICE_CONTROL_SET_MSGSET				0x7
#undef MC_CMD_0x7_PRIVILEGE_CTG

#define MC_CMD_0x7_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CDX_DEVICE_CONTROL_SET_IN msgrequest */
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_LEN				12
/* Device bus number, in range 0 to BUS_COUNT-1 */
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_BUS_OFST			0
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_BUS_LEN			4
/* Device number relative to the bus, in range 0 to DEVICE_COUNT-1 for that bus */
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_DEVICE_OFST			4
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_DEVICE_LEN			4
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_FLAGS_OFST			8
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_FLAGS_LEN			4
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_BUS_MASTER_ENABLE_OFST		8
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_BUS_MASTER_ENABLE_LBN		0
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_BUS_MASTER_ENABLE_WIDTH	1
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_MSI_ENABLE_OFST		8
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_MSI_ENABLE_LBN			1
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_MSI_ENABLE_WIDTH		1
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_MMIO_REGIONS_ENABLE_OFST	8
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_MMIO_REGIONS_ENABLE_LBN	2
#define MC_CMD_CDX_DEVICE_CONTROL_SET_IN_MMIO_REGIONS_ENABLE_WIDTH	1

/* MC_CMD_CDX_DEVICE_CONTROL_SET_OUT msgresponse */
#define MC_CMD_CDX_DEVICE_CONTROL_SET_OUT_LEN				0

/***********************************/
/*
 * MC_CMD_CDX_DEVICE_CONTROL_GET
 * Returns device DMA, interrupt and MMIO region access control bits. See
 * MC_CMD_CDX_DEVICE_CONTROL_SET for definition of the available control bits.
 */
#define MC_CMD_CDX_DEVICE_CONTROL_GET					0x8
#define MC_CMD_CDX_DEVICE_CONTROL_GET_MSGSET				0x8
#undef MC_CMD_0x8_PRIVILEGE_CTG

#define MC_CMD_0x8_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CDX_DEVICE_CONTROL_GET_IN msgrequest */
#define MC_CMD_CDX_DEVICE_CONTROL_GET_IN_LEN				8
/* Device bus number, in range 0 to BUS_COUNT-1 */
#define MC_CMD_CDX_DEVICE_CONTROL_GET_IN_BUS_OFST			0
#define MC_CMD_CDX_DEVICE_CONTROL_GET_IN_BUS_LEN			4
/* Device number relative to the bus, in range 0 to DEVICE_COUNT-1 for that bus */
#define MC_CMD_CDX_DEVICE_CONTROL_GET_IN_DEVICE_OFST			4
#define MC_CMD_CDX_DEVICE_CONTROL_GET_IN_DEVICE_LEN			4

/* MC_CMD_CDX_DEVICE_CONTROL_GET_OUT msgresponse */
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_LEN				4
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_FLAGS_OFST			0
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_FLAGS_LEN			4
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_BUS_MASTER_ENABLE_OFST	0
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_BUS_MASTER_ENABLE_LBN		0
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_BUS_MASTER_ENABLE_WIDTH	1
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_MSI_ENABLE_OFST		0
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_MSI_ENABLE_LBN		1
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_MSI_ENABLE_WIDTH		1
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_MMIO_REGIONS_ENABLE_OFST	0
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_MMIO_REGIONS_ENABLE_LBN	2
#define MC_CMD_CDX_DEVICE_CONTROL_GET_OUT_MMIO_REGIONS_ENABLE_WIDTH	1

/***********************************/
/*
 * MC_CMD_CDX_DEVICE_WRITE_MSI_MSG
 * Populates the MSI message to be used by the hardware to raise the specified
 * interrupt vector. Versal-net implementation specific limitations are that
 * only 4 CDX devices with MSI interrupt capability are supported and all
 * vectors within a device must use the same write address. The command will
 * return EINVAL if any of these limitations is violated.
 */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG					0x9
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_MSGSET				0x9
#undef MC_CMD_0x9_PRIVILEGE_CTG

#define MC_CMD_0x9_PRIVILEGE_CTG SRIOV_CTG_ADMIN

/* MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN msgrequest */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_LEN				28
/* Device bus number, in range 0 to BUS_COUNT-1 */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_BUS_OFST			0
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_BUS_LEN			4
/* Device number relative to the bus, in range 0 to DEVICE_COUNT-1 for that bus */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_DEVICE_OFST			4
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_DEVICE_LEN			4
/*
 * Device-relative MSI vector number. Must be < MSI_COUNT reported for the
 * device.
 */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_VECTOR_OFST		8
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_VECTOR_LEN		4
/* Reserved (alignment) */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_RESERVED_OFST		12
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_RESERVED_LEN			4
/*
 * MSI address to be used by the hardware. Typically, on ARM systems this
 * address is translated by the IOMMU (if enabled) and it is the responsibility
 * of the entity managing the IOMMU (APU kernel) to supply the correct IOVA
 * here.
 */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_OFST		16
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_LEN		8
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_LO_OFST		16
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_LO_LEN		4
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_LO_LBN		128
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_LO_WIDTH		32
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_HI_OFST		20
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_HI_LEN		4
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_HI_LBN		160
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_ADDRESS_HI_WIDTH		32
/*
 * MSI data to be used by the hardware. On versal-net, only the lower 16-bits
 * are used, the remaining bits are ignored and should be set to zero.
 */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_DATA_OFST		24
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_IN_MSI_DATA_LEN			4

/* MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_OUT msgresponse */
#define MC_CMD_CDX_DEVICE_WRITE_MSI_MSG_OUT_LEN				0

/***********************************/
/* MC_CMD_V2_EXTN - Encapsulation for a v2 extended command */
#define MC_CMD_V2_EXTN					0x7f

/* MC_CMD_V2_EXTN_IN msgrequest */
#define MC_CMD_V2_EXTN_IN_LEN				4
/* the extended command number */
#define MC_CMD_V2_EXTN_IN_EXTENDED_CMD_LBN		0
#define MC_CMD_V2_EXTN_IN_EXTENDED_CMD_WIDTH		15
#define MC_CMD_V2_EXTN_IN_UNUSED_LBN			15
#define MC_CMD_V2_EXTN_IN_UNUSED_WIDTH			1
/* the actual length of the encapsulated command */
#define MC_CMD_V2_EXTN_IN_ACTUAL_LEN_LBN		16
#define MC_CMD_V2_EXTN_IN_ACTUAL_LEN_WIDTH		10
#define MC_CMD_V2_EXTN_IN_UNUSED2_LBN			26
#define MC_CMD_V2_EXTN_IN_UNUSED2_WIDTH			2
/* Type of command/response */
#define MC_CMD_V2_EXTN_IN_MESSAGE_TYPE_LBN		28
#define MC_CMD_V2_EXTN_IN_MESSAGE_TYPE_WIDTH		4
/*
 * enum: MCDI command directed to versal-net. MCDI responses of this type
 * are not defined.
 */
#define MC_CMD_V2_EXTN_IN_MCDI_MESSAGE_TYPE_PLATFORM	0x2

#endif /* MC_CDX_PCOL_H */
