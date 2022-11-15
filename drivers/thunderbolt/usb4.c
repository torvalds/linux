// SPDX-License-Identifier: GPL-2.0
/*
 * USB4 specific functionality
 *
 * Copyright (C) 2019, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Rajmohan Mani <rajmohan.mani@intel.com>
 */

#include <linux/delay.h>
#include <linux/ktime.h>

#include "sb_regs.h"
#include "tb.h"

#define USB4_DATA_DWORDS		16
#define USB4_DATA_RETRIES		3

enum usb4_switch_op {
	USB4_SWITCH_OP_QUERY_DP_RESOURCE = 0x10,
	USB4_SWITCH_OP_ALLOC_DP_RESOURCE = 0x11,
	USB4_SWITCH_OP_DEALLOC_DP_RESOURCE = 0x12,
	USB4_SWITCH_OP_NVM_WRITE = 0x20,
	USB4_SWITCH_OP_NVM_AUTH = 0x21,
	USB4_SWITCH_OP_NVM_READ = 0x22,
	USB4_SWITCH_OP_NVM_SET_OFFSET = 0x23,
	USB4_SWITCH_OP_DROM_READ = 0x24,
	USB4_SWITCH_OP_NVM_SECTOR_SIZE = 0x25,
};

enum usb4_sb_target {
	USB4_SB_TARGET_ROUTER,
	USB4_SB_TARGET_PARTNER,
	USB4_SB_TARGET_RETIMER,
};

#define USB4_NVM_READ_OFFSET_MASK	GENMASK(23, 2)
#define USB4_NVM_READ_OFFSET_SHIFT	2
#define USB4_NVM_READ_LENGTH_MASK	GENMASK(27, 24)
#define USB4_NVM_READ_LENGTH_SHIFT	24

#define USB4_NVM_SET_OFFSET_MASK	USB4_NVM_READ_OFFSET_MASK
#define USB4_NVM_SET_OFFSET_SHIFT	USB4_NVM_READ_OFFSET_SHIFT

#define USB4_DROM_ADDRESS_MASK		GENMASK(14, 2)
#define USB4_DROM_ADDRESS_SHIFT		2
#define USB4_DROM_SIZE_MASK		GENMASK(19, 15)
#define USB4_DROM_SIZE_SHIFT		15

#define USB4_NVM_SECTOR_SIZE_MASK	GENMASK(23, 0)

typedef int (*read_block_fn)(void *, unsigned int, void *, size_t);
typedef int (*write_block_fn)(void *, const void *, size_t);

static int usb4_switch_wait_for_bit(struct tb_switch *sw, u32 offset, u32 bit,
				    u32 value, int timeout_msec)
{
	ktime_t timeout = ktime_add_ms(ktime_get(), timeout_msec);

	do {
		u32 val;
		int ret;

		ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, offset, 1);
		if (ret)
			return ret;

		if ((val & bit) == value)
			return 0;

		usleep_range(50, 100);
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

static int usb4_switch_op_read_data(struct tb_switch *sw, void *data,
				    size_t dwords)
{
	if (dwords > USB4_DATA_DWORDS)
		return -EINVAL;

	return tb_sw_read(sw, data, TB_CFG_SWITCH, ROUTER_CS_9, dwords);
}

static int usb4_switch_op_write_data(struct tb_switch *sw, const void *data,
				     size_t dwords)
{
	if (dwords > USB4_DATA_DWORDS)
		return -EINVAL;

	return tb_sw_write(sw, data, TB_CFG_SWITCH, ROUTER_CS_9, dwords);
}

static int usb4_switch_op_read_metadata(struct tb_switch *sw, u32 *metadata)
{
	return tb_sw_read(sw, metadata, TB_CFG_SWITCH, ROUTER_CS_25, 1);
}

static int usb4_switch_op_write_metadata(struct tb_switch *sw, u32 metadata)
{
	return tb_sw_write(sw, &metadata, TB_CFG_SWITCH, ROUTER_CS_25, 1);
}

static int usb4_do_read_data(u16 address, void *buf, size_t size,
			     read_block_fn read_block, void *read_block_data)
{
	unsigned int retries = USB4_DATA_RETRIES;
	unsigned int offset;

	do {
		unsigned int dwaddress, dwords;
		u8 data[USB4_DATA_DWORDS * 4];
		size_t nbytes;
		int ret;

		offset = address & 3;
		nbytes = min_t(size_t, size + offset, USB4_DATA_DWORDS * 4);

		dwaddress = address / 4;
		dwords = ALIGN(nbytes, 4) / 4;

		ret = read_block(read_block_data, dwaddress, data, dwords);
		if (ret) {
			if (ret != -ENODEV && retries--)
				continue;
			return ret;
		}

		nbytes -= offset;
		memcpy(buf, data + offset, nbytes);

		size -= nbytes;
		address += nbytes;
		buf += nbytes;
	} while (size > 0);

	return 0;
}

static int usb4_do_write_data(unsigned int address, const void *buf, size_t size,
	write_block_fn write_next_block, void *write_block_data)
{
	unsigned int retries = USB4_DATA_RETRIES;
	unsigned int offset;

	offset = address & 3;
	address = address & ~3;

	do {
		u32 nbytes = min_t(u32, size, USB4_DATA_DWORDS * 4);
		u8 data[USB4_DATA_DWORDS * 4];
		int ret;

		memcpy(data + offset, buf, nbytes);

		ret = write_next_block(write_block_data, data, nbytes / 4);
		if (ret) {
			if (ret == -ETIMEDOUT) {
				if (retries--)
					continue;
				ret = -EIO;
			}
			return ret;
		}

		size -= nbytes;
		address += nbytes;
		buf += nbytes;
	} while (size > 0);

	return 0;
}

static int usb4_switch_op(struct tb_switch *sw, u16 opcode, u8 *status)
{
	u32 val;
	int ret;

	val = opcode | ROUTER_CS_26_OV;
	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH, ROUTER_CS_26, 1);
	if (ret)
		return ret;

	ret = usb4_switch_wait_for_bit(sw, ROUTER_CS_26, ROUTER_CS_26_OV, 0, 500);
	if (ret)
		return ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_26, 1);
	if (ret)
		return ret;

	if (val & ROUTER_CS_26_ONS)
		return -EOPNOTSUPP;

	*status = (val & ROUTER_CS_26_STATUS_MASK) >> ROUTER_CS_26_STATUS_SHIFT;
	return 0;
}

