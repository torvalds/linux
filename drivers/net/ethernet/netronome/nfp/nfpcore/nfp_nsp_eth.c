// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2017 Netronome Systems, Inc. */

/* Authors: David Brunecz <david.brunecz@netronome.com>
 *          Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason Mcmullan <jason.mcmullan@netronome.com>
 */

#include <linux/bitfield.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "nfp.h"
#include "nfp_nsp.h"
#include "nfp6000/nfp6000.h"

#define NSP_ETH_NBI_PORT_COUNT		24
#define NSP_ETH_MAX_COUNT		(2 * NSP_ETH_NBI_PORT_COUNT)
#define NSP_ETH_TABLE_SIZE		(NSP_ETH_MAX_COUNT *		\
					 sizeof(union eth_table_entry))

#define NSP_ETH_PORT_LANES		GENMASK_ULL(3, 0)
#define NSP_ETH_PORT_INDEX		GENMASK_ULL(15, 8)
#define NSP_ETH_PORT_LABEL		GENMASK_ULL(53, 48)
#define NSP_ETH_PORT_PHYLABEL		GENMASK_ULL(59, 54)
#define NSP_ETH_PORT_FEC_SUPP_BASER	BIT_ULL(60)
#define NSP_ETH_PORT_FEC_SUPP_RS	BIT_ULL(61)
#define NSP_ETH_PORT_SUPP_ANEG		BIT_ULL(63)

#define NSP_ETH_PORT_LANES_MASK		cpu_to_le64(NSP_ETH_PORT_LANES)

#define NSP_ETH_STATE_CONFIGURED	BIT_ULL(0)
#define NSP_ETH_STATE_ENABLED		BIT_ULL(1)
#define NSP_ETH_STATE_TX_ENABLED	BIT_ULL(2)
#define NSP_ETH_STATE_RX_ENABLED	BIT_ULL(3)
#define NSP_ETH_STATE_RATE		GENMASK_ULL(11, 8)
#define NSP_ETH_STATE_INTERFACE		GENMASK_ULL(19, 12)
#define NSP_ETH_STATE_MEDIA		GENMASK_ULL(21, 20)
#define NSP_ETH_STATE_OVRD_CHNG		BIT_ULL(22)
#define NSP_ETH_STATE_ANEG		GENMASK_ULL(25, 23)
#define NSP_ETH_STATE_FEC		GENMASK_ULL(27, 26)
#define NSP_ETH_STATE_ACT_FEC		GENMASK_ULL(29, 28)

#define NSP_ETH_CTRL_CONFIGURED		BIT_ULL(0)
#define NSP_ETH_CTRL_ENABLED		BIT_ULL(1)
#define NSP_ETH_CTRL_TX_ENABLED		BIT_ULL(2)
#define NSP_ETH_CTRL_RX_ENABLED		BIT_ULL(3)
#define NSP_ETH_CTRL_SET_RATE		BIT_ULL(4)
#define NSP_ETH_CTRL_SET_LANES		BIT_ULL(5)
#define NSP_ETH_CTRL_SET_ANEG		BIT_ULL(6)
#define NSP_ETH_CTRL_SET_FEC		BIT_ULL(7)
#define NSP_ETH_CTRL_SET_IDMODE		BIT_ULL(8)

enum nfp_eth_raw {
	NSP_ETH_RAW_PORT = 0,
	NSP_ETH_RAW_STATE,
	NSP_ETH_RAW_MAC,
	NSP_ETH_RAW_CONTROL,

	NSP_ETH_NUM_RAW
};

enum nfp_eth_rate {
	RATE_INVALID = 0,
	RATE_10M,
	RATE_100M,
	RATE_1G,
	RATE_10G,
	RATE_25G,
};

union eth_table_entry {
	struct {
		__le64 port;
		__le64 state;
		u8 mac_addr[6];
		u8 resv[2];
		__le64 control;
	};
	__le64 raw[NSP_ETH_NUM_RAW];
};

static const struct {
	enum nfp_eth_rate rate;
	unsigned int speed;
} nsp_eth_rate_tbl[] = {
	{ RATE_INVALID,	0, },
	{ RATE_10M,	SPEED_10, },
	{ RATE_100M,	SPEED_100, },
	{ RATE_1G,	SPEED_1000, },
	{ RATE_10G,	SPEED_10000, },
	{ RATE_25G,	SPEED_25000, },
};

