// SPDX-License-Identifier: GPL-2.0
/*
 * xhci-dbc.c - xHCI debug capability early driver
 *
 * Copyright (C) 2016 Intel Corporation
 *
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ":%s: " fmt, __func__

#include <linux/console.h>
#include <linux/pci_regs.h>
#include <linux/pci_ids.h>
#include <linux/memblock.h>
#include <linux/io.h>
#include <asm/pci-direct.h>
#include <asm/fixmap.h>
#include <linux/bcd.h>
#include <linux/export.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/usb/xhci-dbgp.h>

#include "../host/xhci.h"
#include "xhci-dbc.h"

static struct xdbc_state xdbc;
static bool early_console_keep;

#ifdef XDBC_TRACE
#define	xdbc_trace	trace_printk
#else
static inline void xdbc_trace(const char *fmt, ...) { }
#endif /* XDBC_TRACE */

static void __iomem * __init xdbc_map_pci_mmio(u32 bus, u32 dev, u32 func)
{
	u64 val64, sz64, mask64;
	void __iomem *base;
	u32 val, sz;
	u8 byte;

	val = read_pci_config(bus, dev, func, PCI_BASE_ADDRESS_0);
	write_pci_config(bus, dev, func, PCI_BASE_ADDRESS_0, ~0);
	sz = read_pci_config(bus, dev, func, PCI_BASE_ADDRESS_0);
	write_pci_config(bus, dev, func, PCI_BASE_ADDRESS_0, val);

	if (val == 0xffffffff || sz == 0xffffffff) {
		pr_notice("invalid mmio bar\n");
		return NULL;
	}

	val64	= val & PCI_BASE_ADDRESS_MEM_MASK;
	sz64	= sz & PCI_BASE_ADDRESS_MEM_MASK;
	mask64	= PCI_BASE_ADDRESS_MEM_MASK;

	if ((val & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64) {
		val = read_pci_config(bus, dev, func, PCI_BASE_ADDRESS_0 + 4);
		write_pci_config(bus, dev, func, PCI_BASE_ADDRESS_0 + 4, ~0);
		sz = read_pci_config(bus, dev, func, PCI_BASE_ADDRESS_0 + 4);
		write_pci_config(bus, dev, func, PCI_BASE_ADDRESS_0 + 4, val);

		val64	|= (u64)val << 32;
		sz64	|= (u64)sz << 32;
		mask64	|= ~0ULL << 32;
	}

	sz64 &= mask64;

	if (!sz64) {
		pr_notice("invalid mmio address\n");
		return NULL;
	}

	sz64 = 1ULL << __ffs64(sz64);

	/* Check if the mem space is enabled: */
	byte = read_pci_config_byte(bus, dev, func, PCI_COMMAND);
	if (!(byte & PCI_COMMAND_MEMORY)) {
		byte |= PCI_COMMAND_MEMORY;
		write_pci_config_byte(bus, dev, func, PCI_COMMAND, byte);
	}

	xdbc.xhci_start = val64;
	xdbc.xhci_length = sz64;
	base = early_ioremap(val64, sz64);

	return base;
}

static void * __init xdbc_get_page(dma_addr_t *dma_addr)
{
	void *virt;

	virt = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!virt)
		return NULL;

	if (dma_addr)
		*dma_addr = (dma_addr_t)__pa(virt);

	return virt;
}

static u32 __init xdbc_find_dbgp(int xdbc_num, u32 *b, u32 *d, u32 *f)
{
	u32 bus, dev, func, class;

	for (bus = 0; bus < XDBC_PCI_MAX_BUSES; bus++) {
		for (dev = 0; dev < XDBC_PCI_MAX_DEVICES; dev++) {
			for (func = 0; func < XDBC_PCI_MAX_FUNCTION; func++) {

				class = read_pci_config(bus, dev, func, PCI_CLASS_REVISION);
				if ((class >> 8) != PCI_CLASS_SERIAL_USB_XHCI)
					continue;

				if (xdbc_num-- != 0)
					continue;

				*b = bus;
				*d = dev;
				*f = func;

				return 0;
			}
		}
	}

	return -1;
}

static int handshake(void __iomem *ptr, u32 mask, u32 done, int wait, int delay)
{
	u32 result;

	/* Can not use readl_poll_timeout_atomic() for early boot things */
	do {
		result = readl(ptr);
		result &= mask;
		if (result == done)
			return 0;
		udelay(delay);
		wait -= delay;
	} while (wait > 0);

	return -ETIMEDOUT;
}