static void usb4_switch_check_wakes(struct tb_switch *sw)
{
	struct tb_port *port;
	bool wakeup = false;
	u32 val;

	if (!device_may_wakeup(&sw->dev))
		return;

	if (tb_route(sw)) {
		if (tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_6, 1))
			return;

		tb_sw_dbg(sw, "PCIe wake: %s, USB3 wake: %s\n",
			  (val & ROUTER_CS_6_WOPS) ? "yes" : "no",
			  (val & ROUTER_CS_6_WOUS) ? "yes" : "no");

		wakeup = val & (ROUTER_CS_6_WOPS | ROUTER_CS_6_WOUS);
	}

	/* Check for any connected downstream ports for USB4 wake */
	tb_switch_for_each_port(sw, port) {
		if (!tb_port_has_remote(port))
			continue;

		if (tb_port_read(port, &val, TB_CFG_PORT,
				 port->cap_usb4 + PORT_CS_18, 1))
			break;

		tb_port_dbg(port, "USB4 wake: %s\n",
			    (val & PORT_CS_18_WOU4S) ? "yes" : "no");

		if (val & PORT_CS_18_WOU4S)
			wakeup = true;
	}

	if (wakeup)
		pm_wakeup_event(&sw->dev, 0);
}

static bool link_is_usb4(struct tb_port *port)
{
	u32 val;

	if (!port->cap_usb4)
		return false;

	if (tb_port_read(port, &val, TB_CFG_PORT,
			 port->cap_usb4 + PORT_CS_18, 1))
		return false;

	return !(val & PORT_CS_18_TCM);
}

/**
 * usb4_switch_setup() - Additional setup for USB4 device
 * @sw: USB4 router to setup
 *
 * USB4 routers need additional settings in order to enable all the
 * tunneling. This function enables USB and PCIe tunneling if it can be
 * enabled (e.g the parent switch also supports them). If USB tunneling
 * is not available for some reason (like that there is Thunderbolt 3
 * switch upstream) then the internal xHCI controller is enabled
 * instead.
 */
int usb4_switch_setup(struct tb_switch *sw)
{
	struct tb_port *downstream_port;
	struct tb_switch *parent;
	bool tbt3, xhci;
	u32 val = 0;
	int ret;

	usb4_switch_check_wakes(sw);

	if (!tb_route(sw))
		return 0;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_6, 1);
	if (ret)
		return ret;

	parent = tb_switch_parent(sw);
	downstream_port = tb_port_at(tb_route(sw), parent);
	sw->link_usb4 = link_is_usb4(downstream_port);
	tb_sw_dbg(sw, "link: %s\n", sw->link_usb4 ? "USB4" : "TBT3");

	xhci = val & ROUTER_CS_6_HCI;
	tbt3 = !(val & ROUTER_CS_6_TNS);

	tb_sw_dbg(sw, "TBT3 support: %s, xHCI: %s\n",
		  tbt3 ? "yes" : "no", xhci ? "yes" : "no");

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
	if (ret)
		return ret;

	if (sw->link_usb4 && tb_switch_find_port(parent, TB_TYPE_USB3_DOWN)) {
		val |= ROUTER_CS_5_UTO;
		xhci = false;
	}

	/* Only enable PCIe tunneling if the parent router supports it */
	if (tb_switch_find_port(parent, TB_TYPE_PCIE_DOWN)) {
		val |= ROUTER_CS_5_PTO;
		/*
		 * xHCI can be enabled if PCIe tunneling is supported
		 * and the parent does not have any USB3 dowstream
		 * adapters (so we cannot do USB 3.x tunneling).
		 */
		if (xhci)
			val |= ROUTER_CS_5_HCO;
	}

	/* TBT3 supported by the CM */
	val |= ROUTER_CS_5_C3S;
	/* Tunneling configuration is ready now */
	val |= ROUTER_CS_5_CV;

	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
	if (ret)
		return ret;

	return usb4_switch_wait_for_bit(sw, ROUTER_CS_6, ROUTER_CS_6_CR,
					ROUTER_CS_6_CR, 50);
}

/**
 * usb4_switch_read_uid() - Read UID from USB4 router
 * @sw: USB4 router
 * @uid: UID is stored here
 *
 * Reads 64-bit UID from USB4 router config space.
 */
int usb4_switch_read_uid(struct tb_switch *sw, u64 *uid)
{
	return tb_sw_read(sw, uid, TB_CFG_SWITCH, ROUTER_CS_7, 2);
}

static int usb4_switch_drom_read_block(void *data,
				       unsigned int dwaddress, void *buf,
				       size_t dwords)
{
	struct tb_switch *sw = data;
	u8 status = 0;
	u32 metadata;
	int ret;

	metadata = (dwords << USB4_DROM_SIZE_SHIFT) & USB4_DROM_SIZE_MASK;
	metadata |= (dwaddress << USB4_DROM_ADDRESS_SHIFT) &
		USB4_DROM_ADDRESS_MASK;

	ret = usb4_switch_op_write_metadata(sw, metadata);
	if (ret)
		return ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_DROM_READ, &status);
	if (ret)
		return ret;

	if (status)
		return -EIO;

	return usb4_switch_op_read_data(sw, buf, dwords);
}

/**
 * usb4_switch_drom_read() - Read arbitrary bytes from USB4 router DROM
 * @sw: USB4 router
 * @address: Byte address inside DROM to start reading
 * @buf: Buffer where the DROM content is stored
 * @size: Number of bytes to read from DROM
 *
 * Uses USB4 router operations to read router DROM. For devices this
 * should always work but for hosts it may return %-EOPNOTSUPP in which
 * case the host router does not have DROM.
 */
int usb4_switch_drom_read(struct tb_switch *sw, unsigned int address, void *buf,
			  size_t size)
{
	return usb4_do_read_data(address, buf, size,
				 usb4_switch_drom_read_block, sw);
}

/**
 * usb4_switch_lane_bonding_possible() - Are conditions met for lane bonding
 * @sw: USB4 router
 *
 * Checks whether conditions are met so that lane bonding can be
 * established with the upstream router. Call only for device routers.
 */
bool usb4_switch_lane_bonding_possible(struct tb_switch *sw)
{
	struct tb_port *up;
	int ret;
	u32 val;

	up = tb_upstream_port(sw);
	ret = tb_port_read(up, &val, TB_CFG_PORT, up->cap_usb4 + PORT_CS_18, 1);
	if (ret)
		return false;

	return !!(val & PORT_CS_18_BE);
}

/**
 * usb4_switch_set_wake() - Enabled/disable wake
 * @sw: USB4 router
 * @flags: Wakeup flags (%0 to disable)
 *
 * Enables/disables router to wake up from sleep.
 */