static unsigned int nfp_eth_rate2speed(enum nfp_eth_rate rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nsp_eth_rate_tbl); i++)
		if (nsp_eth_rate_tbl[i].rate == rate)
			return nsp_eth_rate_tbl[i].speed;

	return 0;
}

static unsigned int nfp_eth_speed2rate(unsigned int speed)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(nsp_eth_rate_tbl); i++)
		if (nsp_eth_rate_tbl[i].speed == speed)
			return nsp_eth_rate_tbl[i].rate;

	return RATE_INVALID;
}

static void nfp_eth_copy_mac_reverse(u8 *dst, const u8 *src)
{
	int i;

	for (i = 0; i < ETH_ALEN; i++)
		dst[ETH_ALEN - i - 1] = src[i];
}

static void
nfp_eth_port_translate(struct nfp_nsp *nsp, const union eth_table_entry *src,
		       unsigned int index, struct nfp_eth_table_port *dst)
{
	unsigned int rate;
	unsigned int fec;
	u64 port, state;

	port = le64_to_cpu(src->port);
	state = le64_to_cpu(src->state);

	dst->eth_index = FIELD_GET(NSP_ETH_PORT_INDEX, port);
	dst->index = index;
	dst->nbi = index / NSP_ETH_NBI_PORT_COUNT;
	dst->base = index % NSP_ETH_NBI_PORT_COUNT;
	dst->lanes = FIELD_GET(NSP_ETH_PORT_LANES, port);

	dst->enabled = FIELD_GET(NSP_ETH_STATE_ENABLED, state);
	dst->tx_enabled = FIELD_GET(NSP_ETH_STATE_TX_ENABLED, state);
	dst->rx_enabled = FIELD_GET(NSP_ETH_STATE_RX_ENABLED, state);

	rate = nfp_eth_rate2speed(FIELD_GET(NSP_ETH_STATE_RATE, state));
	dst->speed = dst->lanes * rate;

	dst->interface = FIELD_GET(NSP_ETH_STATE_INTERFACE, state);
	dst->media = FIELD_GET(NSP_ETH_STATE_MEDIA, state);

	nfp_eth_copy_mac_reverse(dst->mac_addr, src->mac_addr);

	dst->label_port = FIELD_GET(NSP_ETH_PORT_PHYLABEL, port);
	dst->label_subport = FIELD_GET(NSP_ETH_PORT_LABEL, port);

	if (nfp_nsp_get_abi_ver_minor(nsp) < 17)
		return;

	dst->override_changed = FIELD_GET(NSP_ETH_STATE_OVRD_CHNG, state);
	dst->aneg = FIELD_GET(NSP_ETH_STATE_ANEG, state);

	if (nfp_nsp_get_abi_ver_minor(nsp) < 22)
		return;

	fec = FIELD_GET(NSP_ETH_PORT_FEC_SUPP_BASER, port);
	dst->fec_modes_supported |= fec << NFP_FEC_BASER_BIT;
	fec = FIELD_GET(NSP_ETH_PORT_FEC_SUPP_RS, port);
	dst->fec_modes_supported |= fec << NFP_FEC_REED_SOLOMON_BIT;
	if (dst->fec_modes_supported)
		dst->fec_modes_supported |= NFP_FEC_AUTO | NFP_FEC_DISABLED;

	dst->fec = FIELD_GET(NSP_ETH_STATE_FEC, state);
	dst->act_fec = dst->fec;

	if (nfp_nsp_get_abi_ver_minor(nsp) < 33)
		return;

	dst->act_fec = FIELD_GET(NSP_ETH_STATE_ACT_FEC, state);
	dst->supp_aneg = FIELD_GET(NSP_ETH_PORT_SUPP_ANEG, port);
}