static void __init xdbc_bios_handoff(void)
{
	int offset, timeout;
	u32 val;

	offset = xhci_find_next_ext_cap(xdbc.xhci_base, 0, XHCI_EXT_CAPS_LEGACY);
	val = readl(xdbc.xhci_base + offset);

	if (val & XHCI_HC_BIOS_OWNED) {
		writel(val | XHCI_HC_OS_OWNED, xdbc.xhci_base + offset);
		timeout = handshake(xdbc.xhci_base + offset, XHCI_HC_BIOS_OWNED, 0, 5000, 10);

		if (timeout) {
			pr_notice("failed to hand over xHCI control from BIOS\n");
			writel(val & ~XHCI_HC_BIOS_OWNED, xdbc.xhci_base + offset);
		}
	}

	/* Disable BIOS SMIs and clear all SMI events: */
	val = readl(xdbc.xhci_base + offset + XHCI_LEGACY_CONTROL_OFFSET);
	val &= XHCI_LEGACY_DISABLE_SMI;
	val |= XHCI_LEGACY_SMI_EVENTS;
	writel(val, xdbc.xhci_base + offset + XHCI_LEGACY_CONTROL_OFFSET);
}

static int __init
xdbc_alloc_ring(struct xdbc_segment *seg, struct xdbc_ring *ring)
{
	seg->trbs = xdbc_get_page(&seg->dma);
	if (!seg->trbs)
		return -ENOMEM;

	ring->segment = seg;

	return 0;
}

static void __init xdbc_free_ring(struct xdbc_ring *ring)
{
	struct xdbc_segment *seg = ring->segment;

	if (!seg)
		return;

	memblock_phys_free(seg->dma, PAGE_SIZE);
	ring->segment = NULL;
}

static void xdbc_reset_ring(struct xdbc_ring *ring)
{
	struct xdbc_segment *seg = ring->segment;
	struct xdbc_trb *link_trb;

	memset(seg->trbs, 0, PAGE_SIZE);

	ring->enqueue = seg->trbs;
	ring->dequeue = seg->trbs;
	ring->cycle_state = 1;

	if (ring != &xdbc.evt_ring) {
		link_trb = &seg->trbs[XDBC_TRBS_PER_SEGMENT - 1];
		link_trb->field[0] = cpu_to_le32(lower_32_bits(seg->dma));
		link_trb->field[1] = cpu_to_le32(upper_32_bits(seg->dma));
		link_trb->field[3] = cpu_to_le32(TRB_TYPE(TRB_LINK)) | cpu_to_le32(LINK_TOGGLE);
	}
}

static inline void xdbc_put_utf16(u16 *s, const char *c, size_t size)
{
	int i;

	for (i = 0; i < size; i++)
		s[i] = cpu_to_le16(c[i]);
}

