/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_HW_H_
#define _E1000_HW_H_

#include <linux/types.h>
#include <linux/delay.h>
#include <linux/io.h>

#include "e1000_mac.h"
#include "e1000_regs.h"
#include "e1000_defines.h"

struct e1000_hw;

#define E1000_DEV_ID_82575EB_COPPER           0x10A7
#define E1000_DEV_ID_82575EB_FIBER_SERDES     0x10A9
#define E1000_DEV_ID_82575GB_QUAD_COPPER      0x10D6

#define E1000_REVISION_2 2
#define E1000_REVISION_4 4

#define E1000_FUNC_1     1

enum e1000_mac_type {
	e1000_undefined = 0,
	e1000_82575,
	e1000_num_macs  /* List is 1-based, so subtract 1 for true count. */
};

enum e1000_media_type {
	e1000_media_type_unknown = 0,
	e1000_media_type_copper = 1,
	e1000_media_type_fiber = 2,
	e1000_media_type_internal_serdes = 3,
	e1000_num_media_types
};

enum e1000_nvm_type {
	e1000_nvm_unknown = 0,
	e1000_nvm_none,
	e1000_nvm_eeprom_spi,
	e1000_nvm_eeprom_microwire,
	e1000_nvm_flash_hw,
	e1000_nvm_flash_sw
};

enum e1000_nvm_override {
	e1000_nvm_override_none = 0,
	e1000_nvm_override_spi_small,
	e1000_nvm_override_spi_large,
	e1000_nvm_override_microwire_small,
	e1000_nvm_override_microwire_large
};

enum e1000_phy_type {
	e1000_phy_unknown = 0,
	e1000_phy_none,
	e1000_phy_m88,
	e1000_phy_igp,
	e1000_phy_igp_2,
	e1000_phy_gg82563,
	e1000_phy_igp_3,
	e1000_phy_ife,
};

enum e1000_bus_type {
	e1000_bus_type_unknown = 0,
	e1000_bus_type_pci,
	e1000_bus_type_pcix,
	e1000_bus_type_pci_express,
	e1000_bus_type_reserved
};

enum e1000_bus_speed {
	e1000_bus_speed_unknown = 0,
	e1000_bus_speed_33,
	e1000_bus_speed_66,
	e1000_bus_speed_100,
	e1000_bus_speed_120,
	e1000_bus_speed_133,
	e1000_bus_speed_2500,
	e1000_bus_speed_5000,
	e1000_bus_speed_reserved
};

enum e1000_bus_width {
	e1000_bus_width_unknown = 0,
	e1000_bus_width_pcie_x1,
	e1000_bus_width_pcie_x2,
	e1000_bus_width_pcie_x4 = 4,
	e1000_bus_width_pcie_x8 = 8,
	e1000_bus_width_32,
	e1000_bus_width_64,
	e1000_bus_width_reserved
};

enum e1000_1000t_rx_status {
	e1000_1000t_rx_status_not_ok = 0,
	e1000_1000t_rx_status_ok,
	e1000_1000t_rx_status_undefined = 0xFF
};

enum e1000_rev_polarity {
	e1000_rev_polarity_normal = 0,
	e1000_rev_polarity_reversed,
	e1000_rev_polarity_undefined = 0xFF
};

enum e1000_fc_type {
	e1000_fc_none = 0,
	e1000_fc_rx_pause,
	e1000_fc_tx_pause,
	e1000_fc_full,
	e1000_fc_default = 0xFF
};


/* Receive Descriptor */
struct e1000_rx_desc {
	__le64 buffer_addr; /* Address of the descriptor's data buffer */
	__le16 length;      /* Length of data DMAed into data buffer */
	__le16 csum;        /* Packet checksum */
	u8  status;      /* Descriptor status */
	u8  errors;      /* Descriptor Errors */
	__le16 special;
};