static void
nfp_eth_calc_port_geometry(struct nfp_cpp *cpp, struct nfp_eth_table *table)
{
	unsigned int i, j;

	for (i = 0; i < table->count; i++) {
		table->max_index = max(table->max_index, table->ports[i].index);

		for (j = 0; j < table->count; j++) {
			if (table->ports[i].label_port !=
			    table->ports[j].label_port)
				continue;
			table->ports[i].port_lanes += table->ports[j].lanes;

			if (i == j)
				continue;
			if (table->ports[i].label_subport ==
			    table->ports[j].label_subport)
				nfp_warn(cpp,
					 "Port %d subport %d is a duplicate\n",
					 table->ports[i].label_port,
					 table->ports[i].label_subport);

			table->ports[i].is_split = true;
		}
	}
}

static void
nfp_eth_calc_port_type(struct nfp_cpp *cpp, struct nfp_eth_table_port *entry)
{
	if (entry->interface == NFP_INTERFACE_NONE) {
		entry->port_type = PORT_NONE;
		return;
	} else if (entry->interface == NFP_INTERFACE_RJ45) {
		entry->port_type = PORT_TP;
		return;
	}

	if (entry->media == NFP_MEDIA_FIBRE)
		entry->port_type = PORT_FIBRE;
	else
		entry->port_type = PORT_DA;
}

static void
nfp_eth_read_media(struct nfp_cpp *cpp, struct nfp_nsp *nsp, struct nfp_eth_table_port *entry)
{
	struct nfp_eth_media_buf ethm = {
		.eth_index = entry->eth_index,
	};
	unsigned int i;
	int ret;

	if (!nfp_nsp_has_read_media(nsp))
		return;

	ret = nfp_nsp_read_media(nsp, &ethm, sizeof(ethm));
	if (ret) {
		nfp_err(cpp, "Reading media link modes failed: %d\n", ret);
		return;
	}

	for (i = 0; i < 2; i++) {
		entry->link_modes_supp[i] = le64_to_cpu(ethm.supported_modes[i]);
		entry->link_modes_ad[i] = le64_to_cpu(ethm.advertised_modes[i]);
	}
}

/**
 * nfp_eth_read_ports() - retrieve port information
 * @cpp:	NFP CPP handle
 *
 * Read the port information from the device.  Returned structure should
 * be freed with kfree() once no longer needed.
 *
 * Return: populated ETH table or NULL on error.
 */
struct nfp_eth_table *nfp_eth_read_ports(struct nfp_cpp *cpp)
{
	struct nfp_eth_table *ret;
	struct nfp_nsp *nsp;

	nsp = nfp_nsp_open(cpp);
	if (IS_ERR(nsp))
		return NULL;

	ret = __nfp_eth_read_ports(cpp, nsp);
	nfp_nsp_close(nsp);

	return ret;
}

struct nfp_eth_table *
__nfp_eth_read_ports(struct nfp_cpp *cpp, struct nfp_nsp *nsp)
{
	union eth_table_entry *entries;
	struct nfp_eth_table *table;
	int i, j, ret, cnt = 0;

	entries = kzalloc(NSP_ETH_TABLE_SIZE, GFP_KERNEL);
	if (!entries)
		return NULL;

	ret = nfp_nsp_read_eth_table(nsp, entries, NSP_ETH_TABLE_SIZE);
	if (ret < 0) {
		nfp_err(cpp, "reading port table failed %d\n", ret);
		goto err;
	}

	for (i = 0; i < NSP_ETH_MAX_COUNT; i++)
		if (entries[i].port & NSP_ETH_PORT_LANES_MASK)
			cnt++;

	/* Some versions of flash will give us 0 instead of port count.
	 * For those that give a port count, verify it against the value
	 * calculated above.
	 */
	if (ret && ret != cnt) {
		nfp_err(cpp, "table entry count reported (%d) does not match entries present (%d)\n",
			ret, cnt);
		goto err;
	}

	table = kzalloc(struct_size(table, ports, cnt), GFP_KERNEL);
	if (!table)
		goto err;

	table->count = cnt;
	for (i = 0, j = 0; i < NSP_ETH_MAX_COUNT; i++)
		if (entries[i].port & NSP_ETH_PORT_LANES_MASK)
			nfp_eth_port_translate(nsp, &entries[i], i,
					       &table->ports[j++]);

	nfp_eth_calc_port_geometry(cpp, table);
	for (i = 0; i < table->count; i++) {
		nfp_eth_calc_port_type(cpp, &table->ports[i]);
		nfp_eth_read_media(cpp, nsp, &table->ports[i]);
	}

	kfree(entries);

	return table;

err:
	kfree(entries);
	return NULL;
}