static void xdbc_mem_init(void)
{
	struct xdbc_ep_context *ep_in, *ep_out;
	struct usb_string_descriptor *s_desc;
	struct xdbc_erst_entry *entry;
	struct xdbc_strings *strings;
	struct xdbc_context *ctx;
	unsigned int max_burst;
	u32 string_length;
	int index = 0;
	u32 dev_info;

	xdbc_reset_ring(&xdbc.evt_ring);
	xdbc_reset_ring(&xdbc.in_ring);
	xdbc_reset_ring(&xdbc.out_ring);
	memset(xdbc.table_base, 0, PAGE_SIZE);
	memset(xdbc.out_buf, 0, PAGE_SIZE);

	/* Initialize event ring segment table: */
	xdbc.erst_size	= 16;
	xdbc.erst_base	= xdbc.table_base + index * XDBC_TABLE_ENTRY_SIZE;
	xdbc.erst_dma	= xdbc.table_dma + index * XDBC_TABLE_ENTRY_SIZE;

	index += XDBC_ERST_ENTRY_NUM;
	entry = (struct xdbc_erst_entry *)xdbc.erst_base;

	entry->seg_addr		= cpu_to_le64(xdbc.evt_seg.dma);
	entry->seg_size		= cpu_to_le32(XDBC_TRBS_PER_SEGMENT);
	entry->__reserved_0	= 0;

	/* Initialize ERST registers: */
	writel(1, &xdbc.xdbc_reg->ersts);
	xdbc_write64(xdbc.erst_dma, &xdbc.xdbc_reg->erstba);
	xdbc_write64(xdbc.evt_seg.dma, &xdbc.xdbc_reg->erdp);

	/* Debug capability contexts: */
	xdbc.dbcc_size	= 64 * 3;
	xdbc.dbcc_base	= xdbc.table_base + index * XDBC_TABLE_ENTRY_SIZE;
	xdbc.dbcc_dma	= xdbc.table_dma + index * XDBC_TABLE_ENTRY_SIZE;

	index += XDBC_DBCC_ENTRY_NUM;

	/* Popluate the strings: */
	xdbc.string_size = sizeof(struct xdbc_strings);
	xdbc.string_base = xdbc.table_base + index * XDBC_TABLE_ENTRY_SIZE;
	xdbc.string_dma	 = xdbc.table_dma + index * XDBC_TABLE_ENTRY_SIZE;
	strings		 = (struct xdbc_strings *)xdbc.string_base;

	index += XDBC_STRING_ENTRY_NUM;

	/* Serial string: */
	s_desc			= (struct usb_string_descriptor *)strings->serial;
	s_desc->bLength		= (strlen(XDBC_STRING_SERIAL) + 1) * 2;
	s_desc->bDescriptorType	= USB_DT_STRING;

	xdbc_put_utf16(s_desc->wData, XDBC_STRING_SERIAL, strlen(XDBC_STRING_SERIAL));
	string_length = s_desc->bLength;
	string_length <<= 8;

	/* Product string: */
	s_desc			= (struct usb_string_descriptor *)strings->product;
	s_desc->bLength		= (strlen(XDBC_STRING_PRODUCT) + 1) * 2;
	s_desc->bDescriptorType	= USB_DT_STRING;

	xdbc_put_utf16(s_desc->wData, XDBC_STRING_PRODUCT, strlen(XDBC_STRING_PRODUCT));
	string_length += s_desc->bLength;
	string_length <<= 8;

	/* Manufacture string: */
	s_desc			= (struct usb_string_descriptor *)strings->manufacturer;
	s_desc->bLength		= (strlen(XDBC_STRING_MANUFACTURER) + 1) * 2;
	s_desc->bDescriptorType	= USB_DT_STRING;

	xdbc_put_utf16(s_desc->wData, XDBC_STRING_MANUFACTURER, strlen(XDBC_STRING_MANUFACTURER));
	string_length += s_desc->bLength;
	string_length <<= 8;

	/* String0: */
	strings->string0[0]	= 4;
	strings->string0[1]	= USB_DT_STRING;
	strings->string0[2]	= 0x09;
	strings->string0[3]	= 0x04;

	string_length += 4;

	/* Populate info Context: */
	ctx = (struct xdbc_context *)xdbc.dbcc_base;

	ctx->info.string0	= cpu_to_le64(xdbc.string_dma);
	ctx->info.manufacturer	= cpu_to_le64(xdbc.string_dma + XDBC_MAX_STRING_LENGTH);
	ctx->info.product	= cpu_to_le64(xdbc.string_dma + XDBC_MAX_STRING_LENGTH * 2);
	ctx->info.serial	= cpu_to_le64(xdbc.string_dma + XDBC_MAX_STRING_LENGTH * 3);
	ctx->info.length	= cpu_to_le32(string_length);

	/* Populate bulk out endpoint context: */
	max_burst = DEBUG_MAX_BURST(readl(&xdbc.xdbc_reg->control));
	ep_out = (struct xdbc_ep_context *)&ctx->out;

	ep_out->ep_info1	= 0;
	ep_out->ep_info2	= cpu_to_le32(EP_TYPE(BULK_OUT_EP) | MAX_PACKET(1024) | MAX_BURST(max_burst));
	ep_out->deq		= cpu_to_le64(xdbc.out_seg.dma | xdbc.out_ring.cycle_state);

	/* Populate bulk in endpoint context: */
	ep_in = (struct xdbc_ep_context *)&ctx->in;

	ep_in->ep_info1		= 0;
	ep_in->ep_info2		= cpu_to_le32(EP_TYPE(BULK_IN_EP) | MAX_PACKET(1024) | MAX_BURST(max_burst));
	ep_in->deq		= cpu_to_le64(xdbc.in_seg.dma | xdbc.in_ring.cycle_state);

	/* Set DbC context and info registers: */
	xdbc_write64(xdbc.dbcc_dma, &xdbc.xdbc_reg->dccp);

	dev_info = cpu_to_le32((XDBC_VENDOR_ID << 16) | XDBC_PROTOCOL);
	writel(dev_info, &xdbc.xdbc_reg->devinfo1);

	dev_info = cpu_to_le32((XDBC_DEVICE_REV << 16) | XDBC_PRODUCT_ID);
	writel(dev_info, &xdbc.xdbc_reg->devinfo2);

	xdbc.in_buf = xdbc.out_buf + XDBC_MAX_PACKET;
	xdbc.in_dma = xdbc.out_dma + XDBC_MAX_PACKET;
}