int usb4_switch_set_wake(struct tb_switch *sw, unsigned int flags)
{
	struct tb_port *port;
	u64 route = tb_route(sw);
	u32 val;
	int ret;

	/*
	 * Enable wakes coming from all USB4 downstream ports (from
	 * child routers). For device routers do this also for the
	 * upstream USB4 port.
	 */
	tb_switch_for_each_port(sw, port) {
		if (!tb_port_is_null(port))
			continue;
		if (!route && tb_is_upstream_port(port))
			continue;
		if (!port->cap_usb4)
			continue;

		ret = tb_port_read(port, &val, TB_CFG_PORT,
				   port->cap_usb4 + PORT_CS_19, 1);
		if (ret)
			return ret;

		val &= ~(PORT_CS_19_WOC | PORT_CS_19_WOD | PORT_CS_19_WOU4);

		if (flags & TB_WAKE_ON_CONNECT)
			val |= PORT_CS_19_WOC;
		if (flags & TB_WAKE_ON_DISCONNECT)
			val |= PORT_CS_19_WOD;
		if (flags & TB_WAKE_ON_USB4)
			val |= PORT_CS_19_WOU4;

		ret = tb_port_write(port, &val, TB_CFG_PORT,
				    port->cap_usb4 + PORT_CS_19, 1);
		if (ret)
			return ret;
	}

	/*
	 * Enable wakes from PCIe and USB 3.x on this router. Only
	 * needed for device routers.
	 */
	if (route) {
		ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
		if (ret)
			return ret;

		val &= ~(ROUTER_CS_5_WOP | ROUTER_CS_5_WOU);
		if (flags & TB_WAKE_ON_USB3)
			val |= ROUTER_CS_5_WOU;
		if (flags & TB_WAKE_ON_PCIE)
			val |= ROUTER_CS_5_WOP;

		ret = tb_sw_write(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 * usb4_switch_set_sleep() - Prepare the router to enter sleep
 * @sw: USB4 router
 *
 * Sets sleep bit for the router. Returns when the router sleep ready
 * bit has been asserted.
 */
int usb4_switch_set_sleep(struct tb_switch *sw)
{
	int ret;
	u32 val;

	/* Set sleep bit and wait for sleep ready to be asserted */
	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
	if (ret)
		return ret;

	val |= ROUTER_CS_5_SLP;

	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
	if (ret)
		return ret;

	return usb4_switch_wait_for_bit(sw, ROUTER_CS_6, ROUTER_CS_6_SLPR,
					ROUTER_CS_6_SLPR, 500);
}

/**
 * usb4_switch_nvm_sector_size() - Return router NVM sector size
 * @sw: USB4 router
 *
 * If the router supports NVM operations this function returns the NVM
 * sector size in bytes. If NVM operations are not supported returns
 * %-EOPNOTSUPP.
 */
int usb4_switch_nvm_sector_size(struct tb_switch *sw)
{
	u32 metadata;
	u8 status;
	int ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_NVM_SECTOR_SIZE, &status);
	if (ret)
		return ret;

	if (status)
		return status == 0x2 ? -EOPNOTSUPP : -EIO;

	ret = usb4_switch_op_read_metadata(sw, &metadata);
	if (ret)
		return ret;

	return metadata & USB4_NVM_SECTOR_SIZE_MASK;
}

static int usb4_switch_nvm_read_block(void *data,
	unsigned int dwaddress, void *buf, size_t dwords)
{
	struct tb_switch *sw = data;
	u8 status = 0;
	u32 metadata;
	int ret;

	metadata = (dwords << USB4_NVM_READ_LENGTH_SHIFT) &
		   USB4_NVM_READ_LENGTH_MASK;
	metadata |= (dwaddress << USB4_NVM_READ_OFFSET_SHIFT) &
		   USB4_NVM_READ_OFFSET_MASK;

	ret = usb4_switch_op_write_metadata(sw, metadata);
	if (ret)
		return ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_NVM_READ, &status);
	if (ret)
		return ret;

	if (status)
		return -EIO;

	return usb4_switch_op_read_data(sw, buf, dwords);
}

/**
 * usb4_switch_nvm_read() - Read arbitrary bytes from router NVM
 * @sw: USB4 router
 * @address: Starting address in bytes
 * @buf: Read data is placed here
 * @size: How many bytes to read
 *
 * Reads NVM contents of the router. If NVM is not supported returns
 * %-EOPNOTSUPP.
 */
int usb4_switch_nvm_read(struct tb_switch *sw, unsigned int address, void *buf,
			 size_t size)
{
	return usb4_do_read_data(address, buf, size,
				 usb4_switch_nvm_read_block, sw);
}

static int usb4_switch_nvm_set_offset(struct tb_switch *sw,
				      unsigned int address)
{
	u32 metadata, dwaddress;
	u8 status = 0;
	int ret;

	dwaddress = address / 4;
	metadata = (dwaddress << USB4_NVM_SET_OFFSET_SHIFT) &
		   USB4_NVM_SET_OFFSET_MASK;

	ret = usb4_switch_op_write_metadata(sw, metadata);
	if (ret)
		return ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_NVM_SET_OFFSET, &status);
	if (ret)
		return ret;

	return status ? -EIO : 0;
}

static int usb4_switch_nvm_write_next_block(void *data, const void *buf,
					    size_t dwords)
{
	struct tb_switch *sw = data;
	u8 status;
	int ret;

	ret = usb4_switch_op_write_data(sw, buf, dwords);
	if (ret)
		return ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_NVM_WRITE, &status);
	if (ret)
		return ret;

	return status ? -EIO : 0;
}

/**
 * usb4_switch_nvm_write() - Write to the router NVM
 * @sw: USB4 router
 * @address: Start address where to write in bytes
 * @buf: Pointer to the data to write
 * @size: Size of @buf in bytes
 *
 * Writes @buf to the router NVM using USB4 router operations. If NVM
 * write is not supported returns %-EOPNOTSUPP.
 */
int usb4_switch_nvm_write(struct tb_switch *sw, unsigned int address,
			  const void *buf, size_t size)
{
	int ret;

	ret = usb4_switch_nvm_set_offset(sw, address);
	if (ret)
		return ret;

	return usb4_do_write_data(address, buf, size,
				  usb4_switch_nvm_write_next_block, sw);
}

/**
 * usb4_switch_nvm_authenticate() - Authenticate new NVM
 * @sw: USB4 router
 *
 * After the new NVM has been written via usb4_switch_nvm_write(), this
 * function triggers NVM authentication process. If the authentication
 * is successful the router is power cycled and the new NVM starts
 * running. In case of failure returns negative errno.
 */
