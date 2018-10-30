/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Thunderbolt driver - Port/Switch config area registers
 *
 * Every thunderbolt device consists (logically) of a switch with multiple
 * ports. Every port contains up to four config regions (HOPS, PORT, SWITCH,
 * COUNTERS) which are used to configure the device.
 *
 * Copyright (c) 2014 Andreas Noever <andreas.noever@gmail.com>
 * Copyright (C) 2018, Intel Corporation
 */

#ifndef _TB_REGS
#define _TB_REGS

#include <linux/types.h>


#define TB_ROUTE_SHIFT 8  /* number of bits in a port entry of a route */


/*
 * TODO: should be 63? But we do not know how to receive frames larger than 256
 * bytes at the frame level. (header + checksum = 16, 60*4 = 240)
 */
#define TB_MAX_CONFIG_RW_LENGTH 60

enum tb_switch_cap {
	TB_SWITCH_CAP_VSE		= 0x05,
};

enum tb_switch_vse_cap {
	TB_VSE_CAP_PLUG_EVENTS		= 0x01, /* also EEPROM */
	TB_VSE_CAP_TIME2		= 0x03,
	TB_VSE_CAP_IECS			= 0x04,
	TB_VSE_CAP_LINK_CONTROLLER	= 0x06, /* also IECS */
};

enum tb_port_cap {
	TB_PORT_CAP_PHY			= 0x01,
	TB_PORT_CAP_TIME1		= 0x03,
	TB_PORT_CAP_ADAP		= 0x04,
	TB_PORT_CAP_VSE			= 0x05,
};

enum tb_port_state {
	TB_PORT_DISABLED	= 0, /* tb_cap_phy.disable == 1 */
	TB_PORT_CONNECTING	= 1, /* retry */
	TB_PORT_UP		= 2,
	TB_PORT_UNPLUGGED	= 7,
};

/* capability headers */

struct tb_cap_basic {
	u8 next;
	/* enum tb_cap cap:8; prevent "narrower than values of its type" */
	u8 cap; /* if cap == 0x05 then we have a extended capability */
} __packed;

/**
 * struct tb_cap_extended_short - Switch extended short capability
 * @next: Pointer to the next capability. If @next and @length are zero
 *	  then we have a long cap.
 * @cap: Base capability ID (see &enum tb_switch_cap)
 * @vsec_id: Vendor specific capability ID (see &enum switch_vse_cap)
 * @length: Length of this capability
 */
struct tb_cap_extended_short {
	u8 next;
	u8 cap;
	u8 vsec_id;
	u8 length;
} __packed;

/**
 * struct tb_cap_extended_long - Switch extended long capability
 * @zero1: This field should be zero
 * @cap: Base capability ID (see &enum tb_switch_cap)
 * @vsec_id: Vendor specific capability ID (see &enum switch_vse_cap)
 * @zero2: This field should be zero
 * @next: Pointer to the next capability
 * @length: Length of this capability
 */
struct tb_cap_extended_long {
	u8 zero1;
	u8 cap;
	u8 vsec_id;
	u8 zero2;
	u16 next;
	u16 length;
} __packed;

/* capabilities */

struct tb_cap_link_controller {
	struct tb_cap_extended_long cap_header;
	u32 count:4; /* number of link controllers */
	u32 unknown1:4;
	u32 base_offset:8; /*
			    * offset (into this capability) of the configuration
			    * area of the first link controller
			    */
	u32 length:12; /* link controller configuration area length */
	u32 unknown2:4; /* TODO check that length is correct */
} __packed;

struct tb_cap_phy {
	struct tb_cap_basic cap_header;
	u32 unknown1:16;
	u32 unknown2:14;
	bool disable:1;
	u32 unknown3:11;
	enum tb_port_state state:4;
	u32 unknown4:2;
} __packed;

struct tb_eeprom_ctl {
	bool clock:1; /* send pulse to transfer one bit */
	bool access_low:1; /* set to 0 before access */
	bool data_out:1; /* to eeprom */
	bool data_in:1; /* from eeprom */
	bool access_high:1; /* set to 1 before access */
	bool not_present:1; /* should be 0 */
	bool unknown1:1;
	bool present:1; /* should be 1 */
	u32 unknown2:24;
} __packed;