static void xdbc_do_reset_debug_port(u32 id, u32 count)
{
	void __iomem *ops_reg;
	void __iomem *portsc;
	u32 val, cap_length;
	int i;

	cap_length = readl(xdbc.xhci_base) & 0xff;
	ops_reg = xdbc.xhci_base + cap_length;

	id--;
	for (i = id; i < (id + count); i++) {
		portsc = ops_reg + 0x400 + i * 0x10;
		val = readl(portsc);
		if (!(val & PORT_CONNECT))
			writel(val | PORT_RESET, portsc);
	}
}

static void xdbc_reset_debug_port(void)
{
	u32 val, port_offset, port_count;
	int offset = 0;

	do {
		offset = xhci_find_next_ext_cap(xdbc.xhci_base, offset, XHCI_EXT_CAPS_PROTOCOL);
		if (!offset)
			break;

		val = readl(xdbc.xhci_base + offset);
		if (XHCI_EXT_PORT_MAJOR(val) != 0x3)
			continue;

		val = readl(xdbc.xhci_base + offset + 8);
		port_offset = XHCI_EXT_PORT_OFF(val);
		port_count = XHCI_EXT_PORT_COUNT(val);

		xdbc_do_reset_debug_port(port_offset, port_count);
	} while (1);
}

static void
xdbc_queue_trb(struct xdbc_ring *ring, u32 field1, u32 field2, u32 field3, u32 field4)
{
	struct xdbc_trb *trb, *link_trb;

	trb = ring->enqueue;
	trb->field[0] = cpu_to_le32(field1);
	trb->field[1] = cpu_to_le32(field2);
	trb->field[2] = cpu_to_le32(field3);
	trb->field[3] = cpu_to_le32(field4);

	++(ring->enqueue);
	if (ring->enqueue >= &ring->segment->trbs[TRBS_PER_SEGMENT - 1]) {
		link_trb = ring->enqueue;
		if (ring->cycle_state)
			link_trb->field[3] |= cpu_to_le32(TRB_CYCLE);
		else
			link_trb->field[3] &= cpu_to_le32(~TRB_CYCLE);

		ring->enqueue = ring->segment->trbs;
		ring->cycle_state ^= 1;
	}
}

static void xdbc_ring_doorbell(int target)
{
	writel(DOOR_BELL_TARGET(target), &xdbc.xdbc_reg->doorbell);
}

static int xdbc_start(void)
{
	u32 ctrl, status;
	int ret;

	ctrl = readl(&xdbc.xdbc_reg->control);
	writel(ctrl | CTRL_DBC_ENABLE | CTRL_PORT_ENABLE, &xdbc.xdbc_reg->control);
	ret = handshake(&xdbc.xdbc_reg->control, CTRL_DBC_ENABLE, CTRL_DBC_ENABLE, 100000, 100);
	if (ret) {
		xdbc_trace("failed to initialize hardware\n");
		return ret;
	}

	/* Reset port to avoid bus hang: */
	if (xdbc.vendor == PCI_VENDOR_ID_INTEL)
		xdbc_reset_debug_port();

	/* Wait for port connection: */
	ret = handshake(&xdbc.xdbc_reg->portsc, PORTSC_CONN_STATUS, PORTSC_CONN_STATUS, 5000000, 100);
	if (ret) {
		xdbc_trace("waiting for connection timed out\n");
		return ret;
	}

	/* Wait for debug device to be configured: */
	ret = handshake(&xdbc.xdbc_reg->control, CTRL_DBC_RUN, CTRL_DBC_RUN, 5000000, 100);
	if (ret) {
		xdbc_trace("waiting for device configuration timed out\n");
		return ret;
	}

	/* Check port number: */
	status = readl(&xdbc.xdbc_reg->status);
	if (!DCST_DEBUG_PORT(status)) {
		xdbc_trace("invalid root hub port number\n");
		return -ENODEV;
	}

	xdbc.port_number = DCST_DEBUG_PORT(status);

	xdbc_trace("DbC is running now, control 0x%08x port ID %d\n",
		   readl(&xdbc.xdbc_reg->control), xdbc.port_number);

	return 0;
}