int usb4_switch_nvm_authenticate(struct tb_switch *sw)
{
	u8 status = 0;
	int ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_NVM_AUTH, &status);
	if (ret)
		return ret;

	switch (status) {
	case 0x0:
		tb_sw_dbg(sw, "NVM authentication successful\n");
		return 0;
	case 0x1:
		return -EINVAL;
	case 0x2:
		return -EAGAIN;
	case 0x3:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
}

/**
 * usb4_switch_query_dp_resource() - Query availability of DP IN resource
 * @sw: USB4 router
 * @in: DP IN adapter
 *
 * For DP tunneling this function can be used to query availability of
 * DP IN resource. Returns true if the resource is available for DP
 * tunneling, false otherwise.
 */
bool usb4_switch_query_dp_resource(struct tb_switch *sw, struct tb_port *in)
{
	u8 status;
	int ret;

	ret = usb4_switch_op_write_metadata(sw, in->port);
	if (ret)
		return false;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_QUERY_DP_RESOURCE, &status);
	/*
	 * If DP resource allocation is not supported assume it is
	 * always available.
	 */
	if (ret == -EOPNOTSUPP)
		return true;
	else if (ret)
		return false;

	return !status;
}

/**
 * usb4_switch_alloc_dp_resource() - Allocate DP IN resource
 * @sw: USB4 router
 * @in: DP IN adapter
 *
 * Allocates DP IN resource for DP tunneling using USB4 router
 * operations. If the resource was allocated returns %0. Otherwise
 * returns negative errno, in particular %-EBUSY if the resource is
 * already allocated.
 */
int usb4_switch_alloc_dp_resource(struct tb_switch *sw, struct tb_port *in)
{
	u8 status;
	int ret;

	ret = usb4_switch_op_write_metadata(sw, in->port);
	if (ret)
		return ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_ALLOC_DP_RESOURCE, &status);
	if (ret == -EOPNOTSUPP)
		return 0;
	else if (ret)
		return ret;

	return status ? -EBUSY : 0;
}

/**
 * usb4_switch_dealloc_dp_resource() - Releases allocated DP IN resource
 * @sw: USB4 router
 * @in: DP IN adapter
 *
 * Releases the previously allocated DP IN resource.
 */
int usb4_switch_dealloc_dp_resource(struct tb_switch *sw, struct tb_port *in)
{
	u8 status;
	int ret;

	ret = usb4_switch_op_write_metadata(sw, in->port);
	if (ret)
		return ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_DEALLOC_DP_RESOURCE, &status);
	if (ret == -EOPNOTSUPP)
		return 0;
	else if (ret)
		return ret;

	return status ? -EIO : 0;
}

static int usb4_port_idx(const struct tb_switch *sw, const struct tb_port *port)
{
	struct tb_port *p;
	int usb4_idx = 0;

	/* Assume port is primary */
	tb_switch_for_each_port(sw, p) {
		if (!tb_port_is_null(p))
			continue;
		if (tb_is_upstream_port(p))
			continue;
		if (!p->link_nr) {
			if (p == port)
				break;
			usb4_idx++;
		}
	}

	return usb4_idx;
}

/**
 * usb4_switch_map_pcie_down() - Map USB4 port to a PCIe downstream adapter
 * @sw: USB4 router
 * @port: USB4 port
 *
 * USB4 routers have direct mapping between USB4 ports and PCIe
 * downstream adapters where the PCIe topology is extended. This
 * function returns the corresponding downstream PCIe adapter or %NULL
 * if no such mapping was possible.
 */
struct tb_port *usb4_switch_map_pcie_down(struct tb_switch *sw,
					  const struct tb_port *port)
{
	int usb4_idx = usb4_port_idx(sw, port);
	struct tb_port *p;
	int pcie_idx = 0;

	/* Find PCIe down port matching usb4_port */
	tb_switch_for_each_port(sw, p) {
		if (!tb_port_is_pcie_down(p))
			continue;

		if (pcie_idx == usb4_idx)
			return p;

		pcie_idx++;
	}

	return NULL;
}

/**
 * usb4_switch_map_usb3_down() - Map USB4 port to a USB3 downstream adapter
 * @sw: USB4 router
 * @port: USB4 port
 *
 * USB4 routers have direct mapping between USB4 ports and USB 3.x
 * downstream adapters where the USB 3.x topology is extended. This
 * function returns the corresponding downstream USB 3.x adapter or
 * %NULL if no such mapping was possible.
 */
struct tb_port *usb4_switch_map_usb3_down(struct tb_switch *sw,
					  const struct tb_port *port)
{
	int usb4_idx = usb4_port_idx(sw, port);
	struct tb_port *p;
	int usb_idx = 0;

	/* Find USB3 down port matching usb4_port */
	tb_switch_for_each_port(sw, p) {
		if (!tb_port_is_usb3_down(p))
			continue;

		if (usb_idx == usb4_idx)
			return p;

		usb_idx++;
	}

	return NULL;
}

/**
 * usb4_port_unlock() - Unlock USB4 downstream port
 * @port: USB4 port to unlock
 *
 * Unlocks USB4 downstream port so that the connection manager can
 * access the router below this port.
 */
int usb4_port_unlock(struct tb_port *port)
{
	int ret;
	u32 val;

	ret = tb_port_read(port, &val, TB_CFG_PORT, ADP_CS_4, 1);
	if (ret)
		return ret;

	val &= ~ADP_CS_4_LCK;
	return tb_port_write(port, &val, TB_CFG_PORT, ADP_CS_4, 1);
}

/**
 * usb4_port_hotplug_enable() - Enables hotplug for a port
 * @port: USB4 port to operate on
 *
 * Enables hot plug events on a given port. This is only intended
 * to be used on lane, DP-IN, and DP-OUT adapters.
 */
int usb4_port_hotplug_enable(struct tb_port *port)
{
	int ret;
	u32 val;

	ret = tb_port_read(port, &val, TB_CFG_PORT, ADP_CS_5, 1);
	if (ret)
		return ret;

	val &= ~ADP_CS_5_DHP;
	return tb_port_write(port, &val, TB_CFG_PORT, ADP_CS_5, 1);
}

static int usb4_port_set_configured(struct tb_port *port, bool configured)
{
	int ret;
	u32 val;

	if (!port->cap_usb4)
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_usb4 + PORT_CS_19, 1);
	if (ret)
		return ret;

	if (configured)
		val |= PORT_CS_19_PC;
	else
		val &= ~PORT_CS_19_PC;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_usb4 + PORT_CS_19, 1);
}

/**
 * usb4_port_configure() - Set USB4 port configured
 * @port: USB4 router
 *
 * Sets the USB4 link to be configured for power management purposes.
 */