struct nfp_nsp *nfp_eth_config_start(struct nfp_cpp *cpp, unsigned int idx)
{
	union eth_table_entry *entries;
	struct nfp_nsp *nsp;
	int ret;

	entries = kzalloc(NSP_ETH_TABLE_SIZE, GFP_KERNEL);
	if (!entries)
		return ERR_PTR(-ENOMEM);

	nsp = nfp_nsp_open(cpp);
	if (IS_ERR(nsp)) {
		kfree(entries);
		return nsp;
	}

	ret = nfp_nsp_read_eth_table(nsp, entries, NSP_ETH_TABLE_SIZE);
	if (ret < 0) {
		nfp_err(cpp, "reading port table failed %d\n", ret);
		goto err;
	}

	if (!(entries[idx].port & NSP_ETH_PORT_LANES_MASK)) {
		nfp_warn(cpp, "trying to set port state on disabled port %d\n",
			 idx);
		goto err;
	}

	nfp_nsp_config_set_state(nsp, entries, idx);
	return nsp;

err:
	nfp_nsp_close(nsp);
	kfree(entries);
	return ERR_PTR(-EIO);
}

void nfp_eth_config_cleanup_end(struct nfp_nsp *nsp)
{
	union eth_table_entry *entries = nfp_nsp_config_entries(nsp);

	nfp_nsp_config_set_modified(nsp, false);
	nfp_nsp_config_clear_state(nsp);
	nfp_nsp_close(nsp);
	kfree(entries);
}

/**
 * nfp_eth_config_commit_end() - perform recorded configuration changes
 * @nsp:	NFP NSP handle returned from nfp_eth_config_start()
 *
 * Perform the configuration which was requested with __nfp_eth_set_*()
 * helpers and recorded in @nsp state.  If device was already configured
 * as requested or no __nfp_eth_set_*() operations were made no NSP command
 * will be performed.
 *
 * Return:
 * 0 - configuration successful;
 * 1 - no changes were needed;
 * -ERRNO - configuration failed.
 */
int nfp_eth_config_commit_end(struct nfp_nsp *nsp)
{
	union eth_table_entry *entries = nfp_nsp_config_entries(nsp);
	int ret = 1;

	if (nfp_nsp_config_modified(nsp)) {
		ret = nfp_nsp_write_eth_table(nsp, entries, NSP_ETH_TABLE_SIZE);
		ret = ret < 0 ? ret : 0;
	}

	nfp_eth_config_cleanup_end(nsp);

	return ret;
}

/**
 * nfp_eth_set_mod_enable() - set PHY module enable control bit
 * @cpp:	NFP CPP handle
 * @idx:	NFP chip-wide port index
 * @enable:	Desired state
 *
 * Enable or disable PHY module (this usually means setting the TX lanes
 * disable bits).
 *
 * Return:
 * 0 - configuration successful;
 * 1 - no changes were needed;
 * -ERRNO - configuration failed.
 */
int nfp_eth_set_mod_enable(struct nfp_cpp *cpp, unsigned int idx, bool enable)
{
	union eth_table_entry *entries;
	struct nfp_nsp *nsp;
	u64 reg;

	nsp = nfp_eth_config_start(cpp, idx);
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	entries = nfp_nsp_config_entries(nsp);

	/* Check if we are already in requested state */
	reg = le64_to_cpu(entries[idx].state);
	if (enable != FIELD_GET(NSP_ETH_CTRL_ENABLED, reg)) {
		reg = le64_to_cpu(entries[idx].control);
		reg &= ~NSP_ETH_CTRL_ENABLED;
		reg |= FIELD_PREP(NSP_ETH_CTRL_ENABLED, enable);
		entries[idx].control = cpu_to_le64(reg);

		nfp_nsp_config_set_modified(nsp, true);
	}

	return nfp_eth_config_commit_end(nsp);
}