static int xdbc_bulk_transfer(void *data, int size, bool read)
{
	struct xdbc_ring *ring;
	struct xdbc_trb *trb;
	u32 length, control;
	u32 cycle;
	u64 addr;

	if (size > XDBC_MAX_PACKET) {
		xdbc_trace("bad parameter, size %d\n", size);
		return -EINVAL;
	}

	if (!(xdbc.flags & XDBC_FLAGS_INITIALIZED) ||
	    !(xdbc.flags & XDBC_FLAGS_CONFIGURED) ||
	    (!read && (xdbc.flags & XDBC_FLAGS_OUT_STALL)) ||
	    (read && (xdbc.flags & XDBC_FLAGS_IN_STALL))) {

		xdbc_trace("connection not ready, flags %08x\n", xdbc.flags);
		return -EIO;
	}

	ring = (read ? &xdbc.in_ring : &xdbc.out_ring);
	trb = ring->enqueue;
	cycle = ring->cycle_state;
	length = TRB_LEN(size);
	control = TRB_TYPE(TRB_NORMAL) | TRB_IOC;

	if (cycle)
		control &= cpu_to_le32(~TRB_CYCLE);
	else
		control |= cpu_to_le32(TRB_CYCLE);

	if (read) {
		memset(xdbc.in_buf, 0, XDBC_MAX_PACKET);
		addr = xdbc.in_dma;
		xdbc.flags |= XDBC_FLAGS_IN_PROCESS;
	} else {
		memset(xdbc.out_buf, 0, XDBC_MAX_PACKET);
		memcpy(xdbc.out_buf, data, size);
		addr = xdbc.out_dma;
		xdbc.flags |= XDBC_FLAGS_OUT_PROCESS;
	}

	xdbc_queue_trb(ring, lower_32_bits(addr), upper_32_bits(addr), length, control);

	/*
	 * Add a barrier between writes of trb fields and flipping
	 * the cycle bit:
	 */
	wmb();
	if (cycle)
		trb->field[3] |= cpu_to_le32(cycle);
	else
		trb->field[3] &= cpu_to_le32(~TRB_CYCLE);

	xdbc_ring_doorbell(read ? IN_EP_DOORBELL : OUT_EP_DOORBELL);

	return size;
}

static int xdbc_handle_external_reset(void)
{
	int ret = 0;

	xdbc.flags = 0;
	writel(0, &xdbc.xdbc_reg->control);
	ret = handshake(&xdbc.xdbc_reg->control, CTRL_DBC_ENABLE, 0, 100000, 10);
	if (ret)
		goto reset_out;

	xdbc_mem_init();

	ret = xdbc_start();
	if (ret < 0)
		goto reset_out;

	xdbc_trace("dbc recovered\n");

	xdbc.flags |= XDBC_FLAGS_INITIALIZED | XDBC_FLAGS_CONFIGURED;

	xdbc_bulk_transfer(NULL, XDBC_MAX_PACKET, true);

	return 0;

reset_out:
	xdbc_trace("failed to recover from external reset\n");
	return ret;
}

static int __init xdbc_early_setup(void)
{
	int ret;

	writel(0, &xdbc.xdbc_reg->control);
	ret = handshake(&xdbc.xdbc_reg->control, CTRL_DBC_ENABLE, 0, 100000, 100);
	if (ret)
		return ret;

	/* Allocate the table page: */
	xdbc.table_base = xdbc_get_page(&xdbc.table_dma);
	if (!xdbc.table_base)
		return -ENOMEM;

	/* Get and store the transfer buffer: */
	xdbc.out_buf = xdbc_get_page(&xdbc.out_dma);
	if (!xdbc.out_buf)
		return -ENOMEM;

	/* Allocate the event ring: */
	ret = xdbc_alloc_ring(&xdbc.evt_seg, &xdbc.evt_ring);
	if (ret < 0)
		return ret;

	/* Allocate IN/OUT endpoint transfer rings: */
	ret = xdbc_alloc_ring(&xdbc.in_seg, &xdbc.in_ring);
	if (ret < 0)
		return ret;

	ret = xdbc_alloc_ring(&xdbc.out_seg, &xdbc.out_ring);
	if (ret < 0)
		return ret;

	xdbc_mem_init();

	ret = xdbc_start();
	if (ret < 0) {
		writel(0, &xdbc.xdbc_reg->control);
		return ret;
	}

	xdbc.flags |= XDBC_FLAGS_INITIALIZED | XDBC_FLAGS_CONFIGURED;

	xdbc_bulk_transfer(NULL, XDBC_MAX_PACKET, true);

	return 0;
}