int usb4_port_configure(struct tb_port *port)
{
	return usb4_port_set_configured(port, true);
}

/**
 * usb4_port_unconfigure() - Set USB4 port unconfigured
 * @port: USB4 router
 *
 * Sets the USB4 link to be unconfigured for power management purposes.
 */
void usb4_port_unconfigure(struct tb_port *port)
{
	usb4_port_set_configured(port, false);
}

static int usb4_set_xdomain_configured(struct tb_port *port, bool configured)
{
	int ret;
	u32 val;

	if (!port->cap_usb4)
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_usb4 + PORT_CS_19, 1);
	if (ret)
		return ret;

	if (configured)
		val |= PORT_CS_19_PID;
	else
		val &= ~PORT_CS_19_PID;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_usb4 + PORT_CS_19, 1);
}

/**
 * usb4_port_configure_xdomain() - Configure port for XDomain
 * @port: USB4 port connected to another host
 *
 * Marks the USB4 port as being connected to another host. Returns %0 in
 * success and negative errno in failure.
 */
int usb4_port_configure_xdomain(struct tb_port *port)
{
	return usb4_set_xdomain_configured(port, true);
}

/**
 * usb4_port_unconfigure_xdomain() - Unconfigure port for XDomain
 * @port: USB4 port that was connected to another host
 *
 * Clears USB4 port from being marked as XDomain.
 */
void usb4_port_unconfigure_xdomain(struct tb_port *port)
{
	usb4_set_xdomain_configured(port, false);
}

static int usb4_port_wait_for_bit(struct tb_port *port, u32 offset, u32 bit,
				  u32 value, int timeout_msec)
{
	ktime_t timeout = ktime_add_ms(ktime_get(), timeout_msec);

	do {
		u32 val;
		int ret;

		ret = tb_port_read(port, &val, TB_CFG_PORT, offset, 1);
		if (ret)
			return ret;

		if ((val & bit) == value)
			return 0;

		usleep_range(50, 100);
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

static int usb4_port_read_data(struct tb_port *port, void *data, size_t dwords)
{
	if (dwords > USB4_DATA_DWORDS)
		return -EINVAL;

	return tb_port_read(port, data, TB_CFG_PORT, port->cap_usb4 + PORT_CS_2,
			    dwords);
}

static int usb4_port_write_data(struct tb_port *port, const void *data,
				size_t dwords)
{
	if (dwords > USB4_DATA_DWORDS)
		return -EINVAL;

	return tb_port_write(port, data, TB_CFG_PORT, port->cap_usb4 + PORT_CS_2,
			     dwords);
}

static int usb4_port_sb_read(struct tb_port *port, enum usb4_sb_target target,
			     u8 index, u8 reg, void *buf, u8 size)
{
	size_t dwords = DIV_ROUND_UP(size, 4);
	int ret;
	u32 val;

	if (!port->cap_usb4)
		return -EINVAL;

	val = reg;
	val |= size << PORT_CS_1_LENGTH_SHIFT;
	val |= (target << PORT_CS_1_TARGET_SHIFT) & PORT_CS_1_TARGET_MASK;
	if (target == USB4_SB_TARGET_RETIMER)
		val |= (index << PORT_CS_1_RETIMER_INDEX_SHIFT);
	val |= PORT_CS_1_PND;

	ret = tb_port_write(port, &val, TB_CFG_PORT,
			    port->cap_usb4 + PORT_CS_1, 1);
	if (ret)
		return ret;

	ret = usb4_port_wait_for_bit(port, port->cap_usb4 + PORT_CS_1,
				     PORT_CS_1_PND, 0, 500);
	if (ret)
		return ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			    port->cap_usb4 + PORT_CS_1, 1);
	if (ret)
		return ret;

	if (val & PORT_CS_1_NR)
		return -ENODEV;
	if (val & PORT_CS_1_RC)
		return -EIO;

	return buf ? usb4_port_read_data(port, buf, dwords) : 0;
}

static int usb4_port_sb_write(struct tb_port *port, enum usb4_sb_target target,
			      u8 index, u8 reg, const void *buf, u8 size)
{
	size_t dwords = DIV_ROUND_UP(size, 4);
	int ret;
	u32 val;

	if (!port->cap_usb4)
		return -EINVAL;

	if (buf) {
		ret = usb4_port_write_data(port, buf, dwords);
		if (ret)
			return ret;
	}

	val = reg;
	val |= size << PORT_CS_1_LENGTH_SHIFT;
	val |= PORT_CS_1_WNR_WRITE;
	val |= (target << PORT_CS_1_TARGET_SHIFT) & PORT_CS_1_TARGET_MASK;
	if (target == USB4_SB_TARGET_RETIMER)
		val |= (index << PORT_CS_1_RETIMER_INDEX_SHIFT);
	val |= PORT_CS_1_PND;

	ret = tb_port_write(port, &val, TB_CFG_PORT,
			    port->cap_usb4 + PORT_CS_1, 1);
	if (ret)
		return ret;

	ret = usb4_port_wait_for_bit(port, port->cap_usb4 + PORT_CS_1,
				     PORT_CS_1_PND, 0, 500);
	if (ret)
		return ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			    port->cap_usb4 + PORT_CS_1, 1);
	if (ret)
		return ret;

	if (val & PORT_CS_1_NR)
		return -ENODEV;
	if (val & PORT_CS_1_RC)
		return -EIO;

	return 0;
}

