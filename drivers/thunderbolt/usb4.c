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
#include <linux/units.h>

#include "sb_regs.h"
#include "tb.h"

#define USB4_DATA_RETRIES		3
#define USB4_DATA_DWORDS		16

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

#define USB4_BA_LENGTH_MASK		GENMASK(7, 0)
#define USB4_BA_INDEX_MASK		GENMASK(15, 0)

enum usb4_ba_index {
	USB4_BA_MAX_USB3 = 0x1,
	USB4_BA_MIN_DP_AUX = 0x2,
	USB4_BA_MIN_DP_MAIN = 0x3,
	USB4_BA_MAX_PCIE = 0x4,
	USB4_BA_MAX_HI = 0x5,
};

#define USB4_BA_VALUE_MASK		GENMASK(31, 16)
#define USB4_BA_VALUE_SHIFT		16

/* Delays in us used with usb4_port_wait_for_bit() */
#define USB4_PORT_DELAY			50
#define USB4_PORT_SB_DELAY		5000

static int usb4_native_switch_op(struct tb_switch *sw, u16 opcode,
				 u32 *metadata, u8 *status,
				 const void *tx_data, size_t tx_dwords,
				 void *rx_data, size_t rx_dwords)
{
	u32 val;
	int ret;

	if (metadata) {
		ret = tb_sw_write(sw, metadata, TB_CFG_SWITCH, ROUTER_CS_25, 1);
		if (ret)
			return ret;
	}
	if (tx_dwords) {
		ret = tb_sw_write(sw, tx_data, TB_CFG_SWITCH, ROUTER_CS_9,
				  tx_dwords);
		if (ret)
			return ret;
	}

	val = opcode | ROUTER_CS_26_OV;
	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH, ROUTER_CS_26, 1);
	if (ret)
		return ret;

	ret = tb_switch_wait_for_bit(sw, ROUTER_CS_26, ROUTER_CS_26_OV, 0, 500);
	if (ret)
		return ret;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_26, 1);
	if (ret)
		return ret;

	if (val & ROUTER_CS_26_ONS)
		return -EOPNOTSUPP;

	if (status)
		*status = (val & ROUTER_CS_26_STATUS_MASK) >>
			ROUTER_CS_26_STATUS_SHIFT;

	if (metadata) {
		ret = tb_sw_read(sw, metadata, TB_CFG_SWITCH, ROUTER_CS_25, 1);
		if (ret)
			return ret;
	}
	if (rx_dwords) {
		ret = tb_sw_read(sw, rx_data, TB_CFG_SWITCH, ROUTER_CS_9,
				 rx_dwords);
		if (ret)
			return ret;
	}

	return 0;
}

static int __usb4_switch_op(struct tb_switch *sw, u16 opcode, u32 *metadata,
			    u8 *status, const void *tx_data, size_t tx_dwords,
			    void *rx_data, size_t rx_dwords)
{
	const struct tb_cm_ops *cm_ops = sw->tb->cm_ops;

	if (tx_dwords > USB4_DATA_DWORDS || rx_dwords > USB4_DATA_DWORDS)
		return -EINVAL;

	/*
	 * If the connection manager implementation provides USB4 router
	 * operation proxy callback, call it here instead of running the
	 * operation natively.
	 */
	if (cm_ops->usb4_switch_op) {
		int ret;

		ret = cm_ops->usb4_switch_op(sw, opcode, metadata, status,
					     tx_data, tx_dwords, rx_data,
					     rx_dwords);
		if (ret != -EOPNOTSUPP)
			return ret;

		/*
		 * If the proxy was not supported then run the native
		 * router operation instead.
		 */
	}

	return usb4_native_switch_op(sw, opcode, metadata, status, tx_data,
				     tx_dwords, rx_data, rx_dwords);
}

static inline int usb4_switch_op(struct tb_switch *sw, u16 opcode,
				 u32 *metadata, u8 *status)
{
	return __usb4_switch_op(sw, opcode, metadata, status, NULL, 0, NULL, 0);
}

static inline int usb4_switch_op_data(struct tb_switch *sw, u16 opcode,
				      u32 *metadata, u8 *status,
				      const void *tx_data, size_t tx_dwords,
				      void *rx_data, size_t rx_dwords)
{
	return __usb4_switch_op(sw, opcode, metadata, status, tx_data,
				tx_dwords, rx_data, rx_dwords);
}

/**
 * usb4_switch_check_wakes() - Check for wakes and notify PM core about them
 * @sw: Router whose wakes to check
 *
 * Checks wakes occurred during suspend and notify the PM core about them.
 */