int __init early_xdbc_parse_parameter(char *s, int keep_early)
{
	unsigned long dbgp_num = 0;
	u32 bus, dev, func, offset;
	char *e;
	int ret;

	if (!early_pci_allowed())
		return -EPERM;

	early_console_keep = keep_early;

	if (xdbc.xdbc_reg)
		return 0;

	if (*s) {
	       dbgp_num = simple_strtoul(s, &e, 10);
	       if (s == e)
		       dbgp_num = 0;
	}

	pr_notice("dbgp_num: %lu\n", dbgp_num);

	/* Locate the host controller: */
	ret = xdbc_find_dbgp(dbgp_num, &bus, &dev, &func);
	if (ret) {
		pr_notice("failed to locate xhci host\n");
		return -ENODEV;
	}

	xdbc.vendor	= read_pci_config_16(bus, dev, func, PCI_VENDOR_ID);
	xdbc.device	= read_pci_config_16(bus, dev, func, PCI_DEVICE_ID);
	xdbc.bus	= bus;
	xdbc.dev	= dev;
	xdbc.func	= func;

	/* Map the IO memory: */
	xdbc.xhci_base = xdbc_map_pci_mmio(bus, dev, func);
	if (!xdbc.xhci_base)
		return -EINVAL;

	/* Locate DbC registers: */
	offset = xhci_find_next_ext_cap(xdbc.xhci_base, 0, XHCI_EXT_CAPS_DEBUG);
	if (!offset) {
		pr_notice("xhci host doesn't support debug capability\n");
		early_iounmap(xdbc.xhci_base, xdbc.xhci_length);
		xdbc.xhci_base = NULL;
		xdbc.xhci_length = 0;

		return -ENODEV;
	}
	xdbc.xdbc_reg = (struct xdbc_regs __iomem *)(xdbc.xhci_base + offset);

	return 0;
}

int __init early_xdbc_setup_hardware(void)
{
	int ret;

	if (!xdbc.xdbc_reg)
		return -ENODEV;

	xdbc_bios_handoff();

	raw_spin_lock_init(&xdbc.lock);

	ret = xdbc_early_setup();
	if (ret) {
		pr_notice("failed to setup the connection to host\n");

		xdbc_free_ring(&xdbc.evt_ring);
		xdbc_free_ring(&xdbc.out_ring);
		xdbc_free_ring(&xdbc.in_ring);

		if (xdbc.table_dma)
			memblock_phys_free(xdbc.table_dma, PAGE_SIZE);

		if (xdbc.out_dma)
			memblock_phys_free(xdbc.out_dma, PAGE_SIZE);

		xdbc.table_base = NULL;
		xdbc.out_buf = NULL;
	}

	return ret;
}

static void xdbc_handle_port_status(struct xdbc_trb *evt_trb)
{
	u32 port_reg;

	port_reg = readl(&xdbc.xdbc_reg->portsc);
	if (port_reg & PORTSC_CONN_CHANGE) {
		xdbc_trace("connect status change event\n");

		/* Check whether cable unplugged: */
		if (!(port_reg & PORTSC_CONN_STATUS)) {
			xdbc.flags = 0;
			xdbc_trace("cable unplugged\n");
		}
	}

	if (port_reg & PORTSC_RESET_CHANGE)
		xdbc_trace("port reset change event\n");

	if (port_reg & PORTSC_LINK_CHANGE)
		xdbc_trace("port link status change event\n");

	if (port_reg & PORTSC_CONFIG_CHANGE)
		xdbc_trace("config error change\n");

	/* Write back the value to clear RW1C bits: */
	writel(port_reg, &xdbc.xdbc_reg->portsc);
}

static void xdbc_handle_tx_event(struct xdbc_trb *evt_trb)
{
	u32 comp_code;
	int ep_id;

	comp_code	= GET_COMP_CODE(le32_to_cpu(evt_trb->field[2]));
	ep_id		= TRB_TO_EP_ID(le32_to_cpu(evt_trb->field[3]));

	switch (comp_code) {
	case COMP_SUCCESS:
	case COMP_SHORT_PACKET:
		break;
	case COMP_TRB_ERROR:
	case COMP_BABBLE_DETECTED_ERROR:
	case COMP_USB_TRANSACTION_ERROR:
	case COMP_STALL_ERROR:
	default:
		if (ep_id == XDBC_EPID_OUT || ep_id == XDBC_EPID_OUT_INTEL)
			xdbc.flags |= XDBC_FLAGS_OUT_STALL;
		if (ep_id == XDBC_EPID_IN || ep_id == XDBC_EPID_IN_INTEL)
			xdbc.flags |= XDBC_FLAGS_IN_STALL;

		xdbc_trace("endpoint %d stalled\n", ep_id);
		break;
	}

	if (ep_id == XDBC_EPID_IN || ep_id == XDBC_EPID_IN_INTEL) {
		xdbc.flags &= ~XDBC_FLAGS_IN_PROCESS;
		xdbc_bulk_transfer(NULL, XDBC_MAX_PACKET, true);
	} else if (ep_id == XDBC_EPID_OUT || ep_id == XDBC_EPID_OUT_INTEL) {
		xdbc.flags &= ~XDBC_FLAGS_OUT_PROCESS;
	} else {
		xdbc_trace("invalid endpoint id %d\n", ep_id);
	}
}