static int usb4_port_sb_op(struct tb_port *port, enum usb4_sb_target target,
			   u8 index, enum usb4_sb_opcode opcode, int timeout_msec)
{
	ktime_t timeout;
	u32 val;
	int ret;

	val = opcode;
	ret = usb4_port_sb_write(port, target, index, USB4_SB_OPCODE, &val,
				 sizeof(val));
	if (ret)
		return ret;

	timeout = ktime_add_ms(ktime_get(), timeout_msec);

	do {
		/* Check results */
		ret = usb4_port_sb_read(port, target, index, USB4_SB_OPCODE,
					&val, sizeof(val));
		if (ret)
			return ret;

		switch (val) {
		case 0:
			return 0;

		case USB4_SB_OPCODE_ERR:
			return -EAGAIN;

		case USB4_SB_OPCODE_ONS:
			return -EOPNOTSUPP;

		default:
			if (val != opcode)
				return -EIO;
			break;
		}
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

/**
 * usb4_port_enumerate_retimers() - Send RT broadcast transaction
 * @port: USB4 port
 *
 * This forces the USB4 port to send broadcast RT transaction which
 * makes the retimers on the link to assign index to themselves. Returns
 * %0 in case of success and negative errno if there was an error.
 */
int usb4_port_enumerate_retimers(struct tb_port *port)
{
	u32 val;

	val = USB4_SB_OPCODE_ENUMERATE_RETIMERS;
	return usb4_port_sb_write(port, USB4_SB_TARGET_ROUTER, 0,
				  USB4_SB_OPCODE, &val, sizeof(val));
}

static inline int usb4_port_retimer_op(struct tb_port *port, u8 index,
				       enum usb4_sb_opcode opcode,
				       int timeout_msec)
{
	return usb4_port_sb_op(port, USB4_SB_TARGET_RETIMER, index, opcode,
			       timeout_msec);
}

/**
 * usb4_port_retimer_read() - Read from retimer sideband registers
 * @port: USB4 port
 * @index: Retimer index
 * @reg: Sideband register to read
 * @buf: Data from @reg is stored here
 * @size: Number of bytes to read
 *
 * Function reads retimer sideband registers starting from @reg. The
 * retimer is connected to @port at @index. Returns %0 in case of
 * success, and read data is copied to @buf. If there is no retimer
 * present at given @index returns %-ENODEV. In any other failure
 * returns negative errno.
 */
int usb4_port_retimer_read(struct tb_port *port, u8 index, u8 reg, void *buf,
			   u8 size)
{
	return usb4_port_sb_read(port, USB4_SB_TARGET_RETIMER, index, reg, buf,
				 size);
}

/**
 * usb4_port_retimer_write() - Write to retimer sideband registers
 * @port: USB4 port
 * @index: Retimer index
 * @reg: Sideband register to write
 * @buf: Data that is written starting from @reg
 * @size: Number of bytes to write
 *
 * Writes retimer sideband registers starting from @reg. The retimer is
 * connected to @port at @index. Returns %0 in case of success. If there
 * is no retimer present at given @index returns %-ENODEV. In any other
 * failure returns negative errno.
 */
int usb4_port_retimer_write(struct tb_port *port, u8 index, u8 reg,
			    const void *buf, u8 size)
{
	return usb4_port_sb_write(port, USB4_SB_TARGET_RETIMER, index, reg, buf,
				  size);
}

/**
 * usb4_port_retimer_is_last() - Is the retimer last on-board retimer
 * @port: USB4 port
 * @index: Retimer index
 *
 * If the retimer at @index is last one (connected directly to the
 * Type-C port) this function returns %1. If it is not returns %0. If
 * the retimer is not present returns %-ENODEV. Otherwise returns
 * negative errno.
 */
int usb4_port_retimer_is_last(struct tb_port *port, u8 index)
{
	u32 metadata;
	int ret;

	ret = usb4_port_retimer_op(port, index, USB4_SB_OPCODE_QUERY_LAST_RETIMER,
				   500);
	if (ret)
		return ret;

	ret = usb4_port_retimer_read(port, index, USB4_SB_METADATA, &metadata,
				     sizeof(metadata));
	return ret ? ret : metadata & 1;
}

/**
 * usb4_port_retimer_nvm_sector_size() - Read retimer NVM sector size
 * @port: USB4 port
 * @index: Retimer index
 *
 * Reads NVM sector size (in bytes) of a retimer at @index. This
 * operation can be used to determine whether the retimer supports NVM
 * upgrade for example. Returns sector size in bytes or negative errno
 * in case of error. Specifically returns %-ENODEV if there is no
 * retimer at @index.
 */
int usb4_port_retimer_nvm_sector_size(struct tb_port *port, u8 index)
{
	u32 metadata;
	int ret;

	ret = usb4_port_retimer_op(port, index, USB4_SB_OPCODE_GET_NVM_SECTOR_SIZE,
				   500);
	if (ret)
		return ret;

	ret = usb4_port_retimer_read(port, index, USB4_SB_METADATA, &metadata,
				     sizeof(metadata));
	return ret ? ret : metadata & USB4_NVM_SECTOR_SIZE_MASK;
}

static int usb4_port_retimer_nvm_set_offset(struct tb_port *port, u8 index,
					    unsigned int address)
{
	u32 metadata, dwaddress;
	int ret;

	dwaddress = address / 4;
	metadata = (dwaddress << USB4_NVM_SET_OFFSET_SHIFT) &
		  USB4_NVM_SET_OFFSET_MASK;

	ret = usb4_port_retimer_write(port, index, USB4_SB_METADATA, &metadata,
				      sizeof(metadata));
	if (ret)
		return ret;

	return usb4_port_retimer_op(port, index, USB4_SB_OPCODE_NVM_SET_OFFSET,
				    500);
}

struct retimer_info {
	struct tb_port *port;
	u8 index;
};

static int usb4_port_retimer_nvm_write_next_block(void *data, const void *buf,
						  size_t dwords)

{
	const struct retimer_info *info = data;
	struct tb_port *port = info->port;
	u8 index = info->index;
	int ret;

	ret = usb4_port_retimer_write(port, index, USB4_SB_DATA,
				      buf, dwords * 4);
	if (ret)
		return ret;

	return usb4_port_retimer_op(port, index,
			USB4_SB_OPCODE_NVM_BLOCK_WRITE, 1000);
}

/**
 * usb4_port_retimer_nvm_write() - Write to retimer NVM
 * @port: USB4 port
 * @index: Retimer index
 * @address: Byte address where to start the write
 * @buf: Data to write
 * @size: Size in bytes how much to write
 *
 * Writes @size bytes from @buf to the retimer NVM. Used for NVM
 * upgrade. Returns %0 if the data was written successfully and negative
 * errno in case of failure. Specifically returns %-ENODEV if there is
 * no retimer at @index.
 */
int usb4_port_retimer_nvm_write(struct tb_port *port, u8 index, unsigned int address,
				const void *buf, size_t size)
{
	struct retimer_info info = { .port = port, .index = index };
	int ret;

	ret = usb4_port_retimer_nvm_set_offset(port, index, address);
	if (ret)
		return ret;

	return usb4_do_write_data(address, buf, size,
			usb4_port_retimer_nvm_write_next_block, &info);
}

/**
 * usb4_port_retimer_nvm_authenticate() - Start retimer NVM upgrade
 * @port: USB4 port
 * @index: Retimer index
 *
 * After the new NVM image has been written via usb4_port_retimer_nvm_write()
 * this function can be used to trigger the NVM upgrade process. If
 * successful the retimer restarts with the new NVM and may not have the
 * index set so one needs to call usb4_port_enumerate_retimers() to
 * force index to be assigned.
 */
int usb4_port_retimer_nvm_authenticate(struct tb_port *port, u8 index)
{
	u32 val;

	/*
	 * We need to use the raw operation here because once the
	 * authentication completes the retimer index is not set anymore
	 * so we do not get back the status now.
	 */
	val = USB4_SB_OPCODE_NVM_AUTH_WRITE;
	return usb4_port_sb_write(port, USB4_SB_TARGET_RETIMER, index,
				  USB4_SB_OPCODE, &val, sizeof(val));
}

/**
 * usb4_port_retimer_nvm_authenticate_status() - Read status of NVM upgrade
 * @port: USB4 port
 * @index: Retimer index
 * @status: Raw status code read from metadata
 *
 * This can be called after usb4_port_retimer_nvm_authenticate() and
 * usb4_port_enumerate_retimers() to fetch status of the NVM upgrade.
 *
 * Returns %0 if the authentication status was successfully read. The
 * completion metadata (the result) is then stored into @status. If
 * reading the status fails, returns negative errno.
 */
int usb4_port_retimer_nvm_authenticate_status(struct tb_port *port, u8 index,
					      u32 *status)
{
	u32 metadata, val;
	int ret;

	ret = usb4_port_retimer_read(port, index, USB4_SB_OPCODE, &val,
				     sizeof(val));
	if (ret)
		return ret;

	switch (val) {
	case 0:
		*status = 0;
		return 0;

	case USB4_SB_OPCODE_ERR:
		ret = usb4_port_retimer_read(port, index, USB4_SB_METADATA,
					     &metadata, sizeof(metadata));
		if (ret)
			return ret;

		*status = metadata & USB4_SB_METADATA_NVM_AUTH_WRITE_MASK;
		return 0;

	case USB4_SB_OPCODE_ONS:
		return -EOPNOTSUPP;

	default:
		return -EIO;
	}
}

static int usb4_port_retimer_nvm_read_block(void *data, unsigned int dwaddress,
					    void *buf, size_t dwords)
{
	const struct retimer_info *info = data;
	struct tb_port *port = info->port;
	u8 index = info->index;
	u32 metadata;
	int ret;

	metadata = dwaddress << USB4_NVM_READ_OFFSET_SHIFT;
	if (dwords < USB4_DATA_DWORDS)
		metadata |= dwords << USB4_NVM_READ_LENGTH_SHIFT;

	ret = usb4_port_retimer_write(port, index, USB4_SB_METADATA, &metadata,
				      sizeof(metadata));
	if (ret)
		return ret;

	ret = usb4_port_retimer_op(port, index, USB4_SB_OPCODE_NVM_READ, 500);
	if (ret)
		return ret;

	return usb4_port_retimer_read(port, index, USB4_SB_DATA, buf,
				      dwords * 4);
}

/**
 * usb4_port_retimer_nvm_read() - Read contents of retimer NVM
 * @port: USB4 port
 * @index: Retimer index
 * @address: NVM address (in bytes) to start reading
 * @buf: Data read from NVM is stored here
 * @size: Number of bytes to read
 *
 * Reads retimer NVM and copies the contents to @buf. Returns %0 if the
 * read was successful and negative errno in case of failure.
 * Specifically returns %-ENODEV if there is no retimer at @index.
 */
int usb4_port_retimer_nvm_read(struct tb_port *port, u8 index,
			       unsigned int address, void *buf, size_t size)
{
	struct retimer_info info = { .port = port, .index = index };

	return usb4_do_read_data(address, buf, size,
			usb4_port_retimer_nvm_read_block, &info);
}

/**
 * usb4_usb3_port_max_link_rate() - Maximum support USB3 link rate
 * @port: USB3 adapter port
 *
 * Return maximum supported link rate of a USB3 adapter in Mb/s.
 * Negative errno in case of error.
 */
int usb4_usb3_port_max_link_rate(struct tb_port *port)
{
	int ret, lr;
	u32 val;

	if (!tb_port_is_usb3_down(port) && !tb_port_is_usb3_up(port))
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_4, 1);
	if (ret)
		return ret;

	lr = (val & ADP_USB3_CS_4_MSLR_MASK) >> ADP_USB3_CS_4_MSLR_SHIFT;
	return lr == ADP_USB3_CS_4_MSLR_20G ? 20000 : 10000;
}

/**
 * usb4_usb3_port_actual_link_rate() - Established USB3 link rate
 * @port: USB3 adapter port
 *
 * Return actual established link rate of a USB3 adapter in Mb/s. If the
 * link is not up returns %0 and negative errno in case of failure.
 */
int usb4_usb3_port_actual_link_rate(struct tb_port *port)
{
	int ret, lr;
	u32 val;

	if (!tb_port_is_usb3_down(port) && !tb_port_is_usb3_up(port))
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_4, 1);
	if (ret)
		return ret;

	if (!(val & ADP_USB3_CS_4_ULV))
		return 0;

	lr = val & ADP_USB3_CS_4_ALR_MASK;
	return lr == ADP_USB3_CS_4_ALR_20G ? 20000 : 10000;
}