/* Receive Descriptor - Extended */
union e1000_rx_desc_extended {
	struct {
		__le64 buffer_addr;
		__le64 reserved;
	} read;
	struct {
		struct {
			__le32 mrq;              /* Multiple Rx Queues */
			union {
				__le32 rss;            /* RSS Hash */
				struct {
					__le16 ip_id;  /* IP id */
					__le16 csum;   /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error;     /* ext status/error */
			__le16 length;
			__le16 vlan;             /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

#define MAX_PS_BUFFERS 4
/* Receive Descriptor - Packet Split */
union e1000_rx_desc_packet_split {
	struct {
		/* one buffer for protocol header(s), three data buffers */
		__le64 buffer_addr[MAX_PS_BUFFERS];
	} read;
	struct {
		struct {
			__le32 mrq;              /* Multiple Rx Queues */
			union {
				__le32 rss;              /* RSS Hash */
				struct {
					__le16 ip_id;    /* IP id */
					__le16 csum;     /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error;     /* ext status/error */
			__le16 length0;          /* length of buffer 0 */
			__le16 vlan;             /* VLAN tag */
		} middle;
		struct {
			__le16 header_status;
			__le16 length[3];        /* length of buffers 1-3 */
		} upper;
		__le64 reserved;
	} wb; /* writeback */
};

/* Transmit Descriptor */
struct e1000_tx_desc {
	__le64 buffer_addr;      /* Address of the descriptor's data buffer */
	union {
		__le32 data;
		struct {
			__le16 length;    /* Data buffer length */
			u8 cso;        /* Checksum offset */
			u8 cmd;        /* Descriptor control */
		} flags;
	} lower;
	union {
		__le32 data;
		struct {
			u8 status;     /* Descriptor status */
			u8 css;        /* Checksum start */
			__le16 special;
		} fields;
	} upper;
};

/* Offload Context Descriptor */
struct e1000_context_desc {
	union {
		__le32 ip_config;
		struct {
			u8 ipcss;      /* IP checksum start */
			u8 ipcso;      /* IP checksum offset */
			__le16 ipcse;     /* IP checksum end */
		} ip_fields;
	} lower_setup;
	union {
		__le32 tcp_config;
		struct {
			u8 tucss;      /* TCP checksum start */
			u8 tucso;      /* TCP checksum offset */
			__le16 tucse;     /* TCP checksum end */
		} tcp_fields;
	} upper_setup;
	__le32 cmd_and_length;
	union {
		__le32 data;
		struct {
			u8 status;     /* Descriptor status */
			u8 hdr_len;    /* Header length */
			__le16 mss;       /* Maximum segment size */
		} fields;
	} tcp_seg_setup;
};

/* Offload data descriptor */
struct e1000_data_desc {
	__le64 buffer_addr;   /* Address of the descriptor's buffer address */
	union {
		__le32 data;
		struct {
			__le16 length;    /* Data buffer length */
			u8 typ_len_ext;
			u8 cmd;
		} flags;
	} lower;
	union {
		__le32 data;
		struct {
			u8 status;     /* Descriptor status */
			u8 popts;      /* Packet Options */
			__le16 special;
		} fields;
	} upper;
};

/* Statistics counters collected by the MAC */
struct e1000_hw_stats {
	u64 crcerrs;
	u64 algnerrc;
	u64 symerrs;
	u64 rxerrc;
	u64 mpc;
	u64 scc;
	u64 ecol;
	u64 mcc;
	u64 latecol;
	u64 colc;
	u64 dc;
	u64 tncrs;
	u64 sec;
	u64 cexterr;
	u64 rlec;
	u64 xonrxc;
	u64 xontxc;
	u64 xoffrxc;
	u64 xofftxc;
	u64 fcruc;
	u64 prc64;
	u64 prc127;
	u64 prc255;
	u64 prc511;
	u64 prc1023;
	u64 prc1522;
	u64 gprc;
	u64 bprc;
	u64 mprc;
	u64 gptc;
	u64 gorc;
	u64 gotc;
	u64 rnbc;
	u64 ruc;
	u64 rfc;
	u64 roc;
	u64 rjc;
	u64 mgprc;
	u64 mgpdc;
	u64 mgptc;
	u64 tor;
	u64 tot;
	u64 tpr;
	u64 tpt;
	u64 ptc64;
	u64 ptc127;
	u64 ptc255;
	u64 ptc511;
	u64 ptc1023;
	u64 ptc1522;
	u64 mptc;
	u64 bptc;
	u64 tsctc;
	u64 tsctfc;
	u64 iac;
	u64 icrxptc;
	u64 icrxatc;
	u64 ictxptc;
	u64 ictxatc;
	u64 ictxqec;
	u64 ictxqmtc;
	u64 icrxdmtc;
	u64 icrxoc;
	u64 cbtmpc;
	u64 htdpmc;
	u64 cbrdpc;
	u64 cbrmpc;
	u64 rpthc;
	u64 hgptc;
	u64 htcbdpc;
	u64 hgorc;
	u64 hgotc;
	u64 lenerrs;
	u64 scvpc;
	u64 hrmpc;
};

struct e1000_phy_stats {
	u32 idle_errors;
	u32 receive_errors;
};

struct e1000_host_mng_dhcp_cookie {
	u32 signature;
	u8  status;
	u8  reserved0;
	u16 vlan_id;
	u32 reserved1;
	u16 reserved2;
	u8  reserved3;
	u8  checksum;
};

/* Host Interface "Rev 1" */
struct e1000_host_command_header {
	u8 command_id;
	u8 command_length;
	u8 command_options;
	u8 checksum;
};

#define E1000_HI_MAX_DATA_LENGTH     252
struct e1000_host_command_info {
	struct e1000_host_command_header command_header;
	u8 command_data[E1000_HI_MAX_DATA_LENGTH];
};

/* Host Interface "Rev 2" */
struct e1000_host_mng_command_header {
	u8  command_id;
	u8  checksum;
	u16 reserved1;
	u16 reserved2;
	u16 command_length;
};

#define E1000_HI_MAX_MNG_DATA_LENGTH 0x6F8
struct e1000_host_mng_command_info {
	struct e1000_host_mng_command_header command_header;
	u8 command_data[E1000_HI_MAX_MNG_DATA_LENGTH];
};

#include "e1000_mac.h"
#include "e1000_phy.h"
#include "e1000_nvm.h"

struct e1000_mac_operations {
	s32  (*check_for_link)(struct e1000_hw *);
	s32  (*reset_hw)(struct e1000_hw *);
	s32  (*init_hw)(struct e1000_hw *);
	s32  (*setup_physical_interface)(struct e1000_hw *);
	void (*rar_set)(struct e1000_hw *, u8 *, u32);
	s32  (*read_mac_addr)(struct e1000_hw *);
	s32  (*get_speed_and_duplex)(struct e1000_hw *, u16 *, u16 *);
};

struct e1000_phy_operations {
	s32  (*acquire_phy)(struct e1000_hw *);
	s32  (*force_speed_duplex)(struct e1000_hw *);
	s32  (*get_cfg_done)(struct e1000_hw *hw);
	s32  (*get_cable_length)(struct e1000_hw *);
	s32  (*get_phy_info)(struct e1000_hw *);
	s32  (*read_phy_reg)(struct e1000_hw *, u32, u16 *);
	void (*release_phy)(struct e1000_hw *);
	s32  (*reset_phy)(struct e1000_hw *);
	s32  (*set_d0_lplu_state)(struct e1000_hw *, bool);
	s32  (*set_d3_lplu_state)(struct e1000_hw *, bool);
	s32  (*write_phy_reg)(struct e1000_hw *, u32, u16);
};

struct e1000_nvm_operations {
	s32  (*acquire_nvm)(struct e1000_hw *);
	s32  (*read_nvm)(struct e1000_hw *, u16, u16, u16 *);
	void (*release_nvm)(struct e1000_hw *);
	s32  (*write_nvm)(struct e1000_hw *, u16, u16, u16 *);
};

struct e1000_info {
	s32 (*get_invariants)(struct e1000_hw *);
	struct e1000_mac_operations *mac_ops;
	struct e1000_phy_operations *phy_ops;
	struct e1000_nvm_operations *nvm_ops;
};

extern const struct e1000_info e1000_82575_info;

struct e1000_mac_info {
	struct e1000_mac_operations ops;

