// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#include <linux/etherdevice.h>

#include "fbnic.h"
#include "fbnic_netdev.h"
#include "fbnic_rpc.h"

void fbnic_bmc_rpc_all_multi_config(struct fbnic_dev *fbd,
				    bool enable_host)
{
	struct fbnic_mac_addr *mac_addr;

	/* We need to add the all multicast filter at the end of the
	 * multicast address list. This way if there are any that are
	 * shared between the host and the BMC they can be directed to
	 * both. Otherwise the remainder just get sent directly to the
	 * BMC.
	 */
	mac_addr = &fbd->mac_addr[fbd->mac_addr_boundary - 1];
	if (fbnic_bmc_present(fbd) && fbd->fw_cap.all_multi) {
		if (mac_addr->state != FBNIC_TCAM_S_VALID) {
			eth_zero_addr(mac_addr->value.addr8);
			eth_broadcast_addr(mac_addr->mask.addr8);
			mac_addr->value.addr8[0] ^= 1;
			mac_addr->mask.addr8[0] ^= 1;
			set_bit(FBNIC_MAC_ADDR_T_BMC, mac_addr->act_tcam);
			mac_addr->state = FBNIC_TCAM_S_ADD;
		}
		if (enable_host)
			set_bit(FBNIC_MAC_ADDR_T_ALLMULTI,
				mac_addr->act_tcam);
		else
			clear_bit(FBNIC_MAC_ADDR_T_ALLMULTI,
				  mac_addr->act_tcam);
	} else if (!test_bit(FBNIC_MAC_ADDR_T_BMC, mac_addr->act_tcam) &&
		   !is_zero_ether_addr(mac_addr->mask.addr8) &&
		   mac_addr->state == FBNIC_TCAM_S_VALID) {
		clear_bit(FBNIC_MAC_ADDR_T_ALLMULTI, mac_addr->act_tcam);
		clear_bit(FBNIC_MAC_ADDR_T_BMC, mac_addr->act_tcam);
		mac_addr->state = FBNIC_TCAM_S_DELETE;
	}
}

void fbnic_bmc_rpc_init(struct fbnic_dev *fbd)
{
	int i = FBNIC_RPC_TCAM_MACDA_BMC_ADDR_IDX;
	struct fbnic_mac_addr *mac_addr;
	int j;

	/* Check if BMC is present */
	if (!fbnic_bmc_present(fbd))
		return;

	/* Fetch BMC MAC addresses from firmware capabilities */
	for (j = 0; j < 4; j++) {
		u8 *bmc_mac = fbd->fw_cap.bmc_mac_addr[j];

		/* Validate BMC MAC addresses */
		if (is_zero_ether_addr(bmc_mac))
			continue;

		if (is_multicast_ether_addr(bmc_mac))
			mac_addr = __fbnic_mc_sync(fbd, bmc_mac);
		else
			mac_addr = &fbd->mac_addr[i++];

		if (!mac_addr) {
			netdev_err(fbd->netdev,
				   "No slot for BMC MAC address[%d]\n", j);
			continue;
		}

		ether_addr_copy(mac_addr->value.addr8, bmc_mac);
		eth_zero_addr(mac_addr->mask.addr8);

		set_bit(FBNIC_MAC_ADDR_T_BMC, mac_addr->act_tcam);
		mac_addr->state = FBNIC_TCAM_S_ADD;
	}

	/* Validate Broadcast is also present, record it and tag it */
	mac_addr = &fbd->mac_addr[FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX];
	eth_broadcast_addr(mac_addr->value.addr8);
	set_bit(FBNIC_MAC_ADDR_T_BMC, mac_addr->act_tcam);
	mac_addr->state = FBNIC_TCAM_S_ADD;

	fbnic_bmc_rpc_all_multi_config(fbd, false);
}

struct fbnic_mac_addr *__fbnic_uc_sync(struct fbnic_dev *fbd,
				       const unsigned char *addr)
{
	struct fbnic_mac_addr *avail_addr = NULL;
	unsigned int i;

	/* Scan from middle of list to bottom, filling bottom up.
	 * Skip the first entry which is reserved for dev_addr and
	 * leave the last entry to use for promiscuous filtering.
	 */
	for (i = fbd->mac_addr_boundary - 1;
	     i < FBNIC_RPC_TCAM_MACDA_HOST_ADDR_IDX; i++) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (mac_addr->state == FBNIC_TCAM_S_DISABLED) {
			avail_addr = mac_addr;
		} else if (ether_addr_equal(mac_addr->value.addr8, addr)) {
			avail_addr = mac_addr;
			break;
		}
	}

	if (avail_addr && avail_addr->state == FBNIC_TCAM_S_DISABLED) {
		ether_addr_copy(avail_addr->value.addr8, addr);
		eth_zero_addr(avail_addr->mask.addr8);
		avail_addr->state = FBNIC_TCAM_S_ADD;
	}

	return avail_addr;
}

