/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

#ifndef NSP_NSP_H
#define NSP_NSP_H 1

#include <linux/types.h>
#include <linux/if_ether.h>

struct firmware;
struct nfp_cpp;
struct nfp_nsp;

struct nfp_nsp *nfp_nsp_open(struct nfp_cpp *cpp);
void nfp_nsp_close(struct nfp_nsp *state);
u16 nfp_nsp_get_abi_ver_major(struct nfp_nsp *state);
u16 nfp_nsp_get_abi_ver_minor(struct nfp_nsp *state);
int nfp_nsp_wait(struct nfp_nsp *state);
int nfp_nsp_device_soft_reset(struct nfp_nsp *state);
int nfp_nsp_load_fw(struct nfp_nsp *state, const struct firmware *fw);
int nfp_nsp_write_flash(struct nfp_nsp *state, const struct firmware *fw);
int nfp_nsp_mac_reinit(struct nfp_nsp *state);
int nfp_nsp_load_stored_fw(struct nfp_nsp *state);
int nfp_nsp_hwinfo_lookup(struct nfp_nsp *state, void *buf, unsigned int size);
int nfp_nsp_hwinfo_lookup_optional(struct nfp_nsp *state, void *buf,
				   unsigned int size, const char *default_val);
int nfp_nsp_hwinfo_set(struct nfp_nsp *state, void *buf, unsigned int size);
int nfp_nsp_fw_loaded(struct nfp_nsp *state);
int nfp_nsp_read_module_eeprom(struct nfp_nsp *state, int eth_index,
			       unsigned int offset, void *data,
			       unsigned int len, unsigned int *read_len);

static inline bool nfp_nsp_has_mac_reinit(struct nfp_nsp *state)
{
	return nfp_nsp_get_abi_ver_minor(state) > 20;
}

static inline bool nfp_nsp_has_stored_fw_load(struct nfp_nsp *state)
{
	return nfp_nsp_get_abi_ver_minor(state) > 23;
}

static inline bool nfp_nsp_has_hwinfo_lookup(struct nfp_nsp *state)
{
	return nfp_nsp_get_abi_ver_minor(state) > 24;
}

static inline bool nfp_nsp_has_hwinfo_set(struct nfp_nsp *state)
{
	return nfp_nsp_get_abi_ver_minor(state) > 25;
}

static inline bool nfp_nsp_has_fw_loaded(struct nfp_nsp *state)
{
	return nfp_nsp_get_abi_ver_minor(state) > 25;
}

static inline bool nfp_nsp_has_versions(struct nfp_nsp *state)
{
	return nfp_nsp_get_abi_ver_minor(state) > 27;
}

static inline bool nfp_nsp_has_read_module_eeprom(struct nfp_nsp *state)
{
	return nfp_nsp_get_abi_ver_minor(state) > 28;
}

enum nfp_eth_interface {
	NFP_INTERFACE_NONE	= 0,
	NFP_INTERFACE_SFP	= 1,
	NFP_INTERFACE_SFPP	= 10,
	NFP_INTERFACE_SFP28	= 28,
	NFP_INTERFACE_QSFP	= 40,
	NFP_INTERFACE_RJ45	= 45,
	NFP_INTERFACE_CXP	= 100,
	NFP_INTERFACE_QSFP28	= 112,
};

enum nfp_eth_media {
	NFP_MEDIA_DAC_PASSIVE = 0,
	NFP_MEDIA_DAC_ACTIVE,
	NFP_MEDIA_FIBRE,
};

enum nfp_eth_aneg {
	NFP_ANEG_AUTO = 0,
	NFP_ANEG_SEARCH,
	NFP_ANEG_25G_CONSORTIUM,
	NFP_ANEG_25G_IEEE,
	NFP_ANEG_DISABLED,
};

enum nfp_eth_fec {
	NFP_FEC_AUTO_BIT = 0,
	NFP_FEC_BASER_BIT,
	NFP_FEC_REED_SOLOMON_BIT,
	NFP_FEC_DISABLED_BIT,
};

#define NFP_FEC_AUTO		BIT(NFP_FEC_AUTO_BIT)
#define NFP_FEC_BASER		BIT(NFP_FEC_BASER_BIT)
#define NFP_FEC_REED_SOLOMON	BIT(NFP_FEC_REED_SOLOMON_BIT)
#define NFP_FEC_DISABLED	BIT(NFP_FEC_DISABLED_BIT)

/* Defines the valid values of the 'abi_drv_reset' hwinfo key */
#define NFP_NSP_DRV_RESET_DISK			0
#define NFP_NSP_DRV_RESET_ALWAYS		1
#define NFP_NSP_DRV_RESET_NEVER			2
#define NFP_NSP_DRV_RESET_DEFAULT		"0"

/* Defines the valid values of the 'app_fw_from_flash' hwinfo key */
#define NFP_NSP_APP_FW_LOAD_DISK		0
#define NFP_NSP_APP_FW_LOAD_FLASH		1
#define NFP_NSP_APP_FW_LOAD_PREF		2
#define NFP_NSP_APP_FW_LOAD_DEFAULT		"2"

/* Define the default value for the 'abi_drv_load_ifc' key */
#define NFP_NSP_DRV_LOAD_IFC_DEFAULT		"0x10ff"