/**
 * nfp_eth_set_configured() - set PHY module configured control bit
 * @cpp:	NFP CPP handle
 * @idx:	NFP chip-wide port index
 * @configed:	Desired state
 *
 * Set the ifup/ifdown state on the PHY.
 *
 * Return:
 * 0 - configuration successful;
 * 1 - no changes were needed;
 * -ERRNO - configuration failed.
 */
int nfp_eth_set_configured(struct nfp_cpp *cpp, unsigned int idx, bool configed)
{
	union eth_table_entry *entries;
	struct nfp_nsp *nsp;
	u64 reg;

	nsp = nfp_eth_config_start(cpp, idx);
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	/* Older ABI versions did support this feature, however this has only
	 * been reliable since ABI 20.
	 */
	if (nfp_nsp_get_abi_ver_minor(nsp) < 20) {
		nfp_eth_config_cleanup_end(nsp);
		return -EOPNOTSUPP;
	}

	entries = nfp_nsp_config_entries(nsp);

	/* Check if we are already in requested state */
	reg = le64_to_cpu(entries[idx].state);
	if (configed != FIELD_GET(NSP_ETH_STATE_CONFIGURED, reg)) {
		reg = le64_to_cpu(entries[idx].control);
		reg &= ~NSP_ETH_CTRL_CONFIGURED;
		reg |= FIELD_PREP(NSP_ETH_CTRL_CONFIGURED, configed);
		entries[idx].control = cpu_to_le64(reg);

		nfp_nsp_config_set_modified(nsp, true);
	}

	return nfp_eth_config_commit_end(nsp);
}

static int
nfp_eth_set_bit_config(struct nfp_nsp *nsp, unsigned int raw_idx,
		       const u64 mask, const unsigned int shift,
		       unsigned int val, const u64 ctrl_bit)
{
	union eth_table_entry *entries = nfp_nsp_config_entries(nsp);
	unsigned int idx = nfp_nsp_config_idx(nsp);
	u64 reg;

	/* Note: set features were added in ABI 0.14 but the error
	 *	 codes were initially not populated correctly.
	 */
	if (nfp_nsp_get_abi_ver_minor(nsp) < 17) {
		nfp_err(nfp_nsp_cpp(nsp),
			"set operations not supported, please update flash\n");
		return -EOPNOTSUPP;
	}

	/* Check if we are already in requested state */
	reg = le64_to_cpu(entries[idx].raw[raw_idx]);
	if (val == (reg & mask) >> shift)
		return 0;

	reg &= ~mask;
	reg |= (val << shift) & mask;
	entries[idx].raw[raw_idx] = cpu_to_le64(reg);

	entries[idx].control |= cpu_to_le64(ctrl_bit);

	nfp_nsp_config_set_modified(nsp, true);

	return 0;
}

int nfp_eth_set_idmode(struct nfp_cpp *cpp, unsigned int idx, bool state)
{
	union eth_table_entry *entries;
	struct nfp_nsp *nsp;
	u64 reg;

	nsp = nfp_eth_config_start(cpp, idx);
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	/* Set this features were added in ABI 0.32 */
	if (nfp_nsp_get_abi_ver_minor(nsp) < 32) {
		nfp_err(nfp_nsp_cpp(nsp),
			"set id mode operation not supported, please update flash\n");
		nfp_eth_config_cleanup_end(nsp);
		return -EOPNOTSUPP;
	}

	entries = nfp_nsp_config_entries(nsp);

	reg = le64_to_cpu(entries[idx].control);
	reg &= ~NSP_ETH_CTRL_SET_IDMODE;
	reg |= FIELD_PREP(NSP_ETH_CTRL_SET_IDMODE, state);
	entries[idx].control = cpu_to_le64(reg);

	nfp_nsp_config_set_modified(nsp, true);

	return nfp_eth_config_commit_end(nsp);
}

#define NFP_ETH_SET_BIT_CONFIG(nsp, raw_idx, mask, val, ctrl_bit)	\
	({								\
		__BF_FIELD_CHECK(mask, 0ULL, val, "NFP_ETH_SET_BIT_CONFIG: "); \
		nfp_eth_set_bit_config(nsp, raw_idx, mask, __bf_shf(mask), \
				       val, ctrl_bit);			\
	})