void usb4_switch_check_wakes(struct tb_switch *sw)
{
	bool wakeup_usb4 = false;
	struct usb4_port *usb4;
	struct tb_port *port;
	bool wakeup = false;
	u32 val;

	if (tb_route(sw)) {
		if (tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_6, 1))
			return;

		tb_sw_dbg(sw, "PCIe wake: %s, USB3 wake: %s\n",
			  (val & ROUTER_CS_6_WOPS) ? "yes" : "no",
			  (val & ROUTER_CS_6_WOUS) ? "yes" : "no");

		wakeup = val & (ROUTER_CS_6_WOPS | ROUTER_CS_6_WOUS);
	}

	/*
	 * Check for any downstream ports for USB4 wake,
	 * connection wake and disconnection wake.
	 */
	tb_switch_for_each_port(sw, port) {
		if (!port->cap_usb4)
			continue;

		if (tb_port_read(port, &val, TB_CFG_PORT,
				 port->cap_usb4 + PORT_CS_18, 1))
			break;

		tb_port_dbg(port, "USB4 wake: %s, connection wake: %s, disconnection wake: %s\n",
			    (val & PORT_CS_18_WOU4S) ? "yes" : "no",
			    (val & PORT_CS_18_WOCS) ? "yes" : "no",
			    (val & PORT_CS_18_WODS) ? "yes" : "no");

		wakeup_usb4 = val & (PORT_CS_18_WOU4S | PORT_CS_18_WOCS |
				     PORT_CS_18_WODS);

		usb4 = port->usb4;
		if (device_may_wakeup(&usb4->dev) && wakeup_usb4)
			pm_wakeup_event(&usb4->dev, 0);

		wakeup |= wakeup_usb4;
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
 *
 * This does not set the configuration valid bit of the router. To do
 * that call usb4_switch_configuration_valid().
 */
int usb4_switch_setup(struct tb_switch *sw)
{
	struct tb_switch *parent = tb_switch_parent(sw);
	struct tb_port *down;
	bool tbt3, xhci;
	u32 val = 0;
	int ret;

	if (!tb_route(sw))
		return 0;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_6, 1);
	if (ret)
		return ret;

	down = tb_switch_downstream_port(sw);
	sw->link_usb4 = link_is_usb4(down);
	tb_sw_dbg(sw, "link: %s\n", sw->link_usb4 ? "USB4" : "TBT");

	xhci = val & ROUTER_CS_6_HCI;
	tbt3 = !(val & ROUTER_CS_6_TNS);

	tb_sw_dbg(sw, "TBT3 support: %s, xHCI: %s\n",
		  tbt3 ? "yes" : "no", xhci ? "yes" : "no");

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
	if (ret)
		return ret;

	if (tb_acpi_may_tunnel_usb3() && sw->link_usb4 &&
	    tb_switch_find_port(parent, TB_TYPE_USB3_DOWN)) {
		val |= ROUTER_CS_5_UTO;
		xhci = false;
	}

	/*
	 * Only enable PCIe tunneling if the parent router supports it
	 * and it is not disabled.
	 */
	if (tb_acpi_may_tunnel_pcie() &&
	    tb_switch_find_port(parent, TB_TYPE_PCIE_DOWN)) {
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
	val &= ~ROUTER_CS_5_CNS;

	return tb_sw_write(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
}

/**
 * usb4_switch_configuration_valid() - Set tunneling configuration to be valid
 * @sw: USB4 router
 *
 * Sets configuration valid bit for the router. Must be called before
 * any tunnels can be set through the router and after
 * usb4_switch_setup() has been called. Can be called to host and device
 * routers (does nothing for the latter).
 *
 * Returns %0 in success and negative errno otherwise.
 */
int usb4_switch_configuration_valid(struct tb_switch *sw)
{
	u32 val;
	int ret;

	if (!tb_route(sw))
		return 0;

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
	if (ret)
		return ret;

	val |= ROUTER_CS_5_CV;

	ret = tb_sw_write(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
	if (ret)
		return ret;

	return tb_switch_wait_for_bit(sw, ROUTER_CS_6, ROUTER_CS_6_CR,
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

	ret = usb4_switch_op_data(sw, USB4_SWITCH_OP_DROM_READ, &metadata,
				  &status, NULL, 0, buf, dwords);
	if (ret)
		return ret;

	return status ? -EIO : 0;
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
	return tb_nvm_read_data(address, buf, size, USB4_DATA_RETRIES,
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
	struct usb4_port *usb4;
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

		if (tb_is_upstream_port(port)) {
			val |= PORT_CS_19_WOU4;
		} else {
			bool configured = val & PORT_CS_19_PC;
			usb4 = port->usb4;

			if (((flags & TB_WAKE_ON_CONNECT) |
			      device_may_wakeup(&usb4->dev)) && !configured)
				val |= PORT_CS_19_WOC;
			if (((flags & TB_WAKE_ON_DISCONNECT) |
			      device_may_wakeup(&usb4->dev)) && configured)
				val |= PORT_CS_19_WOD;
			if ((flags & TB_WAKE_ON_USB4) && configured)
				val |= PORT_CS_19_WOU4;
		}

		ret = tb_port_write(port, &val, TB_CFG_PORT,
				    port->cap_usb4 + PORT_CS_19, 1);
		if (ret)
			return ret;
	}

	/*
	 * Enable wakes from PCIe, USB 3.x and DP on this router. Only
	 * needed for device routers.
	 */
	if (route) {
		ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_5, 1);
		if (ret)
			return ret;

		val &= ~(ROUTER_CS_5_WOP | ROUTER_CS_5_WOU | ROUTER_CS_5_WOD);
		if (flags & TB_WAKE_ON_USB3)
			val |= ROUTER_CS_5_WOU;
		if (flags & TB_WAKE_ON_PCIE)
			val |= ROUTER_CS_5_WOP;
		if (flags & TB_WAKE_ON_DP)
			val |= ROUTER_CS_5_WOD;

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

	return tb_switch_wait_for_bit(sw, ROUTER_CS_6, ROUTER_CS_6_SLPR,
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

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_NVM_SECTOR_SIZE, &metadata,
			     &status);
	if (ret)
		return ret;

	if (status)
		return status == 0x2 ? -EOPNOTSUPP : -EIO;

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

	ret = usb4_switch_op_data(sw, USB4_SWITCH_OP_NVM_READ, &metadata,
				  &status, NULL, 0, buf, dwords);
	if (ret)
		return ret;

	return status ? -EIO : 0;
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
	return tb_nvm_read_data(address, buf, size, USB4_DATA_RETRIES,
				usb4_switch_nvm_read_block, sw);
}

/**
 * usb4_switch_nvm_set_offset() - Set NVM write offset
 * @sw: USB4 router
 * @address: Start offset
 *
 * Explicitly sets NVM write offset. Normally when writing to NVM this
 * is done automatically by usb4_switch_nvm_write().
 *
 * Returns %0 in success and negative errno if there was a failure.
 */
int usb4_switch_nvm_set_offset(struct tb_switch *sw, unsigned int address)
{
	u32 metadata, dwaddress;
	u8 status = 0;
	int ret;

	dwaddress = address / 4;
	metadata = (dwaddress << USB4_NVM_SET_OFFSET_SHIFT) &
		   USB4_NVM_SET_OFFSET_MASK;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_NVM_SET_OFFSET, &metadata,
			     &status);
	if (ret)
		return ret;

	return status ? -EIO : 0;
}

static int usb4_switch_nvm_write_next_block(void *data, unsigned int dwaddress,
					    const void *buf, size_t dwords)
{
	struct tb_switch *sw = data;
	u8 status;
	int ret;

	ret = usb4_switch_op_data(sw, USB4_SWITCH_OP_NVM_WRITE, NULL, &status,
				  buf, dwords, NULL, 0);
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

	return tb_nvm_write_data(address, buf, size, USB4_DATA_RETRIES,
				 usb4_switch_nvm_write_next_block, sw);
}

/**
 * usb4_switch_nvm_authenticate() - Authenticate new NVM
 * @sw: USB4 router
 *
 * After the new NVM has been written via usb4_switch_nvm_write(), this
 * function triggers NVM authentication process. The router gets power
 * cycled and if the authentication is successful the new NVM starts
 * running. In case of failure returns negative errno.
 *
 * The caller should call usb4_switch_nvm_authenticate_status() to read
 * the status of the authentication after power cycle. It should be the
 * first router operation to avoid the status being lost.
 */
int usb4_switch_nvm_authenticate(struct tb_switch *sw)
{
	int ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_NVM_AUTH, NULL, NULL);
	switch (ret) {
	/*
	 * The router is power cycled once NVM_AUTH is started so it is
	 * expected to get any of the following errors back.
	 */
	case -EACCES:
	case -ENOTCONN:
	case -ETIMEDOUT:
		return 0;

	default:
		return ret;
	}
}

/**
 * usb4_switch_nvm_authenticate_status() - Read status of last NVM authenticate
 * @sw: USB4 router
 * @status: Status code of the operation
 *
 * The function checks if there is status available from the last NVM
 * authenticate router operation. If there is status then %0 is returned
 * and the status code is placed in @status. Returns negative errno in case
 * of failure.
 *
 * Must be called before any other router operation.
 */
int usb4_switch_nvm_authenticate_status(struct tb_switch *sw, u32 *status)
{
	const struct tb_cm_ops *cm_ops = sw->tb->cm_ops;
	u16 opcode;
	u32 val;
	int ret;

	if (cm_ops->usb4_switch_nvm_authenticate_status) {
		ret = cm_ops->usb4_switch_nvm_authenticate_status(sw, status);
		if (ret != -EOPNOTSUPP)
			return ret;
	}

	ret = tb_sw_read(sw, &val, TB_CFG_SWITCH, ROUTER_CS_26, 1);
	if (ret)
		return ret;

	/* Check that the opcode is correct */
	opcode = val & ROUTER_CS_26_OPCODE_MASK;
	if (opcode == USB4_SWITCH_OP_NVM_AUTH) {
		if (val & ROUTER_CS_26_OV)
			return -EBUSY;
		if (val & ROUTER_CS_26_ONS)
			return -EOPNOTSUPP;

		*status = (val & ROUTER_CS_26_STATUS_MASK) >>
			ROUTER_CS_26_STATUS_SHIFT;
	} else {
		*status = 0;
	}

	return 0;
}

/**
 * usb4_switch_credits_init() - Read buffer allocation parameters
 * @sw: USB4 router
 *
 * Reads @sw buffer allocation parameters and initializes @sw buffer
 * allocation fields accordingly. Specifically @sw->credits_allocation
 * is set to %true if these parameters can be used in tunneling.
 *
 * Returns %0 on success and negative errno otherwise.
 */
int usb4_switch_credits_init(struct tb_switch *sw)
{
	int max_usb3, min_dp_aux, min_dp_main, max_pcie, max_dma;
	int ret, length, i, nports;
	const struct tb_port *port;
	u32 data[USB4_DATA_DWORDS];
	u32 metadata = 0;
	u8 status = 0;

	memset(data, 0, sizeof(data));
	ret = usb4_switch_op_data(sw, USB4_SWITCH_OP_BUFFER_ALLOC, &metadata,
				  &status, NULL, 0, data, ARRAY_SIZE(data));
	if (ret)
		return ret;
	if (status)
		return -EIO;

	length = metadata & USB4_BA_LENGTH_MASK;
	if (WARN_ON(length > ARRAY_SIZE(data)))
		return -EMSGSIZE;

	max_usb3 = -1;
	min_dp_aux = -1;
	min_dp_main = -1;
	max_pcie = -1;
	max_dma = -1;

	tb_sw_dbg(sw, "credit allocation parameters:\n");

	for (i = 0; i < length; i++) {
		u16 index, value;

		index = data[i] & USB4_BA_INDEX_MASK;
		value = (data[i] & USB4_BA_VALUE_MASK) >> USB4_BA_VALUE_SHIFT;

		switch (index) {
		case USB4_BA_MAX_USB3:
			tb_sw_dbg(sw, " USB3: %u\n", value);
			max_usb3 = value;
			break;
		case USB4_BA_MIN_DP_AUX:
			tb_sw_dbg(sw, " DP AUX: %u\n", value);
			min_dp_aux = value;
			break;
		case USB4_BA_MIN_DP_MAIN:
			tb_sw_dbg(sw, " DP main: %u\n", value);
			min_dp_main = value;
			break;
		case USB4_BA_MAX_PCIE:
			tb_sw_dbg(sw, " PCIe: %u\n", value);
			max_pcie = value;
			break;
		case USB4_BA_MAX_HI:
			tb_sw_dbg(sw, " DMA: %u\n", value);
			max_dma = value;
			break;
		default:
			tb_sw_dbg(sw, " unknown credit allocation index %#x, skipping\n",
				  index);
			break;
		}
	}

	/*
	 * Validate the buffer allocation preferences. If we find
	 * issues, log a warning and fall back using the hard-coded
	 * values.
	 */

	/* Host router must report baMaxHI */
	if (!tb_route(sw) && max_dma < 0) {
		tb_sw_warn(sw, "host router is missing baMaxHI\n");
		goto err_invalid;
	}

	nports = 0;
	tb_switch_for_each_port(sw, port) {
		if (tb_port_is_null(port))
			nports++;
	}

	/* Must have DP buffer allocation (multiple USB4 ports) */
	if (nports > 2 && (min_dp_aux < 0 || min_dp_main < 0)) {
		tb_sw_warn(sw, "multiple USB4 ports require baMinDPaux/baMinDPmain\n");
		goto err_invalid;
	}

	tb_switch_for_each_port(sw, port) {
		if (tb_port_is_dpout(port) && min_dp_main < 0) {
			tb_sw_warn(sw, "missing baMinDPmain");
			goto err_invalid;
		}
		if ((tb_port_is_dpin(port) || tb_port_is_dpout(port)) &&
		    min_dp_aux < 0) {
			tb_sw_warn(sw, "missing baMinDPaux");
			goto err_invalid;
		}
		if ((tb_port_is_usb3_down(port) || tb_port_is_usb3_up(port)) &&
		    max_usb3 < 0) {
			tb_sw_warn(sw, "missing baMaxUSB3");
			goto err_invalid;
		}
		if ((tb_port_is_pcie_down(port) || tb_port_is_pcie_up(port)) &&
		    max_pcie < 0) {
			tb_sw_warn(sw, "missing baMaxPCIe");
			goto err_invalid;
		}
	}

	/*
	 * Buffer allocation passed the validation so we can use it in
	 * path creation.
	 */
	sw->credit_allocation = true;
	if (max_usb3 > 0)
		sw->max_usb3_credits = max_usb3;
	if (min_dp_aux > 0)
		sw->min_dp_aux_credits = min_dp_aux;
	if (min_dp_main > 0)
		sw->min_dp_main_credits = min_dp_main;
	if (max_pcie > 0)
		sw->max_pcie_credits = max_pcie;
	if (max_dma > 0)
		sw->max_dma_credits = max_dma;

	return 0;

err_invalid:
	return -EINVAL;
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
	u32 metadata = in->port;
	u8 status;
	int ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_QUERY_DP_RESOURCE, &metadata,
			     &status);
	/*
	 * If DP resource allocation is not supported assume it is
	 * always available.
	 */
	if (ret == -EOPNOTSUPP)
		return true;
	if (ret)
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
	u32 metadata = in->port;
	u8 status;
	int ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_ALLOC_DP_RESOURCE, &metadata,
			     &status);
	if (ret == -EOPNOTSUPP)
		return 0;
	if (ret)
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
	u32 metadata = in->port;
	u8 status;
	int ret;

	ret = usb4_switch_op(sw, USB4_SWITCH_OP_DEALLOC_DP_RESOURCE, &metadata,
			     &status);
	if (ret == -EOPNOTSUPP)
		return 0;
	if (ret)
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
 * usb4_switch_add_ports() - Add USB4 ports for this router
 * @sw: USB4 router
 *
 * For USB4 router finds all USB4 ports and registers devices for each.
 * Can be called to any router.
 *
 * Return %0 in case of success and negative errno in case of failure.
 */
int usb4_switch_add_ports(struct tb_switch *sw)
{
	struct tb_port *port;

	if (tb_switch_is_icm(sw) || !tb_switch_is_usb4(sw))
		return 0;

	tb_switch_for_each_port(sw, port) {
		struct usb4_port *usb4;

		if (!tb_port_is_null(port))
			continue;
		if (!port->cap_usb4)
			continue;

		usb4 = usb4_port_device_add(port);
		if (IS_ERR(usb4)) {
			usb4_switch_remove_ports(sw);
			return PTR_ERR(usb4);
		}

		port->usb4 = usb4;
	}

	return 0;
}

/**
 * usb4_switch_remove_ports() - Removes USB4 ports from this router
 * @sw: USB4 router
 *
 * Unregisters previously registered USB4 ports.
 */
void usb4_switch_remove_ports(struct tb_switch *sw)
{
	struct tb_port *port;

	tb_switch_for_each_port(sw, port) {
		if (port->usb4) {
			usb4_port_device_remove(port->usb4);
			port->usb4 = NULL;
		}
	}
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

/**
 * usb4_port_reset() - Issue downstream port reset
 * @port: USB4 port to reset
 *
 * Issues downstream port reset to @port.
 */
int usb4_port_reset(struct tb_port *port)
{
	int ret;
	u32 val;

	if (!port->cap_usb4)
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_usb4 + PORT_CS_19, 1);
	if (ret)
		return ret;

	val |= PORT_CS_19_DPR;

	ret = tb_port_write(port, &val, TB_CFG_PORT,
			    port->cap_usb4 + PORT_CS_19, 1);
	if (ret)
		return ret;

	fsleep(10000);

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_usb4 + PORT_CS_19, 1);
	if (ret)
		return ret;

	val &= ~PORT_CS_19_DPR;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_usb4 + PORT_CS_19, 1);
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
 * @xd: XDomain that is connected to the port
 *
 * Marks the USB4 port as being connected to another host and updates
 * the link type. Returns %0 in success and negative errno in failure.
 */