static void xdbc_handle_events(void)
{
	struct xdbc_trb *evt_trb;
	bool update_erdp = false;
	u32 reg;
	u8 cmd;

	cmd = read_pci_config_byte(xdbc.bus, xdbc.dev, xdbc.func, PCI_COMMAND);
	if (!(cmd & PCI_COMMAND_MASTER)) {
		cmd |= PCI_COMMAND_MASTER | PCI_COMMAND_MEMORY;
		write_pci_config_byte(xdbc.bus, xdbc.dev, xdbc.func, PCI_COMMAND, cmd);
	}

	if (!(xdbc.flags & XDBC_FLAGS_INITIALIZED))
		return;

	/* Handle external reset events: */
	reg = readl(&xdbc.xdbc_reg->control);
	if (!(reg & CTRL_DBC_ENABLE)) {
		if (xdbc_handle_external_reset()) {
			xdbc_trace("failed to recover connection\n");
			return;
		}
	}

	/* Handle configure-exit event: */
	reg = readl(&xdbc.xdbc_reg->control);
	if (reg & CTRL_DBC_RUN_CHANGE) {
		writel(reg, &xdbc.xdbc_reg->control);
		if (reg & CTRL_DBC_RUN)
			xdbc.flags |= XDBC_FLAGS_CONFIGURED;
		else
			xdbc.flags &= ~XDBC_FLAGS_CONFIGURED;
	}

	/* Handle endpoint stall event: */
	reg = readl(&xdbc.xdbc_reg->control);
	if (reg & CTRL_HALT_IN_TR) {
		xdbc.flags |= XDBC_FLAGS_IN_STALL;
	} else {
		xdbc.flags &= ~XDBC_FLAGS_IN_STALL;
		if (!(xdbc.flags & XDBC_FLAGS_IN_PROCESS))
			xdbc_bulk_transfer(NULL, XDBC_MAX_PACKET, true);
	}

	if (reg & CTRL_HALT_OUT_TR)
		xdbc.flags |= XDBC_FLAGS_OUT_STALL;
	else
		xdbc.flags &= ~XDBC_FLAGS_OUT_STALL;

	/* Handle the events in the event ring: */
	evt_trb = xdbc.evt_ring.dequeue;
	while ((le32_to_cpu(evt_trb->field[3]) & TRB_CYCLE) == xdbc.evt_ring.cycle_state) {
		/*
		 * Add a barrier between reading the cycle flag and any
		 * reads of the event's flags/data below:
		 */
		rmb();

		switch ((le32_to_cpu(evt_trb->field[3]) & TRB_TYPE_BITMASK)) {
		case TRB_TYPE(TRB_PORT_STATUS):
			xdbc_handle_port_status(evt_trb);
			break;
		case TRB_TYPE(TRB_TRANSFER):
			xdbc_handle_tx_event(evt_trb);
			break;
		default:
			break;
		}

		++(xdbc.evt_ring.dequeue);
		if (xdbc.evt_ring.dequeue == &xdbc.evt_seg.trbs[TRBS_PER_SEGMENT]) {
			xdbc.evt_ring.dequeue = xdbc.evt_seg.trbs;
			xdbc.evt_ring.cycle_state ^= 1;
		}

		evt_trb = xdbc.evt_ring.dequeue;
		update_erdp = true;
	}

	/* Update event ring dequeue pointer: */
	if (update_erdp)
		xdbc_write64(__pa(xdbc.evt_ring.dequeue), &xdbc.xdbc_reg->erdp);
}

static int xdbc_bulk_write(const char *bytes, int size)
{
	int ret, timeout = 0;
	unsigned long flags;

retry:
	if (in_nmi()) {
		if (!raw_spin_trylock_irqsave(&xdbc.lock, flags))
			return -EAGAIN;
	} else {
		raw_spin_lock_irqsave(&xdbc.lock, flags);
	}

	xdbc_handle_events();

	/* Check completion of the previous request: */
	if ((xdbc.flags & XDBC_FLAGS_OUT_PROCESS) && (timeout < 2000000)) {
		raw_spin_unlock_irqrestore(&xdbc.lock, flags);
		udelay(100);
		timeout += 100;
		goto retry;
	}

	if (xdbc.flags & XDBC_FLAGS_OUT_PROCESS) {
		raw_spin_unlock_irqrestore(&xdbc.lock, flags);
		xdbc_trace("previous transfer not completed yet\n");

		return -ETIMEDOUT;
	}

	ret = xdbc_bulk_transfer((void *)bytes, size, false);
	raw_spin_unlock_irqrestore(&xdbc.lock, flags);

	return ret;
}

