// SPDX-License-Identifier: GPL-2.0
/*
 * xhci-dbc.h - xHCI debug capability early driver
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#ifndef __LINUX_XHCI_DBC_H
#define __LINUX_XHCI_DBC_H

#include <linux/types.h>
#include <linux/usb/ch9.h>

/*
 * xHCI Debug Capability Register interfaces:
 */
struct xdbc_regs {
	__le32	capability;
	__le32	doorbell;
	__le32	ersts;		/* Event Ring Segment Table Size*/
	__le32	__reserved_0;	/* 0c~0f reserved bits */
	__le64	erstba;		/* Event Ring Segment Table Base Address */
	__le64	erdp;		/* Event Ring Dequeue Pointer */
	__le32	control;
	__le32	status;
	__le32	portsc;		/* Port status and control */
	__le32	__reserved_1;	/* 2b~28 reserved bits */
	__le64	dccp;		/* Debug Capability Context Pointer */
	__le32	devinfo1;	/* Device Descriptor Info Register 1 */
	__le32	devinfo2;	/* Device Descriptor Info Register 2 */
};

#define DEBUG_MAX_BURST(p)	(((p) >> 16) & 0xff)

#define CTRL_DBC_RUN		BIT(0)
#define CTRL_PORT_ENABLE	BIT(1)
#define CTRL_HALT_OUT_TR	BIT(2)
#define CTRL_HALT_IN_TR		BIT(3)
#define CTRL_DBC_RUN_CHANGE	BIT(4)
#define CTRL_DBC_ENABLE		BIT(31)

#define DCST_DEBUG_PORT(p)	(((p) >> 24) & 0xff)

#define PORTSC_CONN_STATUS	BIT(0)
#define PORTSC_CONN_CHANGE	BIT(17)
#define PORTSC_RESET_CHANGE	BIT(21)
#define PORTSC_LINK_CHANGE	BIT(22)
#define PORTSC_CONFIG_CHANGE	BIT(23)

/*
 * xHCI Debug Capability data structures:
 */
struct xdbc_trb {
	__le32 field[4];
};

struct xdbc_erst_entry {
	__le64	seg_addr;
	__le32	seg_size;
	__le32	__reserved_0;
};

struct xdbc_info_context {
	__le64	string0;
	__le64	manufacturer;
	__le64	product;
	__le64	serial;
	__le32	length;
	__le32	__reserved_0[7];
};

struct xdbc_ep_context {
	__le32	ep_info1;
	__le32	ep_info2;
	__le64	deq;
	__le32	tx_info;
	__le32	__reserved_0[11];
};

struct xdbc_context {
	struct xdbc_info_context	info;
	struct xdbc_ep_context		out;
	struct xdbc_ep_context		in;
};

#define XDBC_INFO_CONTEXT_SIZE		48
#define XDBC_MAX_STRING_LENGTH		64
#define XDBC_STRING_MANUFACTURER	"Linux Foundation"
#define XDBC_STRING_PRODUCT		"Linux USB GDB Target"
#define XDBC_STRING_SERIAL		"0001"

struct xdbc_strings {
	char	string0[XDBC_MAX_STRING_LENGTH];
	char	manufacturer[XDBC_MAX_STRING_LENGTH];
	char	product[XDBC_MAX_STRING_LENGTH];
	char	serial[XDBC_MAX_STRING_LENGTH];
};

#define XDBC_PROTOCOL		1	/* GNU Remote Debug Command Set */
#define XDBC_VENDOR_ID		0x1d6b	/* Linux Foundation 0x1d6b */
#define XDBC_PRODUCT_ID		0x0011	/* __le16 idProduct; device 0011 */
#define XDBC_DEVICE_REV		0x0010	/* 0.10 */

/*
 * xHCI Debug Capability software state structures:
 */
struct xdbc_segment {
	struct xdbc_trb		*trbs;
	dma_addr_t		dma;
};