int usb4_port_configure_xdomain(struct tb_port *port, struct tb_xdomain *xd)
{
	xd->link_usb4 = link_is_usb4(port);
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
			  u32 value, int timeout_msec, unsigned long delay_usec)
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

		fsleep(delay_usec);
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
				     PORT_CS_1_PND, 0, 500, USB4_PORT_SB_DELAY);
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
				     PORT_CS_1_PND, 0, 500, USB4_PORT_SB_DELAY);
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

static int usb4_port_sb_opcode_err_to_errno(u32 val)
{
	switch (val) {
	case 0:
		return 0;
	case USB4_SB_OPCODE_ERR:
		return -EAGAIN;
	case USB4_SB_OPCODE_ONS:
		return -EOPNOTSUPP;
	default:
		return -EIO;
	}
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

		if (val != opcode)
			return usb4_port_sb_opcode_err_to_errno(val);

		fsleep(USB4_PORT_SB_DELAY);
	} while (ktime_before(ktime_get(), timeout));

	return -ETIMEDOUT;
}

static int usb4_port_set_router_offline(struct tb_port *port, bool offline)
{
	u32 val = !offline;
	int ret;

	ret = usb4_port_sb_write(port, USB4_SB_TARGET_ROUTER, 0,
				  USB4_SB_METADATA, &val, sizeof(val));
	if (ret)
		return ret;

	val = USB4_SB_OPCODE_ROUTER_OFFLINE;
	return usb4_port_sb_write(port, USB4_SB_TARGET_ROUTER, 0,
				  USB4_SB_OPCODE, &val, sizeof(val));
}