static int usb4_usb3_port_cm_request(struct tb_port *port, bool request)
{
	int ret;
	u32 val;

	if (!tb_port_is_usb3_down(port))
		return -EINVAL;
	if (tb_route(port->sw))
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_2, 1);
	if (ret)
		return ret;

	if (request)
		val |= ADP_USB3_CS_2_CMR;
	else
		val &= ~ADP_USB3_CS_2_CMR;

	ret = tb_port_write(port, &val, TB_CFG_PORT,
			    port->cap_adap + ADP_USB3_CS_2, 1);
	if (ret)
		return ret;

	/*
	 * We can use val here directly as the CMR bit is in the same place
	 * as HCA. Just mask out others.
	 */
	val &= ADP_USB3_CS_2_CMR;
	return usb4_port_wait_for_bit(port, port->cap_adap + ADP_USB3_CS_1,
				      ADP_USB3_CS_1_HCA, val, 1500);
}

static inline int usb4_usb3_port_set_cm_request(struct tb_port *port)
{
	return usb4_usb3_port_cm_request(port, true);
}

static inline int usb4_usb3_port_clear_cm_request(struct tb_port *port)
{
	return usb4_usb3_port_cm_request(port, false);
}

static unsigned int usb3_bw_to_mbps(u32 bw, u8 scale)
{
	unsigned long uframes;

	uframes = bw * 512UL << scale;
	return DIV_ROUND_CLOSEST(uframes * 8000, 1000 * 1000);
}

static u32 mbps_to_usb3_bw(unsigned int mbps, u8 scale)
{
	unsigned long uframes;

	/* 1 uframe is 1/8 ms (125 us) -> 1 / 8000 s */
	uframes = ((unsigned long)mbps * 1000 *  1000) / 8000;
	return DIV_ROUND_UP(uframes, 512UL << scale);
}