#define XDBC_TRBS_PER_SEGMENT	256

struct xdbc_ring {
	struct xdbc_segment	*segment;
	struct xdbc_trb		*enqueue;
	struct xdbc_trb		*dequeue;
	u32			cycle_state;
};

/*
 * These are the "Endpoint ID" (also known as "Context Index") values for the
 * OUT Transfer Ring and the IN Transfer Ring of a Debug Capability Context data
 * structure.
 * According to the "eXtensible Host Controller Interface for Universal Serial
 * Bus (xHCI)" specification, section "7.6.3.2 Endpoint Contexts and Transfer
 * Rings", these should be 0 and 1, and those are the values AMD machines give
 * you; but Intel machines seem to use the formula from section "4.5.1 Device
 * Context Index", which is supposed to be used for the Device Context only.
 * Luckily the values from Intel don't overlap with those from AMD, so we can
 * just test for both.
 */
#define XDBC_EPID_OUT		0
#define XDBC_EPID_IN		1
#define XDBC_EPID_OUT_INTEL	2
#define XDBC_EPID_IN_INTEL	3

struct xdbc_state {
	u16			vendor;
	u16			device;
	u32			bus;
	u32			dev;
	u32			func;
	void __iomem		*xhci_base;
	u64			xhci_start;
	size_t			xhci_length;
	int			port_number;

	/* DbC register base */
	struct xdbc_regs __iomem *xdbc_reg;

	/* DbC table page */
	dma_addr_t		table_dma;
	void			*table_base;

	/* event ring segment table */
	dma_addr_t		erst_dma;
	size_t			erst_size;
	void			*erst_base;

	/* event ring segments */
	struct xdbc_ring	evt_ring;
	struct xdbc_segment	evt_seg;

	/* debug capability contexts */
	dma_addr_t		dbcc_dma;
	size_t			dbcc_size;
	void			*dbcc_base;

	/* descriptor strings */
	dma_addr_t		string_dma;
	size_t			string_size;
	void			*string_base;

	/* bulk OUT endpoint */
	struct xdbc_ring	out_ring;
	struct xdbc_segment	out_seg;
	void			*out_buf;
	dma_addr_t		out_dma;

	/* bulk IN endpoint */
	struct xdbc_ring	in_ring;
	struct xdbc_segment	in_seg;
	void			*in_buf;
	dma_addr_t		in_dma;

	u32			flags;

	/* spinlock for early_xdbc_write() reentrancy */
	raw_spinlock_t		lock;
};

#define XDBC_PCI_MAX_BUSES	256
#define XDBC_PCI_MAX_DEVICES	32
#define XDBC_PCI_MAX_FUNCTION	8

#define XDBC_TABLE_ENTRY_SIZE	64
#define XDBC_ERST_ENTRY_NUM	1
#define XDBC_DBCC_ENTRY_NUM	3
#define XDBC_STRING_ENTRY_NUM	4

/* Bits definitions for xdbc_state.flags: */
#define XDBC_FLAGS_INITIALIZED	BIT(0)
#define XDBC_FLAGS_IN_STALL	BIT(1)
#define XDBC_FLAGS_OUT_STALL	BIT(2)
#define XDBC_FLAGS_IN_PROCESS	BIT(3)
#define XDBC_FLAGS_OUT_PROCESS	BIT(4)
#define XDBC_FLAGS_CONFIGURED	BIT(5)

#define XDBC_MAX_PACKET		1024

/* Door bell target: */
#define OUT_EP_DOORBELL		0
#define IN_EP_DOORBELL		1
#define DOOR_BELL_TARGET(p)	(((p) & 0xff) << 8)

#define xdbc_read64(regs)	xhci_read_64(NULL, (regs))
#define xdbc_write64(val, regs)	xhci_write_64(NULL, (val), (regs))

#endif /* __LINUX_XHCI_DBC_H */