/**
 * usb4_port_router_offline() - Put the USB4 port to offline mode
 * @port: USB4 port
 *
 * This function puts the USB4 port into offline mode. In this mode the
 * port does not react on hotplug events anymore. This needs to be
 * called before retimer access is done when the USB4 links is not up.
 *
 * Returns %0 in case of success and negative errno if there was an
 * error.
 */
int usb4_port_router_offline(struct tb_port *port)
{
	return usb4_port_set_router_offline(port, true);
}

/**
 * usb4_port_router_online() - Put the USB4 port back to online
 * @port: USB4 port
 *
 * Makes the USB4 port functional again.
 */
int usb4_port_router_online(struct tb_port *port)
{
	return usb4_port_set_router_offline(port, false);
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

/**
 * usb4_port_clx_supported() - Check if CLx is supported by the link
 * @port: Port to check for CLx support for
 *
 * PORT_CS_18_CPS bit reflects if the link supports CLx including
 * active cables (if connected on the link).
 */
bool usb4_port_clx_supported(struct tb_port *port)
{
	int ret;
	u32 val;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_usb4 + PORT_CS_18, 1);
	if (ret)
		return false;

	return !!(val & PORT_CS_18_CPS);
}

/**
 * usb4_port_asym_supported() - If the port supports asymmetric link
 * @port: USB4 port
 *
 * Checks if the port and the cable supports asymmetric link and returns
 * %true in that case.
 */
bool usb4_port_asym_supported(struct tb_port *port)
{
	u32 val;

	if (!port->cap_usb4)
		return false;

	if (tb_port_read(port, &val, TB_CFG_PORT, port->cap_usb4 + PORT_CS_18, 1))
		return false;

	return !!(val & PORT_CS_18_CSA);
}

/**
 * usb4_port_asym_set_link_width() - Set link width to asymmetric or symmetric
 * @port: USB4 port
 * @width: Asymmetric width to configure
 *
 * Sets USB4 port link width to @width. Can be called for widths where
 * usb4_port_asym_width_supported() returned @true.
 */