struct tb_cap_plug_events {
	struct tb_cap_extended_short cap_header;
	u32 __unknown1:2;
	u32 plug_events:5;
	u32 __unknown2:25;
	u32 __unknown3;
	u32 __unknown4;
	struct tb_eeprom_ctl eeprom_ctl;
	u32 __unknown5[7];
	u32 drom_offset; /* 32 bit register, but eeprom addresses are 16 bit */
} __packed;

/* device headers */

/* Present on port 0 in TB_CFG_SWITCH at address zero. */
struct tb_regs_switch_header {
	/* DWORD 0 */
	u16 vendor_id;
	u16 device_id;
	/* DWORD 1 */
	u32 first_cap_offset:8;
	u32 upstream_port_number:6;
	u32 max_port_number:6;
	u32 depth:3;
	u32 __unknown1:1;
	u32 revision:8;
	/* DWORD 2 */
	u32 route_lo;
	/* DWORD 3 */
	u32 route_hi:31;
	bool enabled:1;
	/* DWORD 4 */
	u32 plug_events_delay:8; /*
				  * RW, pause between plug events in
				  * milliseconds. Writing 0x00 is interpreted
				  * as 255ms.
				  */
	u32 __unknown4:16;
	u32 thunderbolt_version:8;
} __packed;

enum tb_port_type {
	TB_TYPE_INACTIVE	= 0x000000,
	TB_TYPE_PORT		= 0x000001,
	TB_TYPE_NHI		= 0x000002,
	/* TB_TYPE_ETHERNET	= 0x020000, lower order bits are not known */
	/* TB_TYPE_SATA		= 0x080000, lower order bits are not known */
	TB_TYPE_DP_HDMI_IN	= 0x0e0101,
	TB_TYPE_DP_HDMI_OUT	= 0x0e0102,
	TB_TYPE_PCIE_DOWN	= 0x100101,
	TB_TYPE_PCIE_UP		= 0x100102,
	/* TB_TYPE_USB		= 0x200000, lower order bits are not known */
};

/* Present on every port in TB_CF_PORT at address zero. */
struct tb_regs_port_header {
	/* DWORD 0 */
	u16 vendor_id;
	u16 device_id;
	/* DWORD 1 */
	u32 first_cap_offset:8;
	u32 max_counters:11;
	u32 __unknown1:5;
	u32 revision:8;
	/* DWORD 2 */
	enum tb_port_type type:24;
	u32 thunderbolt_version:8;
	/* DWORD 3 */
	u32 __unknown2:20;
	u32 port_number:6;
	u32 __unknown3:6;
	/* DWORD 4 */
	u32 nfc_credits;
	/* DWORD 5 */
	u32 max_in_hop_id:11;
	u32 max_out_hop_id:11;
	u32 __unknown4:10;
	/* DWORD 6 */
	u32 __unknown5;
	/* DWORD 7 */
	u32 __unknown6;

} __packed;

/* Hop register from TB_CFG_HOPS. 8 byte per entry. */
struct tb_regs_hop {
	/* DWORD 0 */
	u32 next_hop:11; /*
			  * hop to take after sending the packet through
			  * out_port (on the incoming port of the next switch)
			  */
	u32 out_port:6; /* next port of the path (on the same switch) */
	u32 initial_credits:8;
	u32 unknown1:6; /* set to zero */
	bool enable:1;

	/* DWORD 1 */
	u32 weight:4;
	u32 unknown2:4; /* set to zero */
	u32 priority:3;
	bool drop_packages:1;
	u32 counter:11; /* index into TB_CFG_COUNTERS on this port */
	bool counter_enable:1;
	bool ingress_fc:1;
	bool egress_fc:1;
	bool ingress_shared_buffer:1;
	bool egress_shared_buffer:1;
	u32 unknown3:4; /* set to zero */
} __packed;


#endif
