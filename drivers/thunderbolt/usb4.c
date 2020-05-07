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

typedef int (*read_block_fn)(struct tb_switch *, unsigned int, void *, size_t);
typedef int (*write_block_fn)(struct tb_switch *, const void *, size_t);

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

static int usb4_switch_do_read_data(struct tb_switch *sw, u16 address,
	void *buf, size_t size, read_block_fn read_block)
{
	unsigned int retries = USB4_DATA_RETRIES;
	unsigned int offset;

	offset = address & 3;
	address = address & ~3;

	do {
		size_t nbytes = min_t(size_t, size, USB4_DATA_DWORDS * 4);
		unsigned int dwaddress, dwords;
		u8 data[USB4_DATA_DWORDS * 4];
		int ret;

		dwaddress = address / 4;
		dwords = ALIGN(nbytes, 4) / 4;

		ret = read_block(sw, dwaddress, data, dwords);
		if (ret) {
			if (ret == -ETIMEDOUT) {
				if (retries--)
					continue;
				ret = -EIO;
			}
			return ret;
		}

		memcpy(buf, data + offset, nbytes);

		size -= nbytes;
		address += nbytes;
		buf += nbytes;
	} while (size > 0);

	return 0;
}

static int usb4_switch_do_write_data(struct tb_switch *sw, u16 address,
	const void *buf, size_t size, write_block_fn write_next_block)
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

		ret = write_next_block(sw, data, nbytes / 4);
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
	if (val & ROUTER_CS_26_ONS)
		return -EOPNOTSUPP;

	*status = (val & ROUTER_CS_26_STATUS_MASK) >> ROUTER_CS_26_STATUS_SHIFT;
	return 0;
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
	struct tb_switch *parent;
	bool tbt3, xhci;
	u32 val = 0;
	int ret;

	if (!tb_route(sw))
		return 0;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_6, 1);
	if (ret)
		return ret;

	xhci = val & ROUTER_CS_6_HCI;
	tbt3 = !(val & ROUTER_CS_6_TNS);

	tb_sw_dbg(sw, "TBT3 support: %s, xHCI: %s\n",
		  tbt3 ? "yes" : "no", xhci ? "yes" : "no");

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
	if (ret)
		return ret;

	parent = tb_switch_parent(sw);

	if (tb_switch_find_port(parent, TB_TYPE_USB3_DOWN)) {
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

static int usb4_switch_drom_read_block(struct tb_switch *sw,
				       unsigned int dwaddress, void *buf,
				       size_t dwords)
{
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
	return usb4_switch_do_read_data(sw, address, buf, size,
					usb4_switch_drom_read_block);
}

static int usb4_set_port_configured(struct tb_port *port, bool configured)
{
	int ret;
	u32 val;

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
 * usb4_switch_configure_link() - Set upstream USB4 link configured
 * @sw: USB4 router
 *
 * Sets the upstream USB4 link to be configured for power management
 * purposes.
 */
int usb4_switch_configure_link(struct tb_switch *sw)
{
	struct tb_port *up;

	if (!tb_route(sw))
		return 0;

	up = tb_upstream_port(sw);
	return usb4_set_port_configured(up, true);
}

/**
 * usb4_switch_unconfigure_link() - Un-set upstream USB4 link configuration
 * @sw: USB4 router
 *
 * Reverse of usb4_switch_configure_link().
 */
void usb4_switch_unconfigure_link(struct tb_switch *sw)
{
	struct tb_port *up;

	if (sw->is_unplugged || !tb_route(sw))
		return;

	up = tb_upstream_port(sw);
	usb4_set_port_configured(up, false);
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
 * usb4_switch_set_sleep() - Prepare the router to enter sleep
 * @sw: USB4 router
 *
 * Enables wakes and sets sleep bit for the router. Returns when the
 * router sleep ready bit has been asserted.
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

static int usb4_switch_nvm_read_block(struct tb_switch *sw,
	unsigned int dwaddress, void *buf, size_t dwords)
{
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
	return usb4_switch_do_read_data(sw, address, buf, size,
					usb4_switch_nvm_read_block);
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

static int usb4_switch_nvm_write_next_block(struct tb_switch *sw,
					    const void *buf, size_t dwords)
{
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

	return usb4_switch_do_write_data(sw, address, buf, size,
					 usb4_switch_nvm_write_next_block);
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

		if (pcie_idx == usb4_idx && !tb_pci_port_is_enabled(p))
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

		if (usb_idx == usb4_idx && !tb_usb3_port_is_enabled(p))
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