static int usb4_usb3_port_read_allocated_bandwidth(struct tb_port *port,
						   int *upstream_bw,
						   int *downstream_bw)
{
	u32 val, bw, scale;
	int ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_2, 1);
	if (ret)
		return ret;

	ret = tb_port_read(port, &scale, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_3, 1);
	if (ret)
		return ret;

	scale &= ADP_USB3_CS_3_SCALE_MASK;

	bw = val & ADP_USB3_CS_2_AUBW_MASK;
	*upstream_bw = usb3_bw_to_mbps(bw, scale);

	bw = (val & ADP_USB3_CS_2_ADBW_MASK) >> ADP_USB3_CS_2_ADBW_SHIFT;
	*downstream_bw = usb3_bw_to_mbps(bw, scale);

	return 0;
}

/**
 * usb4_usb3_port_allocated_bandwidth() - Bandwidth allocated for USB3
 * @port: USB3 adapter port
 * @upstream_bw: Allocated upstream bandwidth is stored here
 * @downstream_bw: Allocated downstream bandwidth is stored here
 *
 * Stores currently allocated USB3 bandwidth into @upstream_bw and
 * @downstream_bw in Mb/s. Returns %0 in case of success and negative
 * errno in failure.
 */
int usb4_usb3_port_allocated_bandwidth(struct tb_port *port, int *upstream_bw,
				       int *downstream_bw)
{
	int ret;

	ret = usb4_usb3_port_set_cm_request(port);
	if (ret)
		return ret;

	ret = usb4_usb3_port_read_allocated_bandwidth(port, upstream_bw,
						      downstream_bw);
	usb4_usb3_port_clear_cm_request(port);

	return ret;
}

static int usb4_usb3_port_read_consumed_bandwidth(struct tb_port *port,
						  int *upstream_bw,
						  int *downstream_bw)
{
	u32 val, bw, scale;
	int ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_1, 1);
	if (ret)
		return ret;

	ret = tb_port_read(port, &scale, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_3, 1);
	if (ret)
		return ret;

	scale &= ADP_USB3_CS_3_SCALE_MASK;

	bw = val & ADP_USB3_CS_1_CUBW_MASK;
	*upstream_bw = usb3_bw_to_mbps(bw, scale);

	bw = (val & ADP_USB3_CS_1_CDBW_MASK) >> ADP_USB3_CS_1_CDBW_SHIFT;
	*downstream_bw = usb3_bw_to_mbps(bw, scale);

	return 0;
}

static int usb4_usb3_port_write_allocated_bandwidth(struct tb_port *port,
						    int upstream_bw,
						    int downstream_bw)
{
	u32 val, ubw, dbw, scale;
	int ret;

	/* Read the used scale, hardware default is 0 */
	ret = tb_port_read(port, &scale, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_3, 1);
	if (ret)
		return ret;

	scale &= ADP_USB3_CS_3_SCALE_MASK;
	ubw = mbps_to_usb3_bw(upstream_bw, scale);
	dbw = mbps_to_usb3_bw(downstream_bw, scale);

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_USB3_CS_2, 1);
	if (ret)
		return ret;

	val &= ~(ADP_USB3_CS_2_AUBW_MASK | ADP_USB3_CS_2_ADBW_MASK);
	val |= dbw << ADP_USB3_CS_2_ADBW_SHIFT;
	val |= ubw;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_USB3_CS_2, 1);
}

/**
 * usb4_usb3_port_allocate_bandwidth() - Allocate bandwidth for USB3
 * @port: USB3 adapter port
 * @upstream_bw: New upstream bandwidth
 * @downstream_bw: New downstream bandwidth
 *
 * This can be used to set how much bandwidth is allocated for the USB3
 * tunneled isochronous traffic. @upstream_bw and @downstream_bw are the
 * new values programmed to the USB3 adapter allocation registers. If
 * the values are lower than what is currently consumed the allocation
 * is set to what is currently consumed instead (consumed bandwidth
 * cannot be taken away by CM). The actual new values are returned in
 * @upstream_bw and @downstream_bw.
 *
 * Returns %0 in case of success and negative errno if there was a
 * failure.
 */
int usb4_usb3_port_allocate_bandwidth(struct tb_port *port, int *upstream_bw,
				      int *downstream_bw)
{
	int ret, consumed_up, consumed_down, allocate_up, allocate_down;

	ret = usb4_usb3_port_set_cm_request(port);
	if (ret)
		return ret;

	ret = usb4_usb3_port_read_consumed_bandwidth(port, &consumed_up,
						     &consumed_down);
	if (ret)
		goto err_request;

	/* Don't allow it go lower than what is consumed */
	allocate_up = max(*upstream_bw, consumed_up);
	allocate_down = max(*downstream_bw, consumed_down);

	ret = usb4_usb3_port_write_allocated_bandwidth(port, allocate_up,
						       allocate_down);
	if (ret)
		goto err_request;

	*upstream_bw = allocate_up;
	*downstream_bw = allocate_down;

err_request:
	usb4_usb3_port_clear_cm_request(port);
	return ret;
}

/**
 * usb4_usb3_port_release_bandwidth() - Release allocated USB3 bandwidth
 * @port: USB3 adapter port
 * @upstream_bw: New allocated upstream bandwidth
 * @downstream_bw: New allocated downstream bandwidth
 *
 * Releases USB3 allocated bandwidth down to what is actually consumed.
 * The new bandwidth is returned in @upstream_bw and @downstream_bw.
 *
 * Returns 0% in success and negative errno in case of failure.
 */
int usb4_usb3_port_release_bandwidth(struct tb_port *port, int *upstream_bw,
				     int *downstream_bw)
{
	int ret, consumed_up, consumed_down;

	ret = usb4_usb3_port_set_cm_request(port);
	if (ret)
		return ret;

	ret = usb4_usb3_port_read_consumed_bandwidth(port, &consumed_up,
						     &consumed_down);
	if (ret)
		goto err_request;

	/*
	 * Always keep 1000 Mb/s to make sure xHCI has at least some
	 * bandwidth available for isochronous traffic.
	 */
	if (consumed_up < 1000)
		consumed_up = 1000;
	if (consumed_down < 1000)
		consumed_down = 1000;

	ret = usb4_usb3_port_write_allocated_bandwidth(port, consumed_up,
						       consumed_down);
	if (ret)
		goto err_request;

	*upstream_bw = consumed_up;
	*downstream_bw = consumed_down;

err_request:
	usb4_usb3_port_clear_cm_request(port);
	return ret;
}
