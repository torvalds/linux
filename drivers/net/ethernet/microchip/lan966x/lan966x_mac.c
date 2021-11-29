// SPDX-License-Identifier: GPL-2.0+

#include "lan966x_main.h"

#define LAN966X_MAC_COLUMNS		4
#define MACACCESS_CMD_IDLE		0
#define MACACCESS_CMD_LEARN		1
#define MACACCESS_CMD_FORGET		2
#define MACACCESS_CMD_AGE		3
#define MACACCESS_CMD_GET_NEXT		4
#define MACACCESS_CMD_INIT		5
#define MACACCESS_CMD_READ		6
#define MACACCESS_CMD_WRITE		7
#define MACACCESS_CMD_SYNC_GET_NEXT	8

static int lan966x_mac_get_status(struct lan966x *lan966x)
{
	return lan_rd(lan966x, ANA_MACACCESS);
}

static int lan966x_mac_wait_for_completion(struct lan966x *lan966x)
{
	u32 val;

	return readx_poll_timeout(lan966x_mac_get_status,
		lan966x, val,
		(ANA_MACACCESS_MAC_TABLE_CMD_GET(val)) ==
		MACACCESS_CMD_IDLE,
		TABLE_UPDATE_SLEEP_US, TABLE_UPDATE_TIMEOUT_US);
}

static void lan966x_mac_select(struct lan966x *lan966x,
			       const unsigned char mac[ETH_ALEN],
			       unsigned int vid)
{
	u32 macl = 0, mach = 0;

	/* Set the MAC address to handle and the vlan associated in a format
	 * understood by the hardware.
	 */
	mach |= vid    << 16;
	mach |= mac[0] << 8;
	mach |= mac[1] << 0;
	macl |= mac[2] << 24;
	macl |= mac[3] << 16;
	macl |= mac[4] << 8;
	macl |= mac[5] << 0;

	lan_wr(macl, lan966x, ANA_MACLDATA);
	lan_wr(mach, lan966x, ANA_MACHDATA);
}

int lan966x_mac_learn(struct lan966x *lan966x, int port,
		      const unsigned char mac[ETH_ALEN],
		      unsigned int vid,
		      enum macaccess_entry_type type)
{
	lan966x_mac_select(lan966x, mac, vid);

	/* Issue a write command */
	lan_wr(ANA_MACACCESS_VALID_SET(1) |
	       ANA_MACACCESS_CHANGE2SW_SET(0) |
	       ANA_MACACCESS_DEST_IDX_SET(port) |
	       ANA_MACACCESS_ENTRYTYPE_SET(type) |
	       ANA_MACACCESS_MAC_TABLE_CMD_SET(MACACCESS_CMD_LEARN),
	       lan966x, ANA_MACACCESS);

	return lan966x_mac_wait_for_completion(lan966x);
}

int lan966x_mac_forget(struct lan966x *lan966x,
		       const unsigned char mac[ETH_ALEN],
		       unsigned int vid,
		       enum macaccess_entry_type type)
{
	lan966x_mac_select(lan966x, mac, vid);

	/* Issue a forget command */
	lan_wr(ANA_MACACCESS_ENTRYTYPE_SET(type) |
	       ANA_MACACCESS_MAC_TABLE_CMD_SET(MACACCESS_CMD_FORGET),
	       lan966x, ANA_MACACCESS);

	return lan966x_mac_wait_for_completion(lan966x);
}

int lan966x_mac_cpu_learn(struct lan966x *lan966x, const char *addr, u16 vid)
{
	return lan966x_mac_learn(lan966x, PGID_CPU, addr, vid, ENTRYTYPE_LOCKED);
}

int lan966x_mac_cpu_forget(struct lan966x *lan966x, const char *addr, u16 vid)
{
	return lan966x_mac_forget(lan966x, addr, vid, ENTRYTYPE_LOCKED);
}

void lan966x_mac_init(struct lan966x *lan966x)
{
	/* Clear the MAC table */
	lan_wr(MACACCESS_CMD_INIT, lan966x, ANA_MACACCESS);
	lan966x_mac_wait_for_completion(lan966x);
}
