/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef __MXL862XX_H
#define __MXL862XX_H

#include <linux/mdio.h>
#include <linux/workqueue.h>
#include <net/dsa.h>

struct mxl862xx_priv;

#define MXL862XX_MAX_PORTS		17
#define MXL862XX_DEFAULT_BRIDGE		0
#define MXL862XX_MAX_BRIDGES		48
#define MXL862XX_MAX_BRIDGE_PORTS	128
#define MXL862XX_TOTAL_EVLAN_ENTRIES	1024
#define MXL862XX_TOTAL_VF_ENTRIES	1024

/* Number of __le16 words in a firmware portmap (128-bit bitmap). */
#define MXL862XX_FW_PORTMAP_WORDS	(MXL862XX_MAX_BRIDGE_PORTS / 16)

/**
 * mxl862xx_fw_portmap_set_bit - set a single port bit in a firmware portmap
 * @map: firmware portmap array (MXL862XX_FW_PORTMAP_WORDS entries)
 * @port: port index (0..MXL862XX_MAX_BRIDGE_PORTS-1)
 */
static inline void mxl862xx_fw_portmap_set_bit(__le16 *map, int port)
{
	map[port / 16] |= cpu_to_le16(BIT(port % 16));
}

/**
 * mxl862xx_fw_portmap_clear_bit - clear a single port bit in a firmware portmap
 * @map: firmware portmap array (MXL862XX_FW_PORTMAP_WORDS entries)
 * @port: port index (0..MXL862XX_MAX_BRIDGE_PORTS-1)
 */
static inline void mxl862xx_fw_portmap_clear_bit(__le16 *map, int port)
{
	map[port / 16] &= ~cpu_to_le16(BIT(port % 16));
}

/**
 * mxl862xx_fw_portmap_is_empty - check whether a firmware portmap has no
 *                                bits set
 * @map: firmware portmap array (MXL862XX_FW_PORTMAP_WORDS entries)
 *
 * Return: true if every word in @map is zero.
 */
static inline bool mxl862xx_fw_portmap_is_empty(const __le16 *map)
{
	int i;

	for (i = 0; i < MXL862XX_FW_PORTMAP_WORDS; i++)
		if (map[i])
			return false;
	return true;
}

/**
 * struct mxl862xx_vf_vid - Per-VID entry within a VLAN Filter block
 * @list:     Linked into &mxl862xx_vf_block.vids
 * @vid:      VLAN ID
 * @index:    Entry index within the VLAN Filter HW block
 * @untagged: Strip tag on egress for this VID (drives EVLAN tag-stripping)
 */
struct mxl862xx_vf_vid {
	struct list_head list;
	u16 vid;
	u16 index;
	bool untagged;
};

/**
 * struct mxl862xx_vf_block - Per-port VLAN Filter block
 * @allocated:    Whether the HW block has been allocated via VLANFILTER_ALLOC
 * @block_id:     HW VLAN Filter block ID from VLANFILTER_ALLOC
 * @block_size:   Total entries allocated in this block
 * @active_count: Number of ALLOW entries at indices [0, active_count).
 *                The bridge port config sends max(active_count, 1) as
 *                block_size to narrow the HW scan window.
 *                discard_unmatched_tagged handles frames outside this range.
 * @vids:         List of &mxl862xx_vf_vid entries programmed in this block
 */
struct mxl862xx_vf_block {
	bool allocated;
	u16 block_id;
	u16 block_size;
	u16 active_count;
	struct list_head vids;
};

/**
 * struct mxl862xx_evlan_block - Per-port per-direction extended VLAN block
 * @allocated:  Whether the HW block has been allocated via EXTENDEDVLAN_ALLOC.
 *              Guards alloc/free idempotency--the block_id is only valid
 *              while allocated is true.
 * @in_use:     Whether the EVLAN engine should be enabled for this block
 *              on the bridge port (sent as the enable flag in
 *              set_bridge_port). Can be false while allocated is still
 *              true -- e.g. when all egress VIDs are removed (idx == 0 in
 *              evlan_program_egress) the block stays allocated for
 *              potential reuse, but the engine is disabled so an empty
 *              rule set does not discard all traffic.
 * @block_id:   HW block ID from EXTENDEDVLAN_ALLOC
 * @block_size: Total entries allocated
 * @n_active:   Number of HW entries currently written. The bridge port
 *              config sends this as the egress scan window, so entries
 *              beyond n_active are never scanned. Always equals
 *              block_size for ingress blocks (fixed catchall rules).
 */