struct fbnic_mac_addr *__fbnic_mc_sync(struct fbnic_dev *fbd,
				       const unsigned char *addr)
{
	struct fbnic_mac_addr *avail_addr = NULL;
	unsigned int i;

	/* Scan from middle of list to top, filling top down.
	 * Skip over the address reserved for the BMC MAC and
	 * exclude index 0 as that belongs to the broadcast address
	 */
	for (i = fbd->mac_addr_boundary;
	     --i > FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX;) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (mac_addr->state == FBNIC_TCAM_S_DISABLED) {
			avail_addr = mac_addr;
		} else if (ether_addr_equal(mac_addr->value.addr8, addr)) {
			avail_addr = mac_addr;
			break;
		}
	}

	/* Scan the BMC addresses to see if it may have already
	 * reserved the address.
	 */
	while (--i) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[i];

		if (!is_zero_ether_addr(mac_addr->mask.addr8))
			continue;

		/* Only move on if we find a match */
		if (!ether_addr_equal(mac_addr->value.addr8, addr))
			continue;

		/* We need to pull this address to the shared area */
		if (avail_addr) {
			memcpy(avail_addr, mac_addr, sizeof(*mac_addr));
			mac_addr->state = FBNIC_TCAM_S_DELETE;
			avail_addr->state = FBNIC_TCAM_S_ADD;
		}

		break;
	}

	if (avail_addr && avail_addr->state == FBNIC_TCAM_S_DISABLED) {
		ether_addr_copy(avail_addr->value.addr8, addr);
		eth_zero_addr(avail_addr->mask.addr8);
		avail_addr->state = FBNIC_TCAM_S_ADD;
	}

	return avail_addr;
}

int __fbnic_xc_unsync(struct fbnic_mac_addr *mac_addr, unsigned int tcam_idx)
{
	if (!test_and_clear_bit(tcam_idx, mac_addr->act_tcam))
		return -ENOENT;

	if (bitmap_empty(mac_addr->act_tcam, FBNIC_RPC_TCAM_ACT_NUM_ENTRIES))
		mac_addr->state = FBNIC_TCAM_S_DELETE;

	return 0;
}

void fbnic_sift_macda(struct fbnic_dev *fbd)
{
	int dest, src;

	/* Move BMC only addresses back into BMC region */
	for (dest = FBNIC_RPC_TCAM_MACDA_BMC_ADDR_IDX,
	     src = FBNIC_RPC_TCAM_MACDA_MULTICAST_IDX;
	     ++dest < FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX &&
	     src < fbd->mac_addr_boundary;) {
		struct fbnic_mac_addr *dest_addr = &fbd->mac_addr[dest];

		if (dest_addr->state != FBNIC_TCAM_S_DISABLED)
			continue;

		while (src < fbd->mac_addr_boundary) {
			struct fbnic_mac_addr *src_addr = &fbd->mac_addr[src++];

			/* Verify BMC bit is set */
			if (!test_bit(FBNIC_MAC_ADDR_T_BMC, src_addr->act_tcam))
				continue;

			/* Verify filter isn't already disabled */
			if (src_addr->state == FBNIC_TCAM_S_DISABLED ||
			    src_addr->state == FBNIC_TCAM_S_DELETE)
				continue;

			/* Verify only BMC bit is set */
			if (bitmap_weight(src_addr->act_tcam,
					  FBNIC_RPC_TCAM_ACT_NUM_ENTRIES) != 1)
				continue;

			/* Verify we are not moving wildcard address */
			if (!is_zero_ether_addr(src_addr->mask.addr8))
				continue;

			memcpy(dest_addr, src_addr, sizeof(*src_addr));
			src_addr->state = FBNIC_TCAM_S_DELETE;
			dest_addr->state = FBNIC_TCAM_S_ADD;
		}
	}
}

static void fbnic_clear_macda_entry(struct fbnic_dev *fbd, unsigned int idx)
{
	int i;

	/* Invalidate entry and clear addr state info */
	for (i = 0; i <= FBNIC_RPC_TCAM_MACDA_WORD_LEN; i++)
		wr32(fbd, FBNIC_RPC_TCAM_MACDA(idx, i), 0);
}

static void fbnic_write_macda_entry(struct fbnic_dev *fbd, unsigned int idx,
				    struct fbnic_mac_addr *mac_addr)
{
	__be16 *mask, *value;
	int i;

	mask = &mac_addr->mask.addr16[FBNIC_RPC_TCAM_MACDA_WORD_LEN - 1];
	value = &mac_addr->value.addr16[FBNIC_RPC_TCAM_MACDA_WORD_LEN - 1];

	for (i = 0; i < FBNIC_RPC_TCAM_MACDA_WORD_LEN; i++)
		wr32(fbd, FBNIC_RPC_TCAM_MACDA(idx, i),
		     FIELD_PREP(FBNIC_RPC_TCAM_MACDA_MASK, ntohs(*mask--)) |
		     FIELD_PREP(FBNIC_RPC_TCAM_MACDA_VALUE, ntohs(*value--)));

	wrfl(fbd);

	wr32(fbd, FBNIC_RPC_TCAM_MACDA(idx, i), FBNIC_RPC_TCAM_VALIDATE);
}

void fbnic_write_macda(struct fbnic_dev *fbd)
{
	int idx;

	for (idx = ARRAY_SIZE(fbd->mac_addr); idx--;) {
		struct fbnic_mac_addr *mac_addr = &fbd->mac_addr[idx];

		/* Check if update flag is set else exit. */
		if (!(mac_addr->state & FBNIC_TCAM_S_UPDATE))
			continue;

		/* Clear by writing 0s. */
		if (mac_addr->state == FBNIC_TCAM_S_DELETE) {
			/* Invalidate entry and clear addr state info */
			fbnic_clear_macda_entry(fbd, idx);
			memset(mac_addr, 0, sizeof(*mac_addr));

			continue;
		}

		fbnic_write_macda_entry(fbd, idx, mac_addr);

		mac_addr->state = FBNIC_TCAM_S_VALID;
	}
}