int usb4_port_asym_set_link_width(struct tb_port *port, enum tb_link_width width)
{
	u32 val;
	int ret;

	if (!port->cap_phy)
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_phy + LANE_ADP_CS_1, 1);
	if (ret)
		return ret;

	val &= ~LANE_ADP_CS_1_TARGET_WIDTH_ASYM_MASK;
	switch (width) {
	case TB_LINK_WIDTH_DUAL:
		val |= FIELD_PREP(LANE_ADP_CS_1_TARGET_WIDTH_ASYM_MASK,
				  LANE_ADP_CS_1_TARGET_WIDTH_ASYM_DUAL);
		break;
	case TB_LINK_WIDTH_ASYM_TX:
		val |= FIELD_PREP(LANE_ADP_CS_1_TARGET_WIDTH_ASYM_MASK,
				  LANE_ADP_CS_1_TARGET_WIDTH_ASYM_TX);
		break;
	case TB_LINK_WIDTH_ASYM_RX:
		val |= FIELD_PREP(LANE_ADP_CS_1_TARGET_WIDTH_ASYM_MASK,
				  LANE_ADP_CS_1_TARGET_WIDTH_ASYM_RX);
		break;
	default:
		return -EINVAL;
	}

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_phy + LANE_ADP_CS_1, 1);
}

/**
 * usb4_port_asym_start() - Start symmetry change and wait for completion
 * @port: USB4 port
 *
 * Start symmetry change of the link to asymmetric or symmetric
 * (according to what was previously set in tb_port_set_link_width().
 * Wait for completion of the change.
 *
 * Returns %0 in case of success, %-ETIMEDOUT if case of timeout or
 * a negative errno in case of a failure.
 */
int usb4_port_asym_start(struct tb_port *port)
{
	int ret;
	u32 val;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_usb4 + PORT_CS_19, 1);
	if (ret)
		return ret;

	val &= ~PORT_CS_19_START_ASYM;
	val |= FIELD_PREP(PORT_CS_19_START_ASYM, 1);

	ret = tb_port_write(port, &val, TB_CFG_PORT,
			    port->cap_usb4 + PORT_CS_19, 1);
	if (ret)
		return ret;

	/*
	 * Wait for PORT_CS_19_START_ASYM to be 0. This means the USB4
	 * port started the symmetry transition.
	 */
	ret = usb4_port_wait_for_bit(port, port->cap_usb4 + PORT_CS_19,
				     PORT_CS_19_START_ASYM, 0, 1000,
				     USB4_PORT_DELAY);
	if (ret)
		return ret;

	/* Then wait for the transtion to be completed */
	return usb4_port_wait_for_bit(port, port->cap_usb4 + PORT_CS_18,
				      PORT_CS_18_TIP, 0, 5000, USB4_PORT_DELAY);
}

/**
 * usb4_port_margining_caps() - Read USB4 port marginig capabilities
 * @port: USB4 port
 * @caps: Array with at least two elements to hold the results
 *
 * Reads the USB4 port lane margining capabilities into @caps.
 */
int usb4_port_margining_caps(struct tb_port *port, u32 *caps)
{
	int ret;

	ret = usb4_port_sb_op(port, USB4_SB_TARGET_ROUTER, 0,
			      USB4_SB_OPCODE_READ_LANE_MARGINING_CAP, 500);
	if (ret)
		return ret;

	return usb4_port_sb_read(port, USB4_SB_TARGET_ROUTER, 0,
				 USB4_SB_DATA, caps, sizeof(*caps) * 2);
}

/**
 * usb4_port_hw_margin() - Run hardware lane margining on port
 * @port: USB4 port
 * @lanes: Which lanes to run (must match the port capabilities). Can be
 *	   %0, %1 or %7.
 * @ber_level: BER level contour value
 * @timing: Perform timing margining instead of voltage
 * @right_high: Use Right/high margin instead of left/low
 * @results: Array with at least two elements to hold the results
 *
 * Runs hardware lane margining on USB4 port and returns the result in
 * @results.
 */
int usb4_port_hw_margin(struct tb_port *port, unsigned int lanes,
			unsigned int ber_level, bool timing, bool right_high,
			u32 *results)
{
	u32 val;
	int ret;

	val = lanes;
	if (timing)
		val |= USB4_MARGIN_HW_TIME;
	if (right_high)
		val |= USB4_MARGIN_HW_RH;
	if (ber_level)
		val |= (ber_level << USB4_MARGIN_HW_BER_SHIFT) &
			USB4_MARGIN_HW_BER_MASK;

	ret = usb4_port_sb_write(port, USB4_SB_TARGET_ROUTER, 0,
				 USB4_SB_METADATA, &val, sizeof(val));
	if (ret)
		return ret;

	ret = usb4_port_sb_op(port, USB4_SB_TARGET_ROUTER, 0,
			      USB4_SB_OPCODE_RUN_HW_LANE_MARGINING, 2500);
	if (ret)
		return ret;

	return usb4_port_sb_read(port, USB4_SB_TARGET_ROUTER, 0,
				 USB4_SB_DATA, results, sizeof(*results) * 2);
}

/**
 * usb4_port_sw_margin() - Run software lane margining on port
 * @port: USB4 port
 * @lanes: Which lanes to run (must match the port capabilities). Can be
 *	   %0, %1 or %7.
 * @timing: Perform timing margining instead of voltage
 * @right_high: Use Right/high margin instead of left/low
 * @counter: What to do with the error counter
 *
 * Runs software lane margining on USB4 port. Read back the error
 * counters by calling usb4_port_sw_margin_errors(). Returns %0 in
 * success and negative errno otherwise.
 */
int usb4_port_sw_margin(struct tb_port *port, unsigned int lanes, bool timing,
			bool right_high, u32 counter)
{
	u32 val;
	int ret;

	val = lanes;
	if (timing)
		val |= USB4_MARGIN_SW_TIME;
	if (right_high)
		val |= USB4_MARGIN_SW_RH;
	val |= (counter << USB4_MARGIN_SW_COUNTER_SHIFT) &
		USB4_MARGIN_SW_COUNTER_MASK;

	ret = usb4_port_sb_write(port, USB4_SB_TARGET_ROUTER, 0,
				 USB4_SB_METADATA, &val, sizeof(val));
	if (ret)
		return ret;

	return usb4_port_sb_op(port, USB4_SB_TARGET_ROUTER, 0,
			       USB4_SB_OPCODE_RUN_SW_LANE_MARGINING, 2500);
}

/**
 * usb4_port_sw_margin_errors() - Read the software margining error counters
 * @port: USB4 port
 * @errors: Error metadata is copied here.
 *
 * This reads back the software margining error counters from the port.
 * Returns %0 in success and negative errno otherwise.
 */
int usb4_port_sw_margin_errors(struct tb_port *port, u32 *errors)
{
	int ret;

	ret = usb4_port_sb_op(port, USB4_SB_TARGET_ROUTER, 0,
			      USB4_SB_OPCODE_READ_SW_MARGIN_ERR, 150);
	if (ret)
		return ret;

	return usb4_port_sb_read(port, USB4_SB_TARGET_ROUTER, 0,
				 USB4_SB_METADATA, errors, sizeof(*errors));
}