static void early_xdbc_write(struct console *con, const char *str, u32 n)
{
	/* static variables are zeroed, so buf is always NULL terminated */
	static char buf[XDBC_MAX_PACKET + 1];
	int chunk, ret;
	int use_cr = 0;

	if (!xdbc.xdbc_reg)
		return;
	memset(buf, 0, XDBC_MAX_PACKET);
	while (n > 0) {
		for (chunk = 0; chunk < XDBC_MAX_PACKET && n > 0; str++, chunk++, n--) {

			if (!use_cr && *str == '\n') {
				use_cr = 1;
				buf[chunk] = '\r';
				str--;
				n++;
				continue;
			}

			if (use_cr)
				use_cr = 0;
			buf[chunk] = *str;
		}

		if (chunk > 0) {
			ret = xdbc_bulk_write(buf, chunk);
			if (ret < 0)
				xdbc_trace("missed message {%s}\n", buf);
		}
	}
}

static struct console early_xdbc_console = {
	.name		= "earlyxdbc",
	.write		= early_xdbc_write,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
};

void __init early_xdbc_register_console(void)
{
	if (early_console)
		return;

	early_console = &early_xdbc_console;
	if (early_console_keep)
		early_console->flags &= ~CON_BOOT;
	else
		early_console->flags |= CON_BOOT;
	register_console(early_console);
}

static void xdbc_unregister_console(void)
{
	if (console_is_registered(&early_xdbc_console))
		unregister_console(&early_xdbc_console);
}

static int xdbc_scrub_function(void *ptr)
{
	unsigned long flags;

	while (true) {
		raw_spin_lock_irqsave(&xdbc.lock, flags);
		xdbc_handle_events();

		if (!(xdbc.flags & XDBC_FLAGS_INITIALIZED)) {
			raw_spin_unlock_irqrestore(&xdbc.lock, flags);
			break;
		}

		raw_spin_unlock_irqrestore(&xdbc.lock, flags);
		schedule_timeout_interruptible(1);
	}

	xdbc_unregister_console();
	writel(0, &xdbc.xdbc_reg->control);
	xdbc_trace("dbc scrub function exits\n");

	return 0;
}

static int __init xdbc_init(void)
{
	unsigned long flags;
	void __iomem *base;
	int ret = 0;
	u32 offset;

	if (!(xdbc.flags & XDBC_FLAGS_INITIALIZED))
		return 0;

	/*
	 * It's time to shut down the DbC, so that the debug
	 * port can be reused by the host controller:
	 */
	if (early_xdbc_console.index == -1 ||
	    (early_xdbc_console.flags & CON_BOOT)) {
		xdbc_trace("hardware not used anymore\n");
		goto free_and_quit;
	}

	base = ioremap(xdbc.xhci_start, xdbc.xhci_length);
	if (!base) {
		xdbc_trace("failed to remap the io address\n");
		ret = -ENOMEM;
		goto free_and_quit;
	}

	raw_spin_lock_irqsave(&xdbc.lock, flags);
	early_iounmap(xdbc.xhci_base, xdbc.xhci_length);
	xdbc.xhci_base = base;
	offset = xhci_find_next_ext_cap(xdbc.xhci_base, 0, XHCI_EXT_CAPS_DEBUG);
	xdbc.xdbc_reg = (struct xdbc_regs __iomem *)(xdbc.xhci_base + offset);
	raw_spin_unlock_irqrestore(&xdbc.lock, flags);

	kthread_run(xdbc_scrub_function, NULL, "%s", "xdbc");

	return 0;

free_and_quit:
	xdbc_free_ring(&xdbc.evt_ring);
	xdbc_free_ring(&xdbc.out_ring);
	xdbc_free_ring(&xdbc.in_ring);
	memblock_phys_free(xdbc.table_dma, PAGE_SIZE);
	memblock_phys_free(xdbc.out_dma, PAGE_SIZE);
	writel(0, &xdbc.xdbc_reg->control);
	early_iounmap(xdbc.xhci_base, xdbc.xhci_length);

	return ret;
}
subsys_initcall(xdbc_init);