/**
 * __nfp_eth_set_aneg() - set PHY autonegotiation control bit
 * @nsp:	NFP NSP handle returned from nfp_eth_config_start()
 * @mode:	Desired autonegotiation mode
 *
 * Allow/disallow PHY module to advertise/perform autonegotiation.
 * Will write to hwinfo overrides in the flash (persistent config).
 *
 * Return: 0 or -ERRNO.
 */
int __nfp_eth_set_aneg(struct nfp_nsp *nsp, enum nfp_eth_aneg mode)
{
	return NFP_ETH_SET_BIT_CONFIG(nsp, NSP_ETH_RAW_STATE,
				      NSP_ETH_STATE_ANEG, mode,
				      NSP_ETH_CTRL_SET_ANEG);
}

/**
 * __nfp_eth_set_fec() - set PHY forward error correction control bit
 * @nsp:	NFP NSP handle returned from nfp_eth_config_start()
 * @mode:	Desired fec mode
 *
 * Set the PHY module forward error correction mode.
 * Will write to hwinfo overrides in the flash (persistent config).
 *
 * Return: 0 or -ERRNO.
 */
static int __nfp_eth_set_fec(struct nfp_nsp *nsp, enum nfp_eth_fec mode)
{
	return NFP_ETH_SET_BIT_CONFIG(nsp, NSP_ETH_RAW_STATE,
				      NSP_ETH_STATE_FEC, mode,
				      NSP_ETH_CTRL_SET_FEC);
}

/**
 * nfp_eth_set_fec() - set PHY forward error correction control mode
 * @cpp:	NFP CPP handle
 * @idx:	NFP chip-wide port index
 * @mode:	Desired fec mode
 *
 * Return:
 * 0 - configuration successful;
 * 1 - no changes were needed;
 * -ERRNO - configuration failed.
 */
int
nfp_eth_set_fec(struct nfp_cpp *cpp, unsigned int idx, enum nfp_eth_fec mode)
{
	struct nfp_nsp *nsp;
	int err;

	nsp = nfp_eth_config_start(cpp, idx);
	if (IS_ERR(nsp))
		return PTR_ERR(nsp);

	err = __nfp_eth_set_fec(nsp, mode);
	if (err) {
		nfp_eth_config_cleanup_end(nsp);
		return err;
	}

	return nfp_eth_config_commit_end(nsp);
}

/**
 * __nfp_eth_set_speed() - set interface speed/rate
 * @nsp:	NFP NSP handle returned from nfp_eth_config_start()
 * @speed:	Desired speed (per lane)
 *
 * Set lane speed.  Provided @speed value should be subport speed divided
 * by number of lanes this subport is spanning (i.e. 10000 for 40G, 25000 for
 * 50G, etc.)
 * Will write to hwinfo overrides in the flash (persistent config).
 *
 * Return: 0 or -ERRNO.
 */
int __nfp_eth_set_speed(struct nfp_nsp *nsp, unsigned int speed)
{
	enum nfp_eth_rate rate;

	rate = nfp_eth_speed2rate(speed);
	if (rate == RATE_INVALID) {
		nfp_warn(nfp_nsp_cpp(nsp),
			 "could not find matching lane rate for speed %u\n",
			 speed);
		return -EINVAL;
	}

	return NFP_ETH_SET_BIT_CONFIG(nsp, NSP_ETH_RAW_STATE,
				      NSP_ETH_STATE_RATE, rate,
				      NSP_ETH_CTRL_SET_RATE);
}

/**
 * __nfp_eth_set_split() - set interface lane split
 * @nsp:	NFP NSP handle returned from nfp_eth_config_start()
 * @lanes:	Desired lanes per port
 *
 * Set number of lanes in the port.
 * Will write to hwinfo overrides in the flash (persistent config).
 *
 * Return: 0 or -ERRNO.
 */
int __nfp_eth_set_split(struct nfp_nsp *nsp, unsigned int lanes)
{
	return NFP_ETH_SET_BIT_CONFIG(nsp, NSP_ETH_RAW_PORT, NSP_ETH_PORT_LANES,
				      lanes, NSP_ETH_CTRL_SET_LANES);
}