struct mxl862xx_evlan_block {
	bool allocated;
	bool in_use;
	u16 block_id;
	u16 block_size;
	u16 n_active;
};

/**
 * struct mxl862xx_port_stats - 64-bit accumulated hardware port statistics
 * @rx_packets: total received packets
 * @tx_packets: total transmitted packets
 * @rx_bytes: total received bytes
 * @tx_bytes: total transmitted bytes
 * @rx_errors: total receive errors
 * @tx_errors: total transmit errors
 * @rx_dropped: total received packets dropped
 * @tx_dropped: total transmitted packets dropped
 * @multicast: total received multicast packets
 * @collisions: total transmit collisions
 * @rx_length_errors: received length errors (undersize + oversize)
 * @rx_crc_errors: received FCS errors
 * @rx_frame_errors: received alignment errors
 * @prev_rx_good_pkts: previous snapshot of rx good packet counter
 * @prev_tx_good_pkts: previous snapshot of tx good packet counter
 * @prev_rx_good_bytes: previous snapshot of rx good byte counter
 * @prev_tx_good_bytes: previous snapshot of tx good byte counter
 * @prev_rx_fcserror_pkts: previous snapshot of rx FCS error counter
 * @prev_rx_under_size_error_pkts: previous snapshot of rx undersize
 *                                 error counter
 * @prev_rx_oversize_error_pkts: previous snapshot of rx oversize
 *                               error counter
 * @prev_rx_align_error_pkts: previous snapshot of rx alignment
 *                            error counter
 * @prev_tx_dropped_pkts: previous snapshot of tx dropped counter
 * @prev_rx_dropped_pkts: previous snapshot of rx dropped counter
 * @prev_rx_evlan_discard_pkts: previous snapshot of extended VLAN
 *                              discard counter
 * @prev_mtu_exceed_discard_pkts: previous snapshot of MTU exceed
 *                                discard counter
 * @prev_tx_acm_dropped_pkts: previous snapshot of tx ACM dropped
 *                            counter
 * @prev_rx_multicast_pkts: previous snapshot of rx multicast counter
 * @prev_tx_coll_count: previous snapshot of tx collision counter
 *
 * The firmware RMON counters are 32-bit free-running (64-bit for byte
 * counters). This structure holds 64-bit accumulators alongside the
 * previous raw snapshot so that deltas can be computed across polls,
 * handling 32-bit wrap correctly via unsigned subtraction.
 */
struct mxl862xx_port_stats {
	u64 rx_packets;
	u64 tx_packets;
	u64 rx_bytes;
	u64 tx_bytes;
	u64 rx_errors;
	u64 tx_errors;
	u64 rx_dropped;
	u64 tx_dropped;
	u64 multicast;
	u64 collisions;
	u64 rx_length_errors;
	u64 rx_crc_errors;
	u64 rx_frame_errors;
	u32 prev_rx_good_pkts;
	u32 prev_tx_good_pkts;
	u64 prev_rx_good_bytes;
	u64 prev_tx_good_bytes;
	u32 prev_rx_fcserror_pkts;
	u32 prev_rx_under_size_error_pkts;
	u32 prev_rx_oversize_error_pkts;
	u32 prev_rx_align_error_pkts;
	u32 prev_tx_dropped_pkts;
	u32 prev_rx_dropped_pkts;
	u32 prev_rx_evlan_discard_pkts;
	u32 prev_mtu_exceed_discard_pkts;
	u32 prev_tx_acm_dropped_pkts;
	u32 prev_rx_multicast_pkts;
	u32 prev_tx_coll_count;
};