static inline int usb4_port_retimer_op(struct tb_port *port, u8 index,
				       enum usb4_sb_opcode opcode,
				       int timeout_msec)
{
	return usb4_port_sb_op(port, USB4_SB_TARGET_RETIMER, index, opcode,
			       timeout_msec);
}

/**
 * usb4_port_retimer_set_inbound_sbtx() - Enable sideband channel transactions
 * @port: USB4 port
 * @index: Retimer index
 *
 * Enables sideband channel transations on SBTX. Can be used when USB4
 * link does not go up, for example if there is no device connected.
 */
int usb4_port_retimer_set_inbound_sbtx(struct tb_port *port, u8 index)
{
	int ret;

	ret = usb4_port_retimer_op(port, index, USB4_SB_OPCODE_SET_INBOUND_SBTX,
				   500);

	if (ret != -ENODEV)
		return ret;

	/*
	 * Per the USB4 retimer spec, the retimer is not required to
	 * send an RT (Retimer Transaction) response for the first
	 * SET_INBOUND_SBTX command
	 */
	return usb4_port_retimer_op(port, index, USB4_SB_OPCODE_SET_INBOUND_SBTX,
				    500);
}

/**
 * usb4_port_retimer_unset_inbound_sbtx() - Disable sideband channel transactions
 * @port: USB4 port
 * @index: Retimer index
 *
 * Disables sideband channel transations on SBTX. The reverse of
 * usb4_port_retimer_set_inbound_sbtx().
 */
int usb4_port_retimer_unset_inbound_sbtx(struct tb_port *port, u8 index)
{
	return usb4_port_retimer_op(port, index,
				    USB4_SB_OPCODE_UNSET_INBOUND_SBTX, 500);
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

/**
 * usb4_port_retimer_nvm_set_offset() - Set NVM write offset
 * @port: USB4 port
 * @index: Retimer index
 * @address: Start offset
 *
 * Exlicitly sets NVM write offset. Normally when writing to NVM this is
 * done automatically by usb4_port_retimer_nvm_write().
 *
 * Returns %0 in success and negative errno if there was a failure.
 */
int usb4_port_retimer_nvm_set_offset(struct tb_port *port, u8 index,
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

static int usb4_port_retimer_nvm_write_next_block(void *data,
	unsigned int dwaddress, const void *buf, size_t dwords)

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

	return tb_nvm_write_data(address, buf, size, USB4_DATA_RETRIES,
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

	ret = usb4_port_sb_opcode_err_to_errno(val);
	switch (ret) {
	case 0:
		*status = 0;
		return 0;

	case -EAGAIN:
		ret = usb4_port_retimer_read(port, index, USB4_SB_METADATA,
					     &metadata, sizeof(metadata));
		if (ret)
			return ret;

		*status = metadata & USB4_SB_METADATA_NVM_AUTH_WRITE_MASK;
		return 0;

	default:
		return ret;
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

	return tb_nvm_read_data(address, buf, size, USB4_DATA_RETRIES,
				usb4_port_retimer_nvm_read_block, &info);
}

static inline unsigned int
usb4_usb3_port_max_bandwidth(const struct tb_port *port, unsigned int bw)
{
	/* Take the possible bandwidth limitation into account */
	if (port->max_bw)
		return min(bw, port->max_bw);
	return bw;
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
	ret = lr == ADP_USB3_CS_4_MSLR_20G ? 20000 : 10000;

	return usb4_usb3_port_max_bandwidth(port, ret);
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
				      ADP_USB3_CS_1_HCA, val, 1500,
				      USB4_PORT_DELAY);
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
	return DIV_ROUND_CLOSEST(uframes * 8000, MEGA);
}

static u32 mbps_to_usb3_bw(unsigned int mbps, u8 scale)
{
	unsigned long uframes;

	/* 1 uframe is 1/8 ms (125 us) -> 1 / 8000 s */
	uframes = ((unsigned long)mbps * MEGA) / 8000;
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
	int ret, max_bw;

	/* Figure out suitable scale */
	scale = 0;
	max_bw = max(upstream_bw, downstream_bw);
	while (scale < 64) {
		if (mbps_to_usb3_bw(max_bw, scale) < 4096)
			break;
		scale++;
	}

	if (WARN_ON(scale >= 64))
		return -EINVAL;

	ret = tb_port_write(port, &scale, TB_CFG_PORT,
			    port->cap_adap + ADP_USB3_CS_3, 1);
	if (ret)
		return ret;

	ubw = mbps_to_usb3_bw(upstream_bw, scale);
	dbw = mbps_to_usb3_bw(downstream_bw, scale);

	tb_port_dbg(port, "scaled bandwidth %u/%u, scale %u\n", ubw, dbw, scale);

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
	 * Always keep 900 Mb/s to make sure xHCI has at least some
	 * bandwidth available for isochronous traffic.
	 */
	if (consumed_up < 900)
		consumed_up = 900;
	if (consumed_down < 900)
		consumed_down = 900;

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

static bool is_usb4_dpin(const struct tb_port *port)
{
	if (!tb_port_is_dpin(port))
		return false;
	if (!tb_switch_is_usb4(port->sw))
		return false;
	return true;
}

/**
 * usb4_dp_port_set_cm_id() - Assign CM ID to the DP IN adapter
 * @port: DP IN adapter
 * @cm_id: CM ID to assign
 *
 * Sets CM ID for the @port. Returns %0 on success and negative errno
 * otherwise. Speficially returns %-EOPNOTSUPP if the @port does not
 * support this.
 */
int usb4_dp_port_set_cm_id(struct tb_port *port, int cm_id)
{
	u32 val;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	val &= ~ADP_DP_CS_2_CM_ID_MASK;
	val |= cm_id << ADP_DP_CS_2_CM_ID_SHIFT;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_2, 1);
}

/**
 * usb4_dp_port_bandwidth_mode_supported() - Is the bandwidth allocation mode
 *					     supported
 * @port: DP IN adapter to check
 *
 * Can be called to any DP IN adapter. Returns true if the adapter
 * supports USB4 bandwidth allocation mode, false otherwise.
 */
bool usb4_dp_port_bandwidth_mode_supported(struct tb_port *port)
{
	int ret;
	u32 val;

	if (!is_usb4_dpin(port))
		return false;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + DP_LOCAL_CAP, 1);
	if (ret)
		return false;

	return !!(val & DP_COMMON_CAP_BW_MODE);
}

/**
 * usb4_dp_port_bandwidth_mode_enabled() - Is the bandwidth allocation mode
 *					   enabled
 * @port: DP IN adapter to check
 *
 * Can be called to any DP IN adapter. Returns true if the bandwidth
 * allocation mode has been enabled, false otherwise.
 */
bool usb4_dp_port_bandwidth_mode_enabled(struct tb_port *port)
{
	int ret;
	u32 val;

	if (!is_usb4_dpin(port))
		return false;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_8, 1);
	if (ret)
		return false;

	return !!(val & ADP_DP_CS_8_DPME);
}