	u8 addr[6];
	u8 perm_addr[6];

	enum e1000_mac_type type;

	u32 collision_delta;
	u32 ledctl_default;
	u32 ledctl_mode1;
	u32 ledctl_mode2;
	u32 mc_filter_type;
	u32 tx_packet_delta;
	u32 txcw;

	u16 current_ifs_val;
	u16 ifs_max_val;
	u16 ifs_min_val;
	u16 ifs_ratio;
	u16 ifs_step_size;
	u16 mta_reg_count;
	u16 rar_entry_count;

	u8  forced_speed_duplex;

	bool adaptive_ifs;
	bool arc_subsystem_valid;
	bool asf_firmware_present;
	bool autoneg;
	bool autoneg_failed;
	bool disable_av;
	bool disable_hw_init_bits;
	bool get_link_status;
	bool ifs_params_forced;
	bool in_ifs_mode;
	bool report_tx_early;
	bool serdes_has_link;
	bool tx_pkt_filtering;
};

struct e1000_phy_info {
	struct e1000_phy_operations ops;

	enum e1000_phy_type type;

	enum e1000_1000t_rx_status local_rx;
	enum e1000_1000t_rx_status remote_rx;
	enum e1000_ms_type ms_type;
	enum e1000_ms_type original_ms_type;
	enum e1000_rev_polarity cable_polarity;
	enum e1000_smart_speed smart_speed;