/**
 * struct mxl862xx_port - per-port state tracked by the driver
 * @priv:                back-pointer to switch private data; needed by
 *                       deferred work handlers to access ds and priv
 * @fid:                 firmware FID for the permanent single-port bridge;
 *                       kept alive for the lifetime of the port so traffic is
 *                       never forwarded while the port is unbridged
 * @flood_block:         bitmask of firmware meter indices that are currently
 *                       rate-limiting flood traffic on this port (zero-rate
 *                       meters used to block flooding)
 * @learning:            true when address learning is enabled on this port
 * @setup_done:          set at end of port_setup, cleared at start of
 *                       port_teardown; guards deferred work against
 *                       acting on torn-down state
 * @pvid:                port VLAN ID (native VLAN) assigned to untagged traffic
 * @vlan_filtering:      true when VLAN filtering is enabled on this port
 * @vf:                  per-port VLAN Filter block state
 * @ingress_evlan:       ingress extended VLAN block state
 * @egress_evlan:        egress extended VLAN block state
 * @host_flood_uc:       desired host unicast flood state (true = flood);
 *                       updated atomically by port_set_host_flood, consumed
 *                       by the deferred host_flood_work
 * @host_flood_mc:       desired host multicast flood state (true = flood)
 * @host_flood_work:     deferred work for applying host flood changes;
 *                       port_set_host_flood runs in atomic context (under
 *                       netif_addr_lock) so firmware calls must be deferred.
 *                       The worker acquires rtnl_lock() to serialize with
 *                       DSA callbacks and checks @setup_done to avoid
 *                       acting on torn-down ports.
 * @stats:               64-bit accumulated hardware statistics; updated
 *                       periodically by the stats polling work
 * @stats_lock:          protects accumulator reads in .get_stats64 against
 *                       concurrent updates from the polling work
 */
struct mxl862xx_port {
	struct mxl862xx_priv *priv;
	u16 fid;
	unsigned long flood_block;
	bool learning;
	bool setup_done;
	u16 pvid;
	bool vlan_filtering;
	struct mxl862xx_vf_block vf;
	struct mxl862xx_evlan_block ingress_evlan;
	struct mxl862xx_evlan_block egress_evlan;
	bool host_flood_uc;
	bool host_flood_mc;
	struct work_struct host_flood_work;
	struct mxl862xx_port_stats stats;
	spinlock_t stats_lock; /* protects stats accumulators */
};

/* Bit indices for struct mxl862xx_priv::flags */
#define MXL862XX_FLAG_CRC_ERR		0
#define MXL862XX_FLAG_WORK_STOPPED	1

/**
 * struct mxl862xx_priv - driver private data for an MxL862xx switch
 * @ds:                 pointer to the DSA switch instance
 * @mdiodev:            MDIO device used to communicate with the switch firmware
 * @crc_err_work:       deferred work for shutting down all ports on MDIO CRC
 *                      errors
 * @flags:              atomic status flags; %MXL862XX_FLAG_CRC_ERR is set
 *                      before CRC-triggered shutdown and cleared after;
 *                      %MXL862XX_FLAG_WORK_STOPPED is set before cancelling
 *                      stats_work to prevent rescheduling during teardown
 * @drop_meter:         index of the single shared zero-rate firmware meter
 *                      used to unconditionally drop traffic (used to block
 *                      flooding)
 * @ports:              per-port state, indexed by switch port number
 * @bridges:            maps DSA bridge number to firmware bridge ID;
 *                      zero means no firmware bridge allocated for that
 *                      DSA bridge number. Indexed by dsa_bridge.num
 *                      (0 .. ds->max_num_bridges).
 * @evlan_ingress_size: per-port ingress Extended VLAN block size
 * @evlan_egress_size:  per-port egress Extended VLAN block size
 * @vf_block_size:      per-port VLAN Filter block size
 * @stats_work:         periodic work item that polls RMON hardware counters
 *                      and accumulates them into 64-bit per-port stats
 */
struct mxl862xx_priv {
	struct dsa_switch *ds;
	struct mdio_device *mdiodev;
	struct work_struct crc_err_work;
	unsigned long flags;
	u16 drop_meter;
	struct mxl862xx_port ports[MXL862XX_MAX_PORTS];
	u16 bridges[MXL862XX_MAX_BRIDGES + 1];
	u16 evlan_ingress_size;
	u16 evlan_egress_size;
	u16 vf_block_size;
	struct delayed_work stats_work;
};

#endif /* __MXL862XX_H */