/**
 * usb4_dp_port_set_cm_bandwidth_mode_supported() - Set/clear CM support for
 *						    bandwidth allocation mode
 * @port: DP IN adapter
 * @supported: Does the CM support bandwidth allocation mode
 *
 * Can be called to any DP IN adapter. Sets or clears the CM support bit
 * of the DP IN adapter. Returns %0 in success and negative errno
 * otherwise. Specifically returns %-OPNOTSUPP if the passed in adapter
 * does not support this.
 */
int usb4_dp_port_set_cm_bandwidth_mode_supported(struct tb_port *port,
						 bool supported)
{
	u32 val;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	if (supported)
		val |= ADP_DP_CS_2_CMMS;
	else
		val &= ~ADP_DP_CS_2_CMMS;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_2, 1);
}

/**
 * usb4_dp_port_group_id() - Return Group ID assigned for the adapter
 * @port: DP IN adapter
 *
 * Reads bandwidth allocation Group ID from the DP IN adapter and
 * returns it. If the adapter does not support setting Group_ID
 * %-EOPNOTSUPP is returned.
 */
int usb4_dp_port_group_id(struct tb_port *port)
{
	u32 val;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	return (val & ADP_DP_CS_2_GROUP_ID_MASK) >> ADP_DP_CS_2_GROUP_ID_SHIFT;
}

/**
 * usb4_dp_port_set_group_id() - Set adapter Group ID
 * @port: DP IN adapter
 * @group_id: Group ID for the adapter
 *
 * Sets bandwidth allocation mode Group ID for the DP IN adapter.
 * Returns %0 in case of success and negative errno otherwise.
 * Specifically returns %-EOPNOTSUPP if the adapter does not support
 * this.
 */
int usb4_dp_port_set_group_id(struct tb_port *port, int group_id)
{
	u32 val;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	val &= ~ADP_DP_CS_2_GROUP_ID_MASK;
	val |= group_id << ADP_DP_CS_2_GROUP_ID_SHIFT;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_2, 1);
}

/**
 * usb4_dp_port_nrd() - Read non-reduced rate and lanes
 * @port: DP IN adapter
 * @rate: Non-reduced rate in Mb/s is placed here
 * @lanes: Non-reduced lanes are placed here
 *
 * Reads the non-reduced rate and lanes from the DP IN adapter. Returns
 * %0 in success and negative errno otherwise. Specifically returns
 * %-EOPNOTSUPP if the adapter does not support this.
 */
int usb4_dp_port_nrd(struct tb_port *port, int *rate, int *lanes)
{
	u32 val, tmp;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	tmp = (val & ADP_DP_CS_2_NRD_MLR_MASK) >> ADP_DP_CS_2_NRD_MLR_SHIFT;
	switch (tmp) {
	case DP_COMMON_CAP_RATE_RBR:
		*rate = 1620;
		break;
	case DP_COMMON_CAP_RATE_HBR:
		*rate = 2700;
		break;
	case DP_COMMON_CAP_RATE_HBR2:
		*rate = 5400;
		break;
	case DP_COMMON_CAP_RATE_HBR3:
		*rate = 8100;
		break;
	}

	tmp = val & ADP_DP_CS_2_NRD_MLC_MASK;
	switch (tmp) {
	case DP_COMMON_CAP_1_LANE:
		*lanes = 1;
		break;
	case DP_COMMON_CAP_2_LANES:
		*lanes = 2;
		break;
	case DP_COMMON_CAP_4_LANES:
		*lanes = 4;
		break;
	}

	return 0;
}

/**
 * usb4_dp_port_set_nrd() - Set non-reduced rate and lanes
 * @port: DP IN adapter
 * @rate: Non-reduced rate in Mb/s
 * @lanes: Non-reduced lanes
 *
 * Before the capabilities reduction this function can be used to set
 * the non-reduced values for the DP IN adapter. Returns %0 in success
 * and negative errno otherwise. If the adapter does not support this
 * %-EOPNOTSUPP is returned.
 */
int usb4_dp_port_set_nrd(struct tb_port *port, int rate, int lanes)
{
	u32 val;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	val &= ~ADP_DP_CS_2_NRD_MLR_MASK;

	switch (rate) {
	case 1620:
		break;
	case 2700:
		val |= (DP_COMMON_CAP_RATE_HBR << ADP_DP_CS_2_NRD_MLR_SHIFT)
			& ADP_DP_CS_2_NRD_MLR_MASK;
		break;
	case 5400:
		val |= (DP_COMMON_CAP_RATE_HBR2 << ADP_DP_CS_2_NRD_MLR_SHIFT)
			& ADP_DP_CS_2_NRD_MLR_MASK;
		break;
	case 8100:
		val |= (DP_COMMON_CAP_RATE_HBR3 << ADP_DP_CS_2_NRD_MLR_SHIFT)
			& ADP_DP_CS_2_NRD_MLR_MASK;
		break;
	default:
		return -EINVAL;
	}

	val &= ~ADP_DP_CS_2_NRD_MLC_MASK;

	switch (lanes) {
	case 1:
		break;
	case 2:
		val |= DP_COMMON_CAP_2_LANES;
		break;
	case 4:
		val |= DP_COMMON_CAP_4_LANES;
		break;
	default:
		return -EINVAL;
	}

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_2, 1);
}

/**
 * usb4_dp_port_granularity() - Return granularity for the bandwidth values
 * @port: DP IN adapter
 *
 * Reads the programmed granularity from @port. If the DP IN adapter does
 * not support bandwidth allocation mode returns %-EOPNOTSUPP and negative
 * errno in other error cases.
 */
int usb4_dp_port_granularity(struct tb_port *port)
{
	u32 val;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	val &= ADP_DP_CS_2_GR_MASK;
	val >>= ADP_DP_CS_2_GR_SHIFT;

	switch (val) {
	case ADP_DP_CS_2_GR_0_25G:
		return 250;
	case ADP_DP_CS_2_GR_0_5G:
		return 500;
	case ADP_DP_CS_2_GR_1G:
		return 1000;
	}

	return -EINVAL;
}

/**
 * usb4_dp_port_set_granularity() - Set granularity for the bandwidth values
 * @port: DP IN adapter
 * @granularity: Granularity in Mb/s. Supported values: 1000, 500 and 250.
 *
 * Sets the granularity used with the estimated, allocated and requested
 * bandwidth. Returns %0 in success and negative errno otherwise. If the
 * adapter does not support this %-EOPNOTSUPP is returned.
 */