/**
 * struct nfp_eth_table - ETH table information
 * @count:	number of table entries
 * @max_index:	max of @index fields of all @ports
 * @ports:	table of ports
 *
 * @ports.eth_index:	port index according to legacy ethX numbering
 * @ports.index:	chip-wide first channel index
 * @ports.nbi:		NBI index
 * @ports.base:		first channel index (within NBI)
 * @ports.lanes:	number of channels
 * @ports.speed:	interface speed (in Mbps)
 * @ports.interface:	interface (module) plugged in
 * @ports.media:	media type of the @interface
 * @ports.fec:		forward error correction mode
 * @ports.aneg:		auto negotiation mode
 * @ports.mac_addr:	interface MAC address
 * @ports.label_port:	port id
 * @ports.label_subport:  id of interface within port (for split ports)
 * @ports.enabled:	is enabled?
 * @ports.tx_enabled:	is TX enabled?
 * @ports.rx_enabled:	is RX enabled?
 * @ports.override_changed: is media reconfig pending?
 *
 * @ports.port_type:	one of %PORT_* defines for ethtool
 * @ports.port_lanes:	total number of lanes on the port (sum of lanes of all
 *			subports)
 * @ports.is_split:	is interface part of a split port
 * @ports.fec_modes_supported:	bitmap of FEC modes supported
 */
struct nfp_eth_table {
	unsigned int count;
	unsigned int max_index;
	struct nfp_eth_table_port {
		unsigned int eth_index;
		unsigned int index;
		unsigned int nbi;
		unsigned int base;
		unsigned int lanes;
		unsigned int speed;

		unsigned int interface;
		enum nfp_eth_media media;

		enum nfp_eth_fec fec;
		enum nfp_eth_aneg aneg;

		u8 mac_addr[ETH_ALEN];

		u8 label_port;
		u8 label_subport;

		bool enabled;
		bool tx_enabled;
		bool rx_enabled;

		bool override_changed;

		/* Computed fields */
		u8 port_type;

		unsigned int port_lanes;

		bool is_split;

		unsigned int fec_modes_supported;
	} ports[];
};

struct nfp_eth_table *nfp_eth_read_ports(struct nfp_cpp *cpp);
struct nfp_eth_table *
__nfp_eth_read_ports(struct nfp_cpp *cpp, struct nfp_nsp *nsp);

int nfp_eth_set_mod_enable(struct nfp_cpp *cpp, unsigned int idx, bool enable);
int nfp_eth_set_configured(struct nfp_cpp *cpp, unsigned int idx,
			   bool configed);
int
nfp_eth_set_fec(struct nfp_cpp *cpp, unsigned int idx, enum nfp_eth_fec mode);

int nfp_eth_set_idmode(struct nfp_cpp *cpp, unsigned int idx, bool state);

static inline bool nfp_eth_can_support_fec(struct nfp_eth_table_port *eth_port)
{
	return !!eth_port->fec_modes_supported;
}

static inline unsigned int
nfp_eth_supported_fec_modes(struct nfp_eth_table_port *eth_port)
{
	return eth_port->fec_modes_supported;
}

struct nfp_nsp *nfp_eth_config_start(struct nfp_cpp *cpp, unsigned int idx);
int nfp_eth_config_commit_end(struct nfp_nsp *nsp);
void nfp_eth_config_cleanup_end(struct nfp_nsp *nsp);

int __nfp_eth_set_aneg(struct nfp_nsp *nsp, enum nfp_eth_aneg mode);
int __nfp_eth_set_speed(struct nfp_nsp *nsp, unsigned int speed);
int __nfp_eth_set_split(struct nfp_nsp *nsp, unsigned int lanes);

/**
 * struct nfp_nsp_identify - NSP static information
 * @version:      opaque version string
 * @flags:        version flags
 * @br_primary:   branch id of primary bootloader
 * @br_secondary: branch id of secondary bootloader
 * @br_nsp:       branch id of NSP
 * @primary:      version of primarary bootloader
 * @secondary:    version id of secondary bootloader
 * @nsp:          version id of NSP
 * @sensor_mask:  mask of present sensors available on NIC
 */
struct nfp_nsp_identify {
	char version[40];
	u8 flags;
	u8 br_primary;
	u8 br_secondary;
	u8 br_nsp;
	u16 primary;
	u16 secondary;
	u16 nsp;
	u64 sensor_mask;
};

struct nfp_nsp_identify *__nfp_nsp_identify(struct nfp_nsp *nsp);

enum nfp_nsp_sensor_id {
	NFP_SENSOR_CHIP_TEMPERATURE,
	NFP_SENSOR_ASSEMBLY_POWER,
	NFP_SENSOR_ASSEMBLY_12V_POWER,
	NFP_SENSOR_ASSEMBLY_3V3_POWER,
};

int nfp_hwmon_read_sensor(struct nfp_cpp *cpp, enum nfp_nsp_sensor_id id,
			  long *val);

#define NFP_NSP_VERSION_BUFSZ	1024 /* reasonable size, not in the ABI */

enum nfp_nsp_versions {
	NFP_VERSIONS_BSP,
	NFP_VERSIONS_CPLD,
	NFP_VERSIONS_APP,
	NFP_VERSIONS_BUNDLE,
	NFP_VERSIONS_UNDI,
	NFP_VERSIONS_NCSI,
	NFP_VERSIONS_CFGR,
};

int nfp_nsp_versions(struct nfp_nsp *state, void *buf, unsigned int size);
const char *nfp_nsp_versions_get(enum nfp_nsp_versions id, bool flash,
				 const u8 *buf, unsigned int size);
#endif