	u32 addr;
	u32 id;
	u32 reset_delay_us; /* in usec */
	u32 revision;

	enum e1000_media_type media_type;

	u16 autoneg_advertised;
	u16 autoneg_mask;
	u16 cable_length;
	u16 max_cable_length;
	u16 min_cable_length;

	u8 mdix;

	bool disable_polarity_correction;
	bool is_mdix;
	bool polarity_correction;
	bool reset_disable;
	bool speed_downgraded;
	bool autoneg_wait_to_complete;
};

struct e1000_nvm_info {
	struct e1000_nvm_operations ops;

	enum e1000_nvm_type type;
	enum e1000_nvm_override override;

	u32 flash_bank_size;
	u32 flash_base_addr;

	u16 word_size;
	u16 delay_usec;
	u16 address_bits;
	u16 opcode_bits;
	u16 page_size;
};

struct e1000_bus_info {
	enum e1000_bus_type type;
	enum e1000_bus_speed speed;
	enum e1000_bus_width width;

	u32 snoop;

	u16 func;
	u16 pci_cmd_word;
};

struct e1000_fc_info {
	u32 high_water;     /* Flow control high-water mark */
	u32 low_water;      /* Flow control low-water mark */
	u16 pause_time;     /* Flow control pause timer */
	bool send_xon;      /* Flow control send XON */
	bool strict_ieee;   /* Strict IEEE mode */
	enum e1000_fc_type type; /* Type of flow control */
	enum e1000_fc_type original_type;
};

struct e1000_hw {
	void *back;
	void *dev_spec;

	u8 __iomem *hw_addr;
	u8 __iomem *flash_address;
	unsigned long io_base;

	struct e1000_mac_info  mac;
	struct e1000_fc_info   fc;
	struct e1000_phy_info  phy;
	struct e1000_nvm_info  nvm;
	struct e1000_bus_info  bus;
	struct e1000_host_mng_dhcp_cookie mng_cookie;

	u32 dev_spec_size;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8  revision_id;
};

#ifdef DEBUG
extern char *igb_get_hw_dev_name(struct e1000_hw *hw);
#define hw_dbg(hw, format, arg...) \
	printk(KERN_DEBUG "%s: " format, igb_get_hw_dev_name(hw), ##arg)
#else
static inline int __attribute__ ((format (printf, 2, 3)))
hw_dbg(struct e1000_hw *hw, const char *format, ...)
{
	return 0;
}
#endif

#endif