int usb4_dp_port_set_granularity(struct tb_port *port, int granularity)
{
	u32 val;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	val &= ~ADP_DP_CS_2_GR_MASK;

	switch (granularity) {
	case 250:
		val |= ADP_DP_CS_2_GR_0_25G << ADP_DP_CS_2_GR_SHIFT;
		break;
	case 500:
		val |= ADP_DP_CS_2_GR_0_5G << ADP_DP_CS_2_GR_SHIFT;
		break;
	case 1000:
		val |= ADP_DP_CS_2_GR_1G << ADP_DP_CS_2_GR_SHIFT;
		break;
	default:
		return -EINVAL;
	}

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_2, 1);
}

/**
 * usb4_dp_port_set_estimated_bandwidth() - Set estimated bandwidth
 * @port: DP IN adapter
 * @bw: Estimated bandwidth in Mb/s.
 *
 * Sets the estimated bandwidth to @bw. Set the granularity by calling
 * usb4_dp_port_set_granularity() before calling this. The @bw is round
 * down to the closest granularity multiplier. Returns %0 in success
 * and negative errno otherwise. Specifically returns %-EOPNOTSUPP if
 * the adapter does not support this.
 */
int usb4_dp_port_set_estimated_bandwidth(struct tb_port *port, int bw)
{
	u32 val, granularity;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = usb4_dp_port_granularity(port);
	if (ret < 0)
		return ret;
	granularity = ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	val &= ~ADP_DP_CS_2_ESTIMATED_BW_MASK;
	val |= (bw / granularity) << ADP_DP_CS_2_ESTIMATED_BW_SHIFT;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_2, 1);
}

/**
 * usb4_dp_port_allocated_bandwidth() - Return allocated bandwidth
 * @port: DP IN adapter
 *
 * Reads and returns allocated bandwidth for @port in Mb/s (taking into
 * account the programmed granularity). Returns negative errno in case
 * of error.
 */
int usb4_dp_port_allocated_bandwidth(struct tb_port *port)
{
	u32 val, granularity;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = usb4_dp_port_granularity(port);
	if (ret < 0)
		return ret;
	granularity = ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + DP_STATUS, 1);
	if (ret)
		return ret;

	val &= DP_STATUS_ALLOCATED_BW_MASK;
	val >>= DP_STATUS_ALLOCATED_BW_SHIFT;

	return val * granularity;
}

static int __usb4_dp_port_set_cm_ack(struct tb_port *port, bool ack)
{
	u32 val;
	int ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	if (ack)
		val |= ADP_DP_CS_2_CA;
	else
		val &= ~ADP_DP_CS_2_CA;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_2, 1);
}

static inline int usb4_dp_port_set_cm_ack(struct tb_port *port)
{
	return __usb4_dp_port_set_cm_ack(port, true);
}

static int usb4_dp_port_wait_and_clear_cm_ack(struct tb_port *port,
					      int timeout_msec)
{
	ktime_t end;
	u32 val;
	int ret;

	ret = __usb4_dp_port_set_cm_ack(port, false);
	if (ret)
		return ret;

	end = ktime_add_ms(ktime_get(), timeout_msec);
	do {
		ret = tb_port_read(port, &val, TB_CFG_PORT,
				   port->cap_adap + ADP_DP_CS_8, 1);
		if (ret)
			return ret;

		if (!(val & ADP_DP_CS_8_DR))
			break;

		usleep_range(50, 100);
	} while (ktime_before(ktime_get(), end));

	if (val & ADP_DP_CS_8_DR) {
		tb_port_warn(port, "timeout waiting for DPTX request to clear\n");
		return -ETIMEDOUT;
	}

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_2, 1);
	if (ret)
		return ret;

	val &= ~ADP_DP_CS_2_CA;
	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_DP_CS_2, 1);
}

/**
 * usb4_dp_port_allocate_bandwidth() - Set allocated bandwidth
 * @port: DP IN adapter
 * @bw: New allocated bandwidth in Mb/s
 *
 * Communicates the new allocated bandwidth with the DPCD (graphics
 * driver). Takes into account the programmed granularity. Returns %0 in
 * success and negative errno in case of error.
 */
int usb4_dp_port_allocate_bandwidth(struct tb_port *port, int bw)
{
	u32 val, granularity;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = usb4_dp_port_granularity(port);
	if (ret < 0)
		return ret;
	granularity = ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + DP_STATUS, 1);
	if (ret)
		return ret;

	val &= ~DP_STATUS_ALLOCATED_BW_MASK;
	val |= (bw / granularity) << DP_STATUS_ALLOCATED_BW_SHIFT;

	ret = tb_port_write(port, &val, TB_CFG_PORT,
			    port->cap_adap + DP_STATUS, 1);
	if (ret)
		return ret;

	ret = usb4_dp_port_set_cm_ack(port);
	if (ret)
		return ret;

	return usb4_dp_port_wait_and_clear_cm_ack(port, 500);
}

/**
 * usb4_dp_port_requested_bandwidth() - Read requested bandwidth
 * @port: DP IN adapter
 *
 * Reads the DPCD (graphics driver) requested bandwidth and returns it
 * in Mb/s. Takes the programmed granularity into account. In case of
 * error returns negative errno. Specifically returns %-EOPNOTSUPP if
 * the adapter does not support bandwidth allocation mode, and %ENODATA
 * if there is no active bandwidth request from the graphics driver.
 */
int usb4_dp_port_requested_bandwidth(struct tb_port *port)
{
	u32 val, granularity;
	int ret;

	if (!is_usb4_dpin(port))
		return -EOPNOTSUPP;

	ret = usb4_dp_port_granularity(port);
	if (ret < 0)
		return ret;
	granularity = ret;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_DP_CS_8, 1);
	if (ret)
		return ret;

	if (!(val & ADP_DP_CS_8_DR))
		return -ENODATA;

	return (val & ADP_DP_CS_8_REQUESTED_BW_MASK) * granularity;
}

/**
 * usb4_pci_port_set_ext_encapsulation() - Enable/disable extended encapsulation
 * @port: PCIe adapter
 * @enable: Enable/disable extended encapsulation
 *
 * Enables or disables extended encapsulation used in PCIe tunneling. Caller
 * needs to make sure both adapters support this before enabling. Returns %0 on
 * success and negative errno otherwise.
 */
int usb4_pci_port_set_ext_encapsulation(struct tb_port *port, bool enable)
{
	u32 val;
	int ret;

	if (!tb_port_is_pcie_up(port) && !tb_port_is_pcie_down(port))
		return -EINVAL;

	ret = tb_port_read(port, &val, TB_CFG_PORT,
			   port->cap_adap + ADP_PCIE_CS_1, 1);
	if (ret)
		return ret;

	if (enable)
		val |= ADP_PCIE_CS_1_EE;
	else
		val &= ~ADP_PCIE_CS_1_EE;

	return tb_port_write(port, &val, TB_CFG_PORT,
			     port->cap_adap + ADP_PCIE_CS_1, 1);
}
