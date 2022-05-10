// SPDX-License-Identifier: GPL-2.0-only
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2011-2013 Solarflare Communications Inc.
 */

/* Theory of operation:
 *
 * PTP support is assisted by firmware running on the MC, which provides
 * the hardware timestamping capabilities.  Both transmitted and received
 * PTP event packets are queued onto internal queues for subsequent processing;
 * this is because the MC operations are relatively long and would block
 * block NAPI/interrupt operation.
 *
 * Receive event processing:
 *	The event contains the packet's UUID and sequence number, together
 *	with the hardware timestamp.  The PTP receive packet queue is searched
 *	for this UUID/sequence number and, if found, put on a pending queue.
 *	Packets not matching are delivered without timestamps (MCDI events will
 *	always arrive after the actual packet).
 *	It is important for the operation of the PTP protocol that the ordering
 *	of packets between the event and general port is maintained.
 *
 * Work queue processing:
 *	If work waiting, synchronise host/hardware time
 *
 *	Transmit: send packet through MC, which returns the transmission time
 *	that is converted to an appropriate timestamp.
 *
 *	Receive: the packet's reception time is converted to an appropriate
 *	timestamp.
 */
#include <linux/ip.h>
#include <linux/udp.h>
#include <linux/time.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/pps_kernel.h>
#include <linux/ptp_clock_kernel.h>
#include "net_driver.h"
#include "efx.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "io.h"
#include "farch_regs.h"
#include "tx.h"
#include "nic.h" /* indirectly includes ptp.h */

/* Maximum number of events expected to make up a PTP event */
#define	MAX_EVENT_FRAGS			3

/* Maximum delay, ms, to begin synchronisation */
#define	MAX_SYNCHRONISE_WAIT_MS		2

/* How long, at most, to spend synchronising */
#define	SYNCHRONISE_PERIOD_NS		250000

/* How often to update the shared memory time */
#define	SYNCHRONISATION_GRANULARITY_NS	200

/* Minimum permitted length of a (corrected) synchronisation time */
#define	DEFAULT_MIN_SYNCHRONISATION_NS	120

/* Maximum permitted length of a (corrected) synchronisation time */
#define	MAX_SYNCHRONISATION_NS		1000

/* How many (MC) receive events that can be queued */
#define	MAX_RECEIVE_EVENTS		8

/* Length of (modified) moving average. */
#define	AVERAGE_LENGTH			16

/* How long an unmatched event or packet can be held */
#define PKT_EVENT_LIFETIME_MS		10

/* Offsets into PTP packet for identification.  These offsets are from the
 * start of the IP header, not the MAC header.  Note that neither PTP V1 nor
 * PTP V2 permit the use of IPV4 options.
 */
#define PTP_DPORT_OFFSET	22

#define PTP_V1_VERSION_LENGTH	2
#define PTP_V1_VERSION_OFFSET	28

#define PTP_V1_UUID_LENGTH	6
#define PTP_V1_UUID_OFFSET	50

#define PTP_V1_SEQUENCE_LENGTH	2
#define PTP_V1_SEQUENCE_OFFSET	58

/* The minimum length of a PTP V1 packet for offsets, etc. to be valid:
 * includes IP header.
 */
#define	PTP_V1_MIN_LENGTH	64

#define PTP_V2_VERSION_LENGTH	1
#define PTP_V2_VERSION_OFFSET	29

#define PTP_V2_UUID_LENGTH	8
#define PTP_V2_UUID_OFFSET	48

/* Although PTP V2 UUIDs are comprised a ClockIdentity (8) and PortNumber (2),
 * the MC only captures the last six bytes of the clock identity. These values
 * reflect those, not the ones used in the standard.  The standard permits
 * mapping of V1 UUIDs to V2 UUIDs with these same values.
 */
#define PTP_V2_MC_UUID_LENGTH	6
#define PTP_V2_MC_UUID_OFFSET	50

#define PTP_V2_SEQUENCE_LENGTH	2
#define PTP_V2_SEQUENCE_OFFSET	58

/* The minimum length of a PTP V2 packet for offsets, etc. to be valid:
 * includes IP header.
 */
#define	PTP_V2_MIN_LENGTH	63

#define	PTP_MIN_LENGTH		63

#define PTP_ADDRESS		0xe0000181	/* 224.0.1.129 */
#define PTP_EVENT_PORT		319
#define PTP_GENERAL_PORT	320

/* Annoyingly the format of the version numbers are different between
 * versions 1 and 2 so it isn't possible to simply look for 1 or 2.
 */
#define	PTP_VERSION_V1		1

#define	PTP_VERSION_V2		2
#define	PTP_VERSION_V2_MASK	0x0f

enum ptp_packet_state {
	PTP_PACKET_STATE_UNMATCHED = 0,
	PTP_PACKET_STATE_MATCHED,
	PTP_PACKET_STATE_TIMED_OUT,
	PTP_PACKET_STATE_MATCH_UNWANTED
};

/* NIC synchronised with single word of time only comprising
 * partial seconds and full nanoseconds: 10^9 ~ 2^30 so 2 bits for seconds.
 */
#define	MC_NANOSECOND_BITS	30
#define	MC_NANOSECOND_MASK	((1 << MC_NANOSECOND_BITS) - 1)
#define	MC_SECOND_MASK		((1 << (32 - MC_NANOSECOND_BITS)) - 1)

/* Maximum parts-per-billion adjustment that is acceptable */
#define MAX_PPB			1000000

/* Precalculate scale word to avoid long long division at runtime */
/* This is equivalent to 2^66 / 10^9. */
#define PPB_SCALE_WORD  ((1LL << (57)) / 1953125LL)

/* How much to shift down after scaling to convert to FP40 */
#define PPB_SHIFT_FP40		26
/* ... and FP44. */
#define PPB_SHIFT_FP44		22

#define PTP_SYNC_ATTEMPTS	4

/**
 * struct efx_ptp_match - Matching structure, stored in sk_buff's cb area.
 * @words: UUID and (partial) sequence number
 * @expiry: Time after which the packet should be delivered irrespective of
 *            event arrival.
 * @state: The state of the packet - whether it is ready for processing or
 *         whether that is of no interest.
 */
struct efx_ptp_match {
	u32 words[DIV_ROUND_UP(PTP_V1_UUID_LENGTH, 4)];
	unsigned long expiry;
	enum ptp_packet_state state;
};

/**
 * struct efx_ptp_event_rx - A PTP receive event (from MC)
 * @link: list of events
 * @seq0: First part of (PTP) UUID
 * @seq1: Second part of (PTP) UUID and sequence number
 * @hwtimestamp: Event timestamp
 * @expiry: Time which the packet arrived
 */
struct efx_ptp_event_rx {
	struct list_head link;
	u32 seq0;
	u32 seq1;
	ktime_t hwtimestamp;
	unsigned long expiry;
};

/**
 * struct efx_ptp_timeset - Synchronisation between host and MC
 * @host_start: Host time immediately before hardware timestamp taken
 * @major: Hardware timestamp, major
 * @minor: Hardware timestamp, minor
 * @host_end: Host time immediately after hardware timestamp taken
 * @wait: Number of NIC clock ticks between hardware timestamp being read and
 *          host end time being seen
 * @window: Difference of host_end and host_start
 * @valid: Whether this timeset is valid
 */
struct efx_ptp_timeset {
	u32 host_start;
	u32 major;
	u32 minor;
	u32 host_end;
	u32 wait;
	u32 window;	/* Derived: end - start, allowing for wrap */
};

/**
 * struct efx_ptp_data - Precision Time Protocol (PTP) state
 * @efx: The NIC context
 * @channel: The PTP channel (Siena only)
 * @rx_ts_inline: Flag for whether RX timestamps are inline (else they are
 *	separate events)
 * @rxq: Receive SKB queue (awaiting timestamps)
 * @txq: Transmit SKB queue
 * @evt_list: List of MC receive events awaiting packets
 * @evt_free_list: List of free events
 * @evt_lock: Lock for manipulating evt_list and evt_free_list
 * @rx_evts: Instantiated events (on evt_list and evt_free_list)
 * @workwq: Work queue for processing pending PTP operations
 * @work: Work task
 * @reset_required: A serious error has occurred and the PTP task needs to be
 *                  reset (disable, enable).
 * @rxfilter_event: Receive filter when operating
 * @rxfilter_general: Receive filter when operating
 * @rxfilter_installed: Receive filter installed
 * @config: Current timestamp configuration
 * @enabled: PTP operation enabled
 * @mode: Mode in which PTP operating (PTP version)
 * @ns_to_nic_time: Function to convert from scalar nanoseconds to NIC time
 * @nic_to_kernel_time: Function to convert from NIC to kernel time
 * @nic_time: contains time details
 * @nic_time.minor_max: Wrap point for NIC minor times
 * @nic_time.sync_event_diff_min: Minimum acceptable difference between time
 * in packet prefix and last MCDI time sync event i.e. how much earlier than
 * the last sync event time a packet timestamp can be.
 * @nic_time.sync_event_diff_max: Maximum acceptable difference between time
 * in packet prefix and last MCDI time sync event i.e. how much later than
 * the last sync event time a packet timestamp can be.
 * @nic_time.sync_event_minor_shift: Shift required to make minor time from
 * field in MCDI time sync event.
 * @min_synchronisation_ns: Minimum acceptable corrected sync window
 * @capabilities: Capabilities flags from the NIC
 * @ts_corrections: contains corrections details
 * @ts_corrections.ptp_tx: Required driver correction of PTP packet transmit
 *                         timestamps
 * @ts_corrections.ptp_rx: Required driver correction of PTP packet receive
 *                         timestamps
 * @ts_corrections.pps_out: PPS output error (information only)
 * @ts_corrections.pps_in: Required driver correction of PPS input timestamps
 * @ts_corrections.general_tx: Required driver correction of general packet
 *                             transmit timestamps
 * @ts_corrections.general_rx: Required driver correction of general packet
 *                             receive timestamps
 * @evt_frags: Partly assembled PTP events
 * @evt_frag_idx: Current fragment number
 * @evt_code: Last event code
 * @start: Address at which MC indicates ready for synchronisation
 * @host_time_pps: Host time at last PPS
 * @adjfreq_ppb_shift: Shift required to convert scaled parts-per-billion
 * frequency adjustment into a fixed point fractional nanosecond format.
 * @current_adjfreq: Current ppb adjustment.
 * @phc_clock: Pointer to registered phc device (if primary function)
 * @phc_clock_info: Registration structure for phc device
 * @pps_work: pps work task for handling pps events
 * @pps_workwq: pps work queue
 * @nic_ts_enabled: Flag indicating if NIC generated TS events are handled
 * @txbuf: Buffer for use when transmitting (PTP) packets to MC (avoids
 *         allocations in main data path).
 * @good_syncs: Number of successful synchronisations.
 * @fast_syncs: Number of synchronisations requiring short delay
 * @bad_syncs: Number of failed synchronisations.
 * @sync_timeouts: Number of synchronisation timeouts
 * @no_time_syncs: Number of synchronisations with no good times.
 * @invalid_sync_windows: Number of sync windows with bad durations.
 * @undersize_sync_windows: Number of corrected sync windows that are too small
 * @oversize_sync_windows: Number of corrected sync windows that are too large
 * @rx_no_timestamp: Number of packets received without a timestamp.
 * @timeset: Last set of synchronisation statistics.
 * @xmit_skb: Transmit SKB function.
 */
struct efx_ptp_data {
	struct efx_nic *efx;
	struct efx_channel *channel;
	bool rx_ts_inline;
	struct sk_buff_head rxq;
	struct sk_buff_head txq;
	struct list_head evt_list;
	struct list_head evt_free_list;
	spinlock_t evt_lock;
	struct efx_ptp_event_rx rx_evts[MAX_RECEIVE_EVENTS];
	struct workqueue_struct *workwq;
	struct work_struct work;
	bool reset_required;
	u32 rxfilter_event;
	u32 rxfilter_general;
	bool rxfilter_installed;
	struct hwtstamp_config config;
	bool enabled;
	unsigned int mode;
	void (*ns_to_nic_time)(s64 ns, u32 *nic_major, u32 *nic_minor);
	ktime_t (*nic_to_kernel_time)(u32 nic_major, u32 nic_minor,
				      s32 correction);
	struct {
		u32 minor_max;
		u32 sync_event_diff_min;
		u32 sync_event_diff_max;
		unsigned int sync_event_minor_shift;
	} nic_time;
	unsigned int min_synchronisation_ns;
	unsigned int capabilities;
	struct {
		s32 ptp_tx;
		s32 ptp_rx;
		s32 pps_out;
		s32 pps_in;
		s32 general_tx;
		s32 general_rx;
	} ts_corrections;
	efx_qword_t evt_frags[MAX_EVENT_FRAGS];
	int evt_frag_idx;
	int evt_code;
	struct efx_buffer start;
	struct pps_event_time host_time_pps;
	unsigned int adjfreq_ppb_shift;
	s64 current_adjfreq;
	struct ptp_clock *phc_clock;
	struct ptp_clock_info phc_clock_info;
	struct work_struct pps_work;
	struct workqueue_struct *pps_workwq;
	bool nic_ts_enabled;
	efx_dword_t txbuf[MCDI_TX_BUF_LEN(MC_CMD_PTP_IN_TRANSMIT_LENMAX)];

	unsigned int good_syncs;
	unsigned int fast_syncs;
	unsigned int bad_syncs;
	unsigned int sync_timeouts;
	unsigned int no_time_syncs;
	unsigned int invalid_sync_windows;
	unsigned int undersize_sync_windows;
	unsigned int oversize_sync_windows;
	unsigned int rx_no_timestamp;
	struct efx_ptp_timeset
	timeset[MC_CMD_PTP_OUT_SYNCHRONIZE_TIMESET_MAXNUM];
	void (*xmit_skb)(struct efx_nic *efx, struct sk_buff *skb);
};

static int efx_phc_adjfreq(struct ptp_clock_info *ptp, s32 delta);
static int efx_phc_adjtime(struct ptp_clock_info *ptp, s64 delta);
static int efx_phc_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts);
static int efx_phc_settime(struct ptp_clock_info *ptp,
			   const struct timespec64 *e_ts);
static int efx_phc_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *request, int on);

bool efx_siena_ptp_use_mac_tx_timestamps(struct efx_nic *efx)
{
	return efx_has_cap(efx, TX_MAC_TIMESTAMPING);
}

/* PTP 'extra' channel is still a traffic channel, but we only create TX queues
 * if PTP uses MAC TX timestamps, not if PTP uses the MC directly to transmit.
 */
static bool efx_ptp_want_txqs(struct efx_channel *channel)
{
	return efx_siena_ptp_use_mac_tx_timestamps(channel->efx);
}

#define PTP_SW_STAT(ext_name, field_name)				\
	{ #ext_name, 0, offsetof(struct efx_ptp_data, field_name) }
#define PTP_MC_STAT(ext_name, mcdi_name)				\
	{ #ext_name, 32, MC_CMD_PTP_OUT_STATUS_STATS_ ## mcdi_name ## _OFST }
static const struct efx_hw_stat_desc efx_ptp_stat_desc[] = {
	PTP_SW_STAT(ptp_good_syncs, good_syncs),
	PTP_SW_STAT(ptp_fast_syncs, fast_syncs),
	PTP_SW_STAT(ptp_bad_syncs, bad_syncs),
	PTP_SW_STAT(ptp_sync_timeouts, sync_timeouts),
	PTP_SW_STAT(ptp_no_time_syncs, no_time_syncs),
	PTP_SW_STAT(ptp_invalid_sync_windows, invalid_sync_windows),
	PTP_SW_STAT(ptp_undersize_sync_windows, undersize_sync_windows),
	PTP_SW_STAT(ptp_oversize_sync_windows, oversize_sync_windows),
	PTP_SW_STAT(ptp_rx_no_timestamp, rx_no_timestamp),
	PTP_MC_STAT(ptp_tx_timestamp_packets, TX),
	PTP_MC_STAT(ptp_rx_timestamp_packets, RX),
	PTP_MC_STAT(ptp_timestamp_packets, TS),
	PTP_MC_STAT(ptp_filter_matches, FM),
	PTP_MC_STAT(ptp_non_filter_matches, NFM),
};
#define PTP_STAT_COUNT ARRAY_SIZE(efx_ptp_stat_desc)
static const unsigned long efx_ptp_stat_mask[] = {
	[0 ... BITS_TO_LONGS(PTP_STAT_COUNT) - 1] = ~0UL,
};

size_t efx_siena_ptp_describe_stats(struct efx_nic *efx, u8 *strings)
{
	if (!efx->ptp_data)
		return 0;

	return efx_siena_describe_stats(efx_ptp_stat_desc, PTP_STAT_COUNT,
					efx_ptp_stat_mask, strings);
}

size_t efx_siena_ptp_update_stats(struct efx_nic *efx, u64 *stats)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_PTP_IN_STATUS_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_PTP_OUT_STATUS_LEN);
	size_t i;
	int rc;

	if (!efx->ptp_data)
		return 0;

	/* Copy software statistics */
	for (i = 0; i < PTP_STAT_COUNT; i++) {
		if (efx_ptp_stat_desc[i].dma_width)
			continue;
		stats[i] = *(unsigned int *)((char *)efx->ptp_data +
					     efx_ptp_stat_desc[i].offset);
	}

	/* Fetch MC statistics.  We *must* fill in all statistics or
	 * risk leaking kernel memory to userland, so if the MCDI
	 * request fails we pretend we got zeroes.
	 */
	MCDI_SET_DWORD(inbuf, PTP_IN_OP, MC_CMD_PTP_OP_STATUS);
	MCDI_SET_DWORD(inbuf, PTP_IN_PERIPH_ID, 0);
	rc = efx_siena_mcdi_rpc(efx, MC_CMD_PTP, inbuf, sizeof(inbuf),
				outbuf, sizeof(outbuf), NULL);
	if (rc)
		memset(outbuf, 0, sizeof(outbuf));
	efx_siena_update_stats(efx_ptp_stat_desc, PTP_STAT_COUNT,
			       efx_ptp_stat_mask,
			       stats, _MCDI_PTR(outbuf, 0), false);

	return PTP_STAT_COUNT;
}

/* For Siena platforms NIC time is s and ns */
static void efx_ptp_ns_to_s_ns(s64 ns, u32 *nic_major, u32 *nic_minor)
{
	struct timespec64 ts = ns_to_timespec64(ns);
	*nic_major = (u32)ts.tv_sec;
	*nic_minor = ts.tv_nsec;
}

static ktime_t efx_ptp_s_ns_to_ktime_correction(u32 nic_major, u32 nic_minor,
						s32 correction)
{
	ktime_t kt = ktime_set(nic_major, nic_minor);
	if (correction >= 0)
		kt = ktime_add_ns(kt, (u64)correction);
	else
		kt = ktime_sub_ns(kt, (u64)-correction);
	return kt;
}

/* To convert from s27 format to ns we multiply then divide by a power of 2.
 * For the conversion from ns to s27, the operation is also converted to a
 * multiply and shift.
 */
#define S27_TO_NS_SHIFT	(27)
#define NS_TO_S27_MULT	(((1ULL << 63) + NSEC_PER_SEC / 2) / NSEC_PER_SEC)
#define NS_TO_S27_SHIFT	(63 - S27_TO_NS_SHIFT)
#define S27_MINOR_MAX	(1 << S27_TO_NS_SHIFT)

/* For Huntington platforms NIC time is in seconds and fractions of a second
 * where the minor register only uses 27 bits in units of 2^-27s.
 */
static void efx_ptp_ns_to_s27(s64 ns, u32 *nic_major, u32 *nic_minor)
{
	struct timespec64 ts = ns_to_timespec64(ns);
	u32 maj = (u32)ts.tv_sec;
	u32 min = (u32)(((u64)ts.tv_nsec * NS_TO_S27_MULT +
			 (1ULL << (NS_TO_S27_SHIFT - 1))) >> NS_TO_S27_SHIFT);

	/* The conversion can result in the minor value exceeding the maximum.
	 * In this case, round up to the next second.
	 */
	if (min >= S27_MINOR_MAX) {
		min -= S27_MINOR_MAX;
		maj++;
	}

	*nic_major = maj;
	*nic_minor = min;
}

static inline ktime_t efx_ptp_s27_to_ktime(u32 nic_major, u32 nic_minor)
{
	u32 ns = (u32)(((u64)nic_minor * NSEC_PER_SEC +
			(1ULL << (S27_TO_NS_SHIFT - 1))) >> S27_TO_NS_SHIFT);
	return ktime_set(nic_major, ns);
}

static ktime_t efx_ptp_s27_to_ktime_correction(u32 nic_major, u32 nic_minor,
					       s32 correction)
{
	/* Apply the correction and deal with carry */
	nic_minor += correction;
	if ((s32)nic_minor < 0) {
		nic_minor += S27_MINOR_MAX;
		nic_major--;
	} else if (nic_minor >= S27_MINOR_MAX) {
		nic_minor -= S27_MINOR_MAX;
		nic_major++;
	}

	return efx_ptp_s27_to_ktime(nic_major, nic_minor);
}

/* For Medford2 platforms the time is in seconds and quarter nanoseconds. */
static void efx_ptp_ns_to_s_qns(s64 ns, u32 *nic_major, u32 *nic_minor)
{
	struct timespec64 ts = ns_to_timespec64(ns);

	*nic_major = (u32)ts.tv_sec;
	*nic_minor = ts.tv_nsec * 4;
}

static ktime_t efx_ptp_s_qns_to_ktime_correction(u32 nic_major, u32 nic_minor,
						 s32 correction)
{
	ktime_t kt;

	nic_minor = DIV_ROUND_CLOSEST(nic_minor, 4);
	correction = DIV_ROUND_CLOSEST(correction, 4);

	kt = ktime_set(nic_major, nic_minor);

	if (correction >= 0)
		kt = ktime_add_ns(kt, (u64)correction);
	else
		kt = ktime_sub_ns(kt, (u64)-correction);
	return kt;
}

struct efx_channel *efx_siena_ptp_channel(struct efx_nic *efx)
{
	return efx->ptp_data ? efx->ptp_data->channel : NULL;
}

static u32 last_sync_timestamp_major(struct efx_nic *efx)
{
	struct efx_channel *channel = efx_siena_ptp_channel(efx);
	u32 major = 0;

	if (channel)
		major = channel->sync_timestamp_major;
	return major;
}

/* The 8000 series and later can provide the time from the MAC, which is only
 * 48 bits long and provides meta-information in the top 2 bits.
 */
static ktime_t
efx_ptp_mac_nic_to_ktime_correction(struct efx_nic *efx,
				    struct efx_ptp_data *ptp,
				    u32 nic_major, u32 nic_minor,
				    s32 correction)
{
	u32 sync_timestamp;
	ktime_t kt = { 0 };
	s16 delta;

	if (!(nic_major & 0x80000000)) {
		WARN_ON_ONCE(nic_major >> 16);

		/* Medford provides 48 bits of timestamp, so we must get the top
		 * 16 bits from the timesync event state.
		 *
		 * We only have the lower 16 bits of the time now, but we do
		 * have a full resolution timestamp at some point in past. As
		 * long as the difference between the (real) now and the sync
		 * is less than 2^15, then we can reconstruct the difference
		 * between those two numbers using only the lower 16 bits of
		 * each.
		 *
		 * Put another way
		 *
		 * a - b = ((a mod k) - b) mod k
		 *
		 * when -k/2 < (a-b) < k/2. In our case k is 2^16. We know
		 * (a mod k) and b, so can calculate the delta, a - b.
		 *
		 */
		sync_timestamp = last_sync_timestamp_major(efx);

		/* Because delta is s16 this does an implicit mask down to
		 * 16 bits which is what we need, assuming
		 * MEDFORD_TX_SECS_EVENT_BITS is 16. delta is signed so that
		 * we can deal with the (unlikely) case of sync timestamps
		 * arriving from the future.
		 */
		delta = nic_major - sync_timestamp;

		/* Recover the fully specified time now, by applying the offset
		 * to the (fully specified) sync time.
		 */
		nic_major = sync_timestamp + delta;

		kt = ptp->nic_to_kernel_time(nic_major, nic_minor,
					     correction);
	}
	return kt;
}

ktime_t efx_siena_ptp_nic_to_kernel_time(struct efx_tx_queue *tx_queue)
{
	struct efx_nic *efx = tx_queue->efx;
	struct efx_ptp_data *ptp = efx->ptp_data;
	ktime_t kt;

	if (efx_siena_ptp_use_mac_tx_timestamps(efx))
		kt = efx_ptp_mac_nic_to_ktime_correction(efx, ptp,
				tx_queue->completed_timestamp_major,
				tx_queue->completed_timestamp_minor,
				ptp->ts_corrections.general_tx);
	else
		kt = ptp->nic_to_kernel_time(
				tx_queue->completed_timestamp_major,
				tx_queue->completed_timestamp_minor,
				ptp->ts_corrections.general_tx);
	return kt;
}

/* Get PTP attributes and set up time conversions */
static int efx_ptp_get_attributes(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_PTP_IN_GET_ATTRIBUTES_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_PTP_OUT_GET_ATTRIBUTES_LEN);
	struct efx_ptp_data *ptp = efx->ptp_data;
	int rc;
	u32 fmt;
	size_t out_len;

	/* Get the PTP attributes. If the NIC doesn't support the operation we
	 * use the default format for compatibility with older NICs i.e.
	 * seconds and nanoseconds.
	 */
	MCDI_SET_DWORD(inbuf, PTP_IN_OP, MC_CMD_PTP_OP_GET_ATTRIBUTES);
	MCDI_SET_DWORD(inbuf, PTP_IN_PERIPH_ID, 0);
	rc = efx_siena_mcdi_rpc_quiet(efx, MC_CMD_PTP, inbuf, sizeof(inbuf),
				      outbuf, sizeof(outbuf), &out_len);
	if (rc == 0) {
		fmt = MCDI_DWORD(outbuf, PTP_OUT_GET_ATTRIBUTES_TIME_FORMAT);
	} else if (rc == -EINVAL) {
		fmt = MC_CMD_PTP_OUT_GET_ATTRIBUTES_SECONDS_NANOSECONDS;
	} else if (rc == -EPERM) {
		pci_info(efx->pci_dev, "no PTP support\n");
		return rc;
	} else {
		efx_siena_mcdi_display_error(efx, MC_CMD_PTP, sizeof(inbuf),
					     outbuf, sizeof(outbuf), rc);
		return rc;
	}

	switch (fmt) {
	case MC_CMD_PTP_OUT_GET_ATTRIBUTES_SECONDS_27FRACTION:
		ptp->ns_to_nic_time = efx_ptp_ns_to_s27;
		ptp->nic_to_kernel_time = efx_ptp_s27_to_ktime_correction;
		ptp->nic_time.minor_max = 1 << 27;
		ptp->nic_time.sync_event_minor_shift = 19;
		break;
	case MC_CMD_PTP_OUT_GET_ATTRIBUTES_SECONDS_NANOSECONDS:
		ptp->ns_to_nic_time = efx_ptp_ns_to_s_ns;
		ptp->nic_to_kernel_time = efx_ptp_s_ns_to_ktime_correction;
		ptp->nic_time.minor_max = 1000000000;
		ptp->nic_time.sync_event_minor_shift = 22;
		break;
	case MC_CMD_PTP_OUT_GET_ATTRIBUTES_SECONDS_QTR_NANOSECONDS:
		ptp->ns_to_nic_time = efx_ptp_ns_to_s_qns;
		ptp->nic_to_kernel_time = efx_ptp_s_qns_to_ktime_correction;
		ptp->nic_time.minor_max = 4000000000UL;
		ptp->nic_time.sync_event_minor_shift = 24;
		break;
	default:
		return -ERANGE;
	}

	/* Precalculate acceptable difference between the minor time in the
	 * packet prefix and the last MCDI time sync event. We expect the
	 * packet prefix timestamp to be after of sync event by up to one
	 * sync event interval (0.25s) but we allow it to exceed this by a
	 * fuzz factor of (0.1s)
	 */
	ptp->nic_time.sync_event_diff_min = ptp->nic_time.minor_max
		- (ptp->nic_time.minor_max / 10);
	ptp->nic_time.sync_event_diff_max = (ptp->nic_time.minor_max / 4)
		+ (ptp->nic_time.minor_max / 10);

	/* MC_CMD_PTP_OP_GET_ATTRIBUTES has been extended twice from an older
	 * operation MC_CMD_PTP_OP_GET_TIME_FORMAT. The function now may return
	 * a value to use for the minimum acceptable corrected synchronization
	 * window and may return further capabilities.
	 * If we have the extra information store it. For older firmware that
	 * does not implement the extended command use the default value.
	 */
	if (rc == 0 &&
	    out_len >= MC_CMD_PTP_OUT_GET_ATTRIBUTES_CAPABILITIES_OFST)
		ptp->min_synchronisation_ns =
			MCDI_DWORD(outbuf,
				   PTP_OUT_GET_ATTRIBUTES_SYNC_WINDOW_MIN);
	else
		ptp->min_synchronisation_ns = DEFAULT_MIN_SYNCHRONISATION_NS;

	if (rc == 0 &&
	    out_len >= MC_CMD_PTP_OUT_GET_ATTRIBUTES_LEN)
		ptp->capabilities = MCDI_DWORD(outbuf,
					PTP_OUT_GET_ATTRIBUTES_CAPABILITIES);
	else
		ptp->capabilities = 0;

	/* Set up the shift for conversion between frequency
	 * adjustments in parts-per-billion and the fixed-point
	 * fractional ns format that the adapter uses.
	 */
	if (ptp->capabilities & (1 << MC_CMD_PTP_OUT_GET_ATTRIBUTES_FP44_FREQ_ADJ_LBN))
		ptp->adjfreq_ppb_shift = PPB_SHIFT_FP44;
	else
		ptp->adjfreq_ppb_shift = PPB_SHIFT_FP40;

	return 0;
}

/* Get PTP timestamp corrections */
static int efx_ptp_get_timestamp_corrections(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_PTP_IN_GET_TIMESTAMP_CORRECTIONS_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_LEN);
	int rc;
	size_t out_len;

	/* Get the timestamp corrections from the NIC. If this operation is
	 * not supported (older NICs) then no correction is required.
	 */
	MCDI_SET_DWORD(inbuf, PTP_IN_OP,
		       MC_CMD_PTP_OP_GET_TIMESTAMP_CORRECTIONS);
	MCDI_SET_DWORD(inbuf, PTP_IN_PERIPH_ID, 0);

	rc = efx_siena_mcdi_rpc_quiet(efx, MC_CMD_PTP, inbuf, sizeof(inbuf),
				      outbuf, sizeof(outbuf), &out_len);
	if (rc == 0) {
		efx->ptp_data->ts_corrections.ptp_tx = MCDI_DWORD(outbuf,
			PTP_OUT_GET_TIMESTAMP_CORRECTIONS_TRANSMIT);
		efx->ptp_data->ts_corrections.ptp_rx = MCDI_DWORD(outbuf,
			PTP_OUT_GET_TIMESTAMP_CORRECTIONS_RECEIVE);
		efx->ptp_data->ts_corrections.pps_out = MCDI_DWORD(outbuf,
			PTP_OUT_GET_TIMESTAMP_CORRECTIONS_PPS_OUT);
		efx->ptp_data->ts_corrections.pps_in = MCDI_DWORD(outbuf,
			PTP_OUT_GET_TIMESTAMP_CORRECTIONS_PPS_IN);

		if (out_len >= MC_CMD_PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_LEN) {
			efx->ptp_data->ts_corrections.general_tx = MCDI_DWORD(
				outbuf,
				PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_GENERAL_TX);
			efx->ptp_data->ts_corrections.general_rx = MCDI_DWORD(
				outbuf,
				PTP_OUT_GET_TIMESTAMP_CORRECTIONS_V2_GENERAL_RX);
		} else {
			efx->ptp_data->ts_corrections.general_tx =
				efx->ptp_data->ts_corrections.ptp_tx;
			efx->ptp_data->ts_corrections.general_rx =
				efx->ptp_data->ts_corrections.ptp_rx;
		}
	} else if (rc == -EINVAL) {
		efx->ptp_data->ts_corrections.ptp_tx = 0;
		efx->ptp_data->ts_corrections.ptp_rx = 0;
		efx->ptp_data->ts_corrections.pps_out = 0;
		efx->ptp_data->ts_corrections.pps_in = 0;
		efx->ptp_data->ts_corrections.general_tx = 0;
		efx->ptp_data->ts_corrections.general_rx = 0;
	} else {
		efx_siena_mcdi_display_error(efx, MC_CMD_PTP, sizeof(inbuf),
					     outbuf, sizeof(outbuf), rc);
		return rc;
	}

	return 0;
}

/* Enable MCDI PTP support. */
static int efx_ptp_enable(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_PTP_IN_ENABLE_LEN);
	MCDI_DECLARE_BUF_ERR(outbuf);
	int rc;

	MCDI_SET_DWORD(inbuf, PTP_IN_OP, MC_CMD_PTP_OP_ENABLE);
	MCDI_SET_DWORD(inbuf, PTP_IN_PERIPH_ID, 0);
	MCDI_SET_DWORD(inbuf, PTP_IN_ENABLE_QUEUE,
		       efx->ptp_data->channel ?
		       efx->ptp_data->channel->channel : 0);
	MCDI_SET_DWORD(inbuf, PTP_IN_ENABLE_MODE, efx->ptp_data->mode);

	rc = efx_siena_mcdi_rpc_quiet(efx, MC_CMD_PTP, inbuf, sizeof(inbuf),
				      outbuf, sizeof(outbuf), NULL);
	rc = (rc == -EALREADY) ? 0 : rc;
	if (rc)
		efx_siena_mcdi_display_error(efx, MC_CMD_PTP,
					     MC_CMD_PTP_IN_ENABLE_LEN,
					     outbuf, sizeof(outbuf), rc);
	return rc;
}

/* Disable MCDI PTP support.
 *
 * Note that this function should never rely on the presence of ptp_data -
 * may be called before that exists.
 */
static int efx_ptp_disable(struct efx_nic *efx)
{
	MCDI_DECLARE_BUF(inbuf, MC_CMD_PTP_IN_DISABLE_LEN);
	MCDI_DECLARE_BUF_ERR(outbuf);
	int rc;

	MCDI_SET_DWORD(inbuf, PTP_IN_OP, MC_CMD_PTP_OP_DISABLE);
	MCDI_SET_DWORD(inbuf, PTP_IN_PERIPH_ID, 0);
	rc = efx_siena_mcdi_rpc_quiet(efx, MC_CMD_PTP, inbuf, sizeof(inbuf),
				      outbuf, sizeof(outbuf), NULL);
	rc = (rc == -EALREADY) ? 0 : rc;
	/* If we get ENOSYS, the NIC doesn't support PTP, and thus this function
	 * should only have been called during probe.
	 */
	if (rc == -ENOSYS || rc == -EPERM)
		pci_info(efx->pci_dev, "no PTP support\n");
	else if (rc)
		efx_siena_mcdi_display_error(efx, MC_CMD_PTP,
					     MC_CMD_PTP_IN_DISABLE_LEN,
					     outbuf, sizeof(outbuf), rc);
	return rc;
}

static void efx_ptp_deliver_rx_queue(struct sk_buff_head *q)
{
	struct sk_buff *skb;

	while ((skb = skb_dequeue(q))) {
		local_bh_disable();
		netif_receive_skb(skb);
		local_bh_enable();
	}
}

static void efx_ptp_handle_no_channel(struct efx_nic *efx)
{
	netif_err(efx, drv, efx->net_dev,
		  "ERROR: PTP requires MSI-X and 1 additional interrupt"
		  "vector. PTP disabled\n");
}

/* Repeatedly send the host time to the MC which will capture the hardware
 * time.
 */
static void efx_ptp_send_times(struct efx_nic *efx,
			       struct pps_event_time *last_time)
{
	struct pps_event_time now;
	struct timespec64 limit;
	struct efx_ptp_data *ptp = efx->ptp_data;
	int *mc_running = ptp->start.addr;

	pps_get_ts(&now);
	limit = now.ts_real;
	timespec64_add_ns(&limit, SYNCHRONISE_PERIOD_NS);

	/* Write host time for specified period or until MC is done */
	while ((timespec64_compare(&now.ts_real, &limit) < 0) &&
	       READ_ONCE(*mc_running)) {
		struct timespec64 update_time;
		unsigned int host_time;

		/* Don't update continuously to avoid saturating the PCIe bus */
		update_time = now.ts_real;
		timespec64_add_ns(&update_time, SYNCHRONISATION_GRANULARITY_NS);
		do {
			pps_get_ts(&now);
		} while ((timespec64_compare(&now.ts_real, &update_time) < 0) &&
			 READ_ONCE(*mc_running));

		/* Synchronise NIC with single word of time only */
		host_time = (now.ts_real.tv_sec << MC_NANOSECOND_BITS |
			     now.ts_real.tv_nsec);
		/* Update host time in NIC memory */
		efx->type->ptp_write_host_time(efx, host_time);
	}
	*last_time = now;
}

/* Read a timeset from the MC's results and partial process. */
static void efx_ptp_read_timeset(MCDI_DECLARE_STRUCT_PTR(data),
				 struct efx_ptp_timeset *timeset)
{
	unsigned start_ns, end_ns;

	timeset->host_start = MCDI_DWORD(data, PTP_OUT_SYNCHRONIZE_HOSTSTART);
	timeset->major = MCDI_DWORD(data, PTP_OUT_SYNCHRONIZE_MAJOR);
	timeset->minor = MCDI_DWORD(data, PTP_OUT_SYNCHRONIZE_MINOR);
	timeset->host_end = MCDI_DWORD(data, PTP_OUT_SYNCHRONIZE_HOSTEND),
	timeset->wait = MCDI_DWORD(data, PTP_OUT_SYNCHRONIZE_WAITNS);

	/* Ignore seconds */
	start_ns = timeset->host_start & MC_NANOSECOND_MASK;
	end_ns = timeset->host_end & MC_NANOSECOND_MASK;
	/* Allow for rollover */
	if (end_ns < start_ns)
		end_ns += NSEC_PER_SEC;
	/* Determine duration of operation */
	timeset->window = end_ns - start_ns;
}

/* Process times received from MC.
 *
 * Extract times from returned results, and establish the minimum value
 * seen.  The minimum value represents the "best" possible time and events
 * too much greater than this are rejected - the machine is, perhaps, too
 * busy. A number of readings are taken so that, hopefully, at least one good
 * synchronisation will be seen in the results.
 */
static int
efx_ptp_process_times(struct efx_nic *efx, MCDI_DECLARE_STRUCT_PTR(synch_buf),
		      size_t response_length,
		      const struct pps_event_time *last_time)
{
	unsigned number_readings =
		MCDI_VAR_ARRAY_LEN(response_length,
				   PTP_OUT_SYNCHRONIZE_TIMESET);
	unsigned i;
	unsigned ngood = 0;
	unsigned last_good = 0;
	struct efx_ptp_data *ptp = efx->ptp_data;
	u32 last_sec;
	u32 start_sec;
	struct timespec64 delta;
	ktime_t mc_time;

	if (number_readings == 0)
		return -EAGAIN;

	/* Read the set of results and find the last good host-MC
	 * synchronization result. The MC times when it finishes reading the
	 * host time so the corrected window time should be fairly constant
	 * for a given platform. Increment stats for any results that appear
	 * to be erroneous.
	 */
	for (i = 0; i < number_readings; i++) {
		s32 window, corrected;
		struct timespec64 wait;

		efx_ptp_read_timeset(
			MCDI_ARRAY_STRUCT_PTR(synch_buf,
					      PTP_OUT_SYNCHRONIZE_TIMESET, i),
			&ptp->timeset[i]);

		wait = ktime_to_timespec64(
			ptp->nic_to_kernel_time(0, ptp->timeset[i].wait, 0));
		window = ptp->timeset[i].window;
		corrected = window - wait.tv_nsec;

		/* We expect the uncorrected synchronization window to be at
		 * least as large as the interval between host start and end
		 * times. If it is smaller than this then this is mostly likely
		 * to be a consequence of the host's time being adjusted.
		 * Check that the corrected sync window is in a reasonable
		 * range. If it is out of range it is likely to be because an
		 * interrupt or other delay occurred between reading the system
		 * time and writing it to MC memory.
		 */
		if (window < SYNCHRONISATION_GRANULARITY_NS) {
			++ptp->invalid_sync_windows;
		} else if (corrected >= MAX_SYNCHRONISATION_NS) {
			++ptp->oversize_sync_windows;
		} else if (corrected < ptp->min_synchronisation_ns) {
			++ptp->undersize_sync_windows;
		} else {
			ngood++;
			last_good = i;
		}
	}

	if (ngood == 0) {
		netif_warn(efx, drv, efx->net_dev,
			   "PTP no suitable synchronisations\n");
		return -EAGAIN;
	}

	/* Calculate delay from last good sync (host time) to last_time.
	 * It is possible that the seconds rolled over between taking
	 * the start reading and the last value written by the host.  The
	 * timescales are such that a gap of more than one second is never
	 * expected.  delta is *not* normalised.
	 */
	start_sec = ptp->timeset[last_good].host_start >> MC_NANOSECOND_BITS;
	last_sec = last_time->ts_real.tv_sec & MC_SECOND_MASK;
	if (start_sec != last_sec &&
	    ((start_sec + 1) & MC_SECOND_MASK) != last_sec) {
		netif_warn(efx, hw, efx->net_dev,
			   "PTP bad synchronisation seconds\n");
		return -EAGAIN;
	}
	delta.tv_sec = (last_sec - start_sec) & 1;
	delta.tv_nsec =
		last_time->ts_real.tv_nsec -
		(ptp->timeset[last_good].host_start & MC_NANOSECOND_MASK);

	/* Convert the NIC time at last good sync into kernel time.
	 * No correction is required - this time is the output of a
	 * firmware process.
	 */
	mc_time = ptp->nic_to_kernel_time(ptp->timeset[last_good].major,
					  ptp->timeset[last_good].minor, 0);

	/* Calculate delay from NIC top of second to last_time */
	delta.tv_nsec += ktime_to_timespec64(mc_time).tv_nsec;

	/* Set PPS timestamp to match NIC top of second */
	ptp->host_time_pps = *last_time;
	pps_sub_ts(&ptp->host_time_pps, delta);

	return 0;
}

/* Synchronize times between the host and the MC */
static int efx_ptp_synchronize(struct efx_nic *efx, unsigned int num_readings)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	MCDI_DECLARE_BUF(synch_buf, MC_CMD_PTP_OUT_SYNCHRONIZE_LENMAX);
	size_t response_length;
	int rc;
	unsigned long timeout;
	struct pps_event_time last_time = {};
	unsigned int loops = 0;
	int *start = ptp->start.addr;

	MCDI_SET_DWORD(synch_buf, PTP_IN_OP, MC_CMD_PTP_OP_SYNCHRONIZE);
	MCDI_SET_DWORD(synch_buf, PTP_IN_PERIPH_ID, 0);
	MCDI_SET_DWORD(synch_buf, PTP_IN_SYNCHRONIZE_NUMTIMESETS,
		       num_readings);
	MCDI_SET_QWORD(synch_buf, PTP_IN_SYNCHRONIZE_START_ADDR,
		       ptp->start.dma_addr);

	/* Clear flag that signals MC ready */
	WRITE_ONCE(*start, 0);
	rc = efx_siena_mcdi_rpc_start(efx, MC_CMD_PTP, synch_buf,
				      MC_CMD_PTP_IN_SYNCHRONIZE_LEN);
	EFX_WARN_ON_ONCE_PARANOID(rc);

	/* Wait for start from MCDI (or timeout) */
	timeout = jiffies + msecs_to_jiffies(MAX_SYNCHRONISE_WAIT_MS);
	while (!READ_ONCE(*start) && (time_before(jiffies, timeout))) {
		udelay(20);	/* Usually start MCDI execution quickly */
		loops++;
	}

	if (loops <= 1)
		++ptp->fast_syncs;
	if (!time_before(jiffies, timeout))
		++ptp->sync_timeouts;

	if (READ_ONCE(*start))
		efx_ptp_send_times(efx, &last_time);

	/* Collect results */
	rc = efx_siena_mcdi_rpc_finish(efx, MC_CMD_PTP,
				       MC_CMD_PTP_IN_SYNCHRONIZE_LEN,
				       synch_buf, sizeof(synch_buf),
				       &response_length);
	if (rc == 0) {
		rc = efx_ptp_process_times(efx, synch_buf, response_length,
					   &last_time);
		if (rc == 0)
			++ptp->good_syncs;
		else
			++ptp->no_time_syncs;
	}

	/* Increment the bad syncs counter if the synchronize fails, whatever
	 * the reason.
	 */
	if (rc != 0)
		++ptp->bad_syncs;

	return rc;
}

/* Transmit a PTP packet via the dedicated hardware timestamped queue. */
static void efx_ptp_xmit_skb_queue(struct efx_nic *efx, struct sk_buff *skb)
{
	struct efx_ptp_data *ptp_data = efx->ptp_data;
	u8 type = efx_tx_csum_type_skb(skb);
	struct efx_tx_queue *tx_queue;

	tx_queue = efx_channel_get_tx_queue(ptp_data->channel, type);
	if (tx_queue && tx_queue->timestamping) {
		efx_enqueue_skb(tx_queue, skb);
	} else {
		WARN_ONCE(1, "PTP channel has no timestamped tx queue\n");
		dev_kfree_skb_any(skb);
	}
}

/* Transmit a PTP packet, via the MCDI interface, to the wire. */
static void efx_ptp_xmit_skb_mc(struct efx_nic *efx, struct sk_buff *skb)
{
	struct efx_ptp_data *ptp_data = efx->ptp_data;
	struct skb_shared_hwtstamps timestamps;
	int rc = -EIO;
	MCDI_DECLARE_BUF(txtime, MC_CMD_PTP_OUT_TRANSMIT_LEN);
	size_t len;

	MCDI_SET_DWORD(ptp_data->txbuf, PTP_IN_OP, MC_CMD_PTP_OP_TRANSMIT);
	MCDI_SET_DWORD(ptp_data->txbuf, PTP_IN_PERIPH_ID, 0);
	MCDI_SET_DWORD(ptp_data->txbuf, PTP_IN_TRANSMIT_LENGTH, skb->len);
	if (skb_shinfo(skb)->nr_frags != 0) {
		rc = skb_linearize(skb);
		if (rc != 0)
			goto fail;
	}

	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		rc = skb_checksum_help(skb);
		if (rc != 0)
			goto fail;
	}
	skb_copy_from_linear_data(skb,
				  MCDI_PTR(ptp_data->txbuf,
					   PTP_IN_TRANSMIT_PACKET),
				  skb->len);
	rc = efx_siena_mcdi_rpc(efx, MC_CMD_PTP, ptp_data->txbuf,
				MC_CMD_PTP_IN_TRANSMIT_LEN(skb->len), txtime,
				sizeof(txtime), &len);
	if (rc != 0)
		goto fail;

	memset(&timestamps, 0, sizeof(timestamps));
	timestamps.hwtstamp = ptp_data->nic_to_kernel_time(
		MCDI_DWORD(txtime, PTP_OUT_TRANSMIT_MAJOR),
		MCDI_DWORD(txtime, PTP_OUT_TRANSMIT_MINOR),
		ptp_data->ts_corrections.ptp_tx);

	skb_tstamp_tx(skb, &timestamps);

	rc = 0;

fail:
	dev_kfree_skb_any(skb);

	return;
}

static void efx_ptp_drop_time_expired_events(struct efx_nic *efx)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	struct list_head *cursor;
	struct list_head *next;

	if (ptp->rx_ts_inline)
		return;

	/* Drop time-expired events */
	spin_lock_bh(&ptp->evt_lock);
	list_for_each_safe(cursor, next, &ptp->evt_list) {
		struct efx_ptp_event_rx *evt;

		evt = list_entry(cursor, struct efx_ptp_event_rx,
				 link);
		if (time_after(jiffies, evt->expiry)) {
			list_move(&evt->link, &ptp->evt_free_list);
			netif_warn(efx, hw, efx->net_dev,
				   "PTP rx event dropped\n");
		}
	}
	spin_unlock_bh(&ptp->evt_lock);
}

static enum ptp_packet_state efx_ptp_match_rx(struct efx_nic *efx,
					      struct sk_buff *skb)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	bool evts_waiting;
	struct list_head *cursor;
	struct list_head *next;
	struct efx_ptp_match *match;
	enum ptp_packet_state rc = PTP_PACKET_STATE_UNMATCHED;

	WARN_ON_ONCE(ptp->rx_ts_inline);

	spin_lock_bh(&ptp->evt_lock);
	evts_waiting = !list_empty(&ptp->evt_list);
	spin_unlock_bh(&ptp->evt_lock);

	if (!evts_waiting)
		return PTP_PACKET_STATE_UNMATCHED;

	match = (struct efx_ptp_match *)skb->cb;
	/* Look for a matching timestamp in the event queue */
	spin_lock_bh(&ptp->evt_lock);
	list_for_each_safe(cursor, next, &ptp->evt_list) {
		struct efx_ptp_event_rx *evt;

		evt = list_entry(cursor, struct efx_ptp_event_rx, link);
		if ((evt->seq0 == match->words[0]) &&
		    (evt->seq1 == match->words[1])) {
			struct skb_shared_hwtstamps *timestamps;

			/* Match - add in hardware timestamp */
			timestamps = skb_hwtstamps(skb);
			timestamps->hwtstamp = evt->hwtimestamp;

			match->state = PTP_PACKET_STATE_MATCHED;
			rc = PTP_PACKET_STATE_MATCHED;
			list_move(&evt->link, &ptp->evt_free_list);
			break;
		}
	}
	spin_unlock_bh(&ptp->evt_lock);

	return rc;
}

/* Process any queued receive events and corresponding packets
 *
 * q is returned with all the packets that are ready for delivery.
 */
static void efx_ptp_process_events(struct efx_nic *efx, struct sk_buff_head *q)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	struct sk_buff *skb;

	while ((skb = skb_dequeue(&ptp->rxq))) {
		struct efx_ptp_match *match;

		match = (struct efx_ptp_match *)skb->cb;
		if (match->state == PTP_PACKET_STATE_MATCH_UNWANTED) {
			__skb_queue_tail(q, skb);
		} else if (efx_ptp_match_rx(efx, skb) ==
			   PTP_PACKET_STATE_MATCHED) {
			__skb_queue_tail(q, skb);
		} else if (time_after(jiffies, match->expiry)) {
			match->state = PTP_PACKET_STATE_TIMED_OUT;
			++ptp->rx_no_timestamp;
			__skb_queue_tail(q, skb);
		} else {
			/* Replace unprocessed entry and stop */
			skb_queue_head(&ptp->rxq, skb);
			break;
		}
	}
}

/* Complete processing of a received packet */
static inline void efx_ptp_process_rx(struct efx_nic *efx, struct sk_buff *skb)
{
	local_bh_disable();
	netif_receive_skb(skb);
	local_bh_enable();
}

static void efx_ptp_remove_multicast_filters(struct efx_nic *efx)
{
	struct efx_ptp_data *ptp = efx->ptp_data;

	if (ptp->rxfilter_installed) {
		efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED,
					  ptp->rxfilter_general);
		efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED,
					  ptp->rxfilter_event);
		ptp->rxfilter_installed = false;
	}
}

static int efx_ptp_insert_multicast_filters(struct efx_nic *efx)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	struct efx_filter_spec rxfilter;
	int rc;

	if (!ptp->channel || ptp->rxfilter_installed)
		return 0;

	/* Must filter on both event and general ports to ensure
	 * that there is no packet re-ordering.
	 */
	efx_filter_init_rx(&rxfilter, EFX_FILTER_PRI_REQUIRED, 0,
			   efx_rx_queue_index(
				   efx_channel_get_rx_queue(ptp->channel)));
	rc = efx_filter_set_ipv4_local(&rxfilter, IPPROTO_UDP,
				       htonl(PTP_ADDRESS),
				       htons(PTP_EVENT_PORT));
	if (rc != 0)
		return rc;

	rc = efx_filter_insert_filter(efx, &rxfilter, true);
	if (rc < 0)
		return rc;
	ptp->rxfilter_event = rc;

	efx_filter_init_rx(&rxfilter, EFX_FILTER_PRI_REQUIRED, 0,
			   efx_rx_queue_index(
				   efx_channel_get_rx_queue(ptp->channel)));
	rc = efx_filter_set_ipv4_local(&rxfilter, IPPROTO_UDP,
				       htonl(PTP_ADDRESS),
				       htons(PTP_GENERAL_PORT));
	if (rc != 0)
		goto fail;

	rc = efx_filter_insert_filter(efx, &rxfilter, true);
	if (rc < 0)
		goto fail;
	ptp->rxfilter_general = rc;

	ptp->rxfilter_installed = true;
	return 0;

fail:
	efx_filter_remove_id_safe(efx, EFX_FILTER_PRI_REQUIRED,
				  ptp->rxfilter_event);
	return rc;
}

static int efx_ptp_start(struct efx_nic *efx)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	int rc;

	ptp->reset_required = false;

	rc = efx_ptp_insert_multicast_filters(efx);
	if (rc)
		return rc;

	rc = efx_ptp_enable(efx);
	if (rc != 0)
		goto fail;

	ptp->evt_frag_idx = 0;
	ptp->current_adjfreq = 0;

	return 0;

fail:
	efx_ptp_remove_multicast_filters(efx);
	return rc;
}

static int efx_ptp_stop(struct efx_nic *efx)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	struct list_head *cursor;
	struct list_head *next;
	int rc;

	if (ptp == NULL)
		return 0;

	rc = efx_ptp_disable(efx);

	efx_ptp_remove_multicast_filters(efx);

	/* Make sure RX packets are really delivered */
	efx_ptp_deliver_rx_queue(&efx->ptp_data->rxq);
	skb_queue_purge(&efx->ptp_data->txq);

	/* Drop any pending receive events */
	spin_lock_bh(&efx->ptp_data->evt_lock);
	list_for_each_safe(cursor, next, &efx->ptp_data->evt_list) {
		list_move(cursor, &efx->ptp_data->evt_free_list);
	}
	spin_unlock_bh(&efx->ptp_data->evt_lock);

	return rc;
}

static int efx_ptp_restart(struct efx_nic *efx)
{
	if (efx->ptp_data && efx->ptp_data->enabled)
		return efx_ptp_start(efx);
	return 0;
}

static void efx_ptp_pps_worker(struct work_struct *work)
{
	struct efx_ptp_data *ptp =
		container_of(work, struct efx_ptp_data, pps_work);
	struct efx_nic *efx = ptp->efx;
	struct ptp_clock_event ptp_evt;

	if (efx_ptp_synchronize(efx, PTP_SYNC_ATTEMPTS))
		return;

	ptp_evt.type = PTP_CLOCK_PPSUSR;
	ptp_evt.pps_times = ptp->host_time_pps;
	ptp_clock_event(ptp->phc_clock, &ptp_evt);
}

static void efx_ptp_worker(struct work_struct *work)
{
	struct efx_ptp_data *ptp_data =
		container_of(work, struct efx_ptp_data, work);
	struct efx_nic *efx = ptp_data->efx;
	struct sk_buff *skb;
	struct sk_buff_head tempq;

	if (ptp_data->reset_required) {
		efx_ptp_stop(efx);
		efx_ptp_start(efx);
		return;
	}

	efx_ptp_drop_time_expired_events(efx);

	__skb_queue_head_init(&tempq);
	efx_ptp_process_events(efx, &tempq);

	while ((skb = skb_dequeue(&ptp_data->txq)))
		ptp_data->xmit_skb(efx, skb);

	while ((skb = __skb_dequeue(&tempq)))
		efx_ptp_process_rx(efx, skb);
}

static const struct ptp_clock_info efx_phc_clock_info = {
	.owner		= THIS_MODULE,
	.name		= "sfc",
	.max_adj	= MAX_PPB,
	.n_alarm	= 0,
	.n_ext_ts	= 0,
	.n_per_out	= 0,
	.n_pins		= 0,
	.pps		= 1,
	.adjfreq	= efx_phc_adjfreq,
	.adjtime	= efx_phc_adjtime,
	.gettime64	= efx_phc_gettime,
	.settime64	= efx_phc_settime,
	.enable		= efx_phc_enable,
};

/* Initialise PTP state. */
static int efx_ptp_probe(struct efx_nic *efx, struct efx_channel *channel)
{
	struct efx_ptp_data *ptp;
	int rc = 0;
	unsigned int pos;

	ptp = kzalloc(sizeof(struct efx_ptp_data), GFP_KERNEL);
	efx->ptp_data = ptp;
	if (!efx->ptp_data)
		return -ENOMEM;

	ptp->efx = efx;
	ptp->channel = channel;
	ptp->rx_ts_inline = efx_nic_rev(efx) >= EFX_REV_HUNT_A0;

	rc = efx_siena_alloc_buffer(efx, &ptp->start, sizeof(int), GFP_KERNEL);
	if (rc != 0)
		goto fail1;

	skb_queue_head_init(&ptp->rxq);
	skb_queue_head_init(&ptp->txq);
	ptp->workwq = create_singlethread_workqueue("sfc_ptp");
	if (!ptp->workwq) {
		rc = -ENOMEM;
		goto fail2;
	}

	if (efx_siena_ptp_use_mac_tx_timestamps(efx)) {
		ptp->xmit_skb = efx_ptp_xmit_skb_queue;
		/* Request sync events on this channel. */
		channel->sync_events_state = SYNC_EVENTS_QUIESCENT;
	} else {
		ptp->xmit_skb = efx_ptp_xmit_skb_mc;
	}

	INIT_WORK(&ptp->work, efx_ptp_worker);
	ptp->config.flags = 0;
	ptp->config.tx_type = HWTSTAMP_TX_OFF;
	ptp->config.rx_filter = HWTSTAMP_FILTER_NONE;
	INIT_LIST_HEAD(&ptp->evt_list);
	INIT_LIST_HEAD(&ptp->evt_free_list);
	spin_lock_init(&ptp->evt_lock);
	for (pos = 0; pos < MAX_RECEIVE_EVENTS; pos++)
		list_add(&ptp->rx_evts[pos].link, &ptp->evt_free_list);

	/* Get the NIC PTP attributes and set up time conversions */
	rc = efx_ptp_get_attributes(efx);
	if (rc < 0)
		goto fail3;

	/* Get the timestamp corrections */
	rc = efx_ptp_get_timestamp_corrections(efx);
	if (rc < 0)
		goto fail3;

	if (efx->mcdi->fn_flags &
	    (1 << MC_CMD_DRV_ATTACH_EXT_OUT_FLAG_PRIMARY)) {
		ptp->phc_clock_info = efx_phc_clock_info;
		ptp->phc_clock = ptp_clock_register(&ptp->phc_clock_info,
						    &efx->pci_dev->dev);
		if (IS_ERR(ptp->phc_clock)) {
			rc = PTR_ERR(ptp->phc_clock);
			goto fail3;
		} else if (ptp->phc_clock) {
			INIT_WORK(&ptp->pps_work, efx_ptp_pps_worker);
			ptp->pps_workwq = create_singlethread_workqueue("sfc_pps");
			if (!ptp->pps_workwq) {
				rc = -ENOMEM;
				goto fail4;
			}
		}
	}
	ptp->nic_ts_enabled = false;

	return 0;
fail4:
	ptp_clock_unregister(efx->ptp_data->phc_clock);

fail3:
	destroy_workqueue(efx->ptp_data->workwq);

fail2:
	efx_siena_free_buffer(efx, &ptp->start);

fail1:
	kfree(efx->ptp_data);
	efx->ptp_data = NULL;

	return rc;
}

/* Initialise PTP channel.
 *
 * Setting core_index to zero causes the queue to be initialised and doesn't
 * overlap with 'rxq0' because ptp.c doesn't use skb_record_rx_queue.
 */
static int efx_ptp_probe_channel(struct efx_channel *channel)
{
	struct efx_nic *efx = channel->efx;
	int rc;

	channel->irq_moderation_us = 0;
	channel->rx_queue.core_index = 0;

	rc = efx_ptp_probe(efx, channel);
	/* Failure to probe PTP is not fatal; this channel will just not be
	 * used for anything.
	 * In the case of EPERM, efx_ptp_probe will print its own message (in
	 * efx_ptp_get_attributes()), so we don't need to.
	 */
	if (rc && rc != -EPERM)
		netif_warn(efx, drv, efx->net_dev,
			   "Failed to probe PTP, rc=%d\n", rc);
	return 0;
}

static void efx_ptp_remove(struct efx_nic *efx)
{
	if (!efx->ptp_data)
		return;

	(void)efx_ptp_disable(efx);

	cancel_work_sync(&efx->ptp_data->work);
	if (efx->ptp_data->pps_workwq)
		cancel_work_sync(&efx->ptp_data->pps_work);

	skb_queue_purge(&efx->ptp_data->rxq);
	skb_queue_purge(&efx->ptp_data->txq);

	if (efx->ptp_data->phc_clock) {
		destroy_workqueue(efx->ptp_data->pps_workwq);
		ptp_clock_unregister(efx->ptp_data->phc_clock);
	}

	destroy_workqueue(efx->ptp_data->workwq);

	efx_siena_free_buffer(efx, &efx->ptp_data->start);
	kfree(efx->ptp_data);
	efx->ptp_data = NULL;
}

static void efx_ptp_remove_channel(struct efx_channel *channel)
{
	efx_ptp_remove(channel->efx);
}

static void efx_ptp_get_channel_name(struct efx_channel *channel,
				     char *buf, size_t len)
{
	snprintf(buf, len, "%s-ptp", channel->efx->name);
}

/* Determine whether this packet should be processed by the PTP module
 * or transmitted conventionally.
 */
bool efx_siena_ptp_is_ptp_tx(struct efx_nic *efx, struct sk_buff *skb)
{
	return efx->ptp_data &&
		efx->ptp_data->enabled &&
		skb->len >= PTP_MIN_LENGTH &&
		skb->len <= MC_CMD_PTP_IN_TRANSMIT_PACKET_MAXNUM &&
		likely(skb->protocol == htons(ETH_P_IP)) &&
		skb_transport_header_was_set(skb) &&
		skb_network_header_len(skb) >= sizeof(struct iphdr) &&
		ip_hdr(skb)->protocol == IPPROTO_UDP &&
		skb_headlen(skb) >=
		skb_transport_offset(skb) + sizeof(struct udphdr) &&
		udp_hdr(skb)->dest == htons(PTP_EVENT_PORT);
}

/* Receive a PTP packet.  Packets are queued until the arrival of
 * the receive timestamp from the MC - this will probably occur after the
 * packet arrival because of the processing in the MC.
 */
static bool efx_ptp_rx(struct efx_channel *channel, struct sk_buff *skb)
{
	struct efx_nic *efx = channel->efx;
	struct efx_ptp_data *ptp = efx->ptp_data;
	struct efx_ptp_match *match = (struct efx_ptp_match *)skb->cb;
	u8 *match_data_012, *match_data_345;
	unsigned int version;
	u8 *data;

	match->expiry = jiffies + msecs_to_jiffies(PKT_EVENT_LIFETIME_MS);

	/* Correct version? */
	if (ptp->mode == MC_CMD_PTP_MODE_V1) {
		if (!pskb_may_pull(skb, PTP_V1_MIN_LENGTH)) {
			return false;
		}
		data = skb->data;
		version = ntohs(*(__be16 *)&data[PTP_V1_VERSION_OFFSET]);
		if (version != PTP_VERSION_V1) {
			return false;
		}

		/* PTP V1 uses all six bytes of the UUID to match the packet
		 * to the timestamp
		 */
		match_data_012 = data + PTP_V1_UUID_OFFSET;
		match_data_345 = data + PTP_V1_UUID_OFFSET + 3;
	} else {
		if (!pskb_may_pull(skb, PTP_V2_MIN_LENGTH)) {
			return false;
		}
		data = skb->data;
		version = data[PTP_V2_VERSION_OFFSET];
		if ((version & PTP_VERSION_V2_MASK) != PTP_VERSION_V2) {
			return false;
		}

		/* The original V2 implementation uses bytes 2-7 of
		 * the UUID to match the packet to the timestamp. This
		 * discards two of the bytes of the MAC address used
		 * to create the UUID (SF bug 33070).  The PTP V2
		 * enhanced mode fixes this issue and uses bytes 0-2
		 * and byte 5-7 of the UUID.
		 */
		match_data_345 = data + PTP_V2_UUID_OFFSET + 5;
		if (ptp->mode == MC_CMD_PTP_MODE_V2) {
			match_data_012 = data + PTP_V2_UUID_OFFSET + 2;
		} else {
			match_data_012 = data + PTP_V2_UUID_OFFSET + 0;
			BUG_ON(ptp->mode != MC_CMD_PTP_MODE_V2_ENHANCED);
		}
	}

	/* Does this packet require timestamping? */
	if (ntohs(*(__be16 *)&data[PTP_DPORT_OFFSET]) == PTP_EVENT_PORT) {
		match->state = PTP_PACKET_STATE_UNMATCHED;

		/* We expect the sequence number to be in the same position in
		 * the packet for PTP V1 and V2
		 */
		BUILD_BUG_ON(PTP_V1_SEQUENCE_OFFSET != PTP_V2_SEQUENCE_OFFSET);
		BUILD_BUG_ON(PTP_V1_SEQUENCE_LENGTH != PTP_V2_SEQUENCE_LENGTH);

		/* Extract UUID/Sequence information */
		match->words[0] = (match_data_012[0]         |
				   (match_data_012[1] << 8)  |
				   (match_data_012[2] << 16) |
				   (match_data_345[0] << 24));
		match->words[1] = (match_data_345[1]         |
				   (match_data_345[2] << 8)  |
				   (data[PTP_V1_SEQUENCE_OFFSET +
					 PTP_V1_SEQUENCE_LENGTH - 1] <<
				    16));
	} else {
		match->state = PTP_PACKET_STATE_MATCH_UNWANTED;
	}

	skb_queue_tail(&ptp->rxq, skb);
	queue_work(ptp->workwq, &ptp->work);

	return true;
}

/* Transmit a PTP packet.  This has to be transmitted by the MC
 * itself, through an MCDI call.  MCDI calls aren't permitted
 * in the transmit path so defer the actual transmission to a suitable worker.
 */
int efx_siena_ptp_tx(struct efx_nic *efx, struct sk_buff *skb)
{
	struct efx_ptp_data *ptp = efx->ptp_data;

	skb_queue_tail(&ptp->txq, skb);

	if ((udp_hdr(skb)->dest == htons(PTP_EVENT_PORT)) &&
	    (skb->len <= MC_CMD_PTP_IN_TRANSMIT_PACKET_MAXNUM))
		efx_xmit_hwtstamp_pending(skb);
	queue_work(ptp->workwq, &ptp->work);

	return NETDEV_TX_OK;
}

int efx_siena_ptp_get_mode(struct efx_nic *efx)
{
	return efx->ptp_data->mode;
}

int efx_siena_ptp_change_mode(struct efx_nic *efx, bool enable_wanted,
			      unsigned int new_mode)
{
	if ((enable_wanted != efx->ptp_data->enabled) ||
	    (enable_wanted && (efx->ptp_data->mode != new_mode))) {
		int rc = 0;

		if (enable_wanted) {
			/* Change of mode requires disable */
			if (efx->ptp_data->enabled &&
			    (efx->ptp_data->mode != new_mode)) {
				efx->ptp_data->enabled = false;
				rc = efx_ptp_stop(efx);
				if (rc != 0)
					return rc;
			}

			/* Set new operating mode and establish
			 * baseline synchronisation, which must
			 * succeed.
			 */
			efx->ptp_data->mode = new_mode;
			if (netif_running(efx->net_dev))
				rc = efx_ptp_start(efx);
			if (rc == 0) {
				rc = efx_ptp_synchronize(efx,
							 PTP_SYNC_ATTEMPTS * 2);
				if (rc != 0)
					efx_ptp_stop(efx);
			}
		} else {
			rc = efx_ptp_stop(efx);
		}

		if (rc != 0)
			return rc;

		efx->ptp_data->enabled = enable_wanted;
	}

	return 0;
}

static int efx_ptp_ts_init(struct efx_nic *efx, struct hwtstamp_config *init)
{
	int rc;

	if ((init->tx_type != HWTSTAMP_TX_OFF) &&
	    (init->tx_type != HWTSTAMP_TX_ON))
		return -ERANGE;

	rc = efx->type->ptp_set_ts_config(efx, init);
	if (rc)
		return rc;

	efx->ptp_data->config = *init;
	return 0;
}

void efx_siena_ptp_get_ts_info(struct efx_nic *efx,
			       struct ethtool_ts_info *ts_info)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	struct efx_nic *primary = efx->primary;

	ASSERT_RTNL();

	if (!ptp)
		return;

	ts_info->so_timestamping |= (SOF_TIMESTAMPING_TX_HARDWARE |
				     SOF_TIMESTAMPING_RX_HARDWARE |
				     SOF_TIMESTAMPING_RAW_HARDWARE);
	if (primary && primary->ptp_data && primary->ptp_data->phc_clock)
		ts_info->phc_index =
			ptp_clock_index(primary->ptp_data->phc_clock);
	ts_info->tx_types = 1 << HWTSTAMP_TX_OFF | 1 << HWTSTAMP_TX_ON;
	ts_info->rx_filters = ptp->efx->type->hwtstamp_filters;
}

int efx_siena_ptp_set_ts_config(struct efx_nic *efx, struct ifreq *ifr)
{
	struct hwtstamp_config config;
	int rc;

	/* Not a PTP enabled port */
	if (!efx->ptp_data)
		return -EOPNOTSUPP;

	if (copy_from_user(&config, ifr->ifr_data, sizeof(config)))
		return -EFAULT;

	rc = efx_ptp_ts_init(efx, &config);
	if (rc != 0)
		return rc;

	return copy_to_user(ifr->ifr_data, &config, sizeof(config))
		? -EFAULT : 0;
}

int efx_siena_ptp_get_ts_config(struct efx_nic *efx, struct ifreq *ifr)
{
	if (!efx->ptp_data)
		return -EOPNOTSUPP;

	return copy_to_user(ifr->ifr_data, &efx->ptp_data->config,
			    sizeof(efx->ptp_data->config)) ? -EFAULT : 0;
}

static void ptp_event_failure(struct efx_nic *efx, int expected_frag_len)
{
	struct efx_ptp_data *ptp = efx->ptp_data;

	netif_err(efx, hw, efx->net_dev,
		"PTP unexpected event length: got %d expected %d\n",
		ptp->evt_frag_idx, expected_frag_len);
	ptp->reset_required = true;
	queue_work(ptp->workwq, &ptp->work);
}

/* Process a completed receive event.  Put it on the event queue and
 * start worker thread.  This is required because event and their
 * correspoding packets may come in either order.
 */
static void ptp_event_rx(struct efx_nic *efx, struct efx_ptp_data *ptp)
{
	struct efx_ptp_event_rx *evt = NULL;

	if (WARN_ON_ONCE(ptp->rx_ts_inline))
		return;

	if (ptp->evt_frag_idx != 3) {
		ptp_event_failure(efx, 3);
		return;
	}

	spin_lock_bh(&ptp->evt_lock);
	if (!list_empty(&ptp->evt_free_list)) {
		evt = list_first_entry(&ptp->evt_free_list,
				       struct efx_ptp_event_rx, link);
		list_del(&evt->link);

		evt->seq0 = EFX_QWORD_FIELD(ptp->evt_frags[2], MCDI_EVENT_DATA);
		evt->seq1 = (EFX_QWORD_FIELD(ptp->evt_frags[2],
					     MCDI_EVENT_SRC)        |
			     (EFX_QWORD_FIELD(ptp->evt_frags[1],
					      MCDI_EVENT_SRC) << 8) |
			     (EFX_QWORD_FIELD(ptp->evt_frags[0],
					      MCDI_EVENT_SRC) << 16));
		evt->hwtimestamp = efx->ptp_data->nic_to_kernel_time(
			EFX_QWORD_FIELD(ptp->evt_frags[0], MCDI_EVENT_DATA),
			EFX_QWORD_FIELD(ptp->evt_frags[1], MCDI_EVENT_DATA),
			ptp->ts_corrections.ptp_rx);
		evt->expiry = jiffies + msecs_to_jiffies(PKT_EVENT_LIFETIME_MS);
		list_add_tail(&evt->link, &ptp->evt_list);

		queue_work(ptp->workwq, &ptp->work);
	} else if (net_ratelimit()) {
		/* Log a rate-limited warning message. */
		netif_err(efx, rx_err, efx->net_dev, "PTP event queue overflow\n");
	}
	spin_unlock_bh(&ptp->evt_lock);
}

static void ptp_event_fault(struct efx_nic *efx, struct efx_ptp_data *ptp)
{
	int code = EFX_QWORD_FIELD(ptp->evt_frags[0], MCDI_EVENT_DATA);
	if (ptp->evt_frag_idx != 1) {
		ptp_event_failure(efx, 1);
		return;
	}

	netif_err(efx, hw, efx->net_dev, "PTP error %d\n", code);
}

static void ptp_event_pps(struct efx_nic *efx, struct efx_ptp_data *ptp)
{
	if (ptp->nic_ts_enabled)
		queue_work(ptp->pps_workwq, &ptp->pps_work);
}

void efx_siena_ptp_event(struct efx_nic *efx, efx_qword_t *ev)
{
	struct efx_ptp_data *ptp = efx->ptp_data;
	int code = EFX_QWORD_FIELD(*ev, MCDI_EVENT_CODE);

	if (!ptp) {
		if (!efx->ptp_warned) {
			netif_warn(efx, drv, efx->net_dev,
				   "Received PTP event but PTP not set up\n");
			efx->ptp_warned = true;
		}
		return;
	}

	if (!ptp->enabled)
		return;

	if (ptp->evt_frag_idx == 0) {
		ptp->evt_code = code;
	} else if (ptp->evt_code != code) {
		netif_err(efx, hw, efx->net_dev,
			  "PTP out of sequence event %d\n", code);
		ptp->evt_frag_idx = 0;
	}

	ptp->evt_frags[ptp->evt_frag_idx++] = *ev;
	if (!MCDI_EVENT_FIELD(*ev, CONT)) {
		/* Process resulting event */
		switch (code) {
		case MCDI_EVENT_CODE_PTP_RX:
			ptp_event_rx(efx, ptp);
			break;
		case MCDI_EVENT_CODE_PTP_FAULT:
			ptp_event_fault(efx, ptp);
			break;
		case MCDI_EVENT_CODE_PTP_PPS:
			ptp_event_pps(efx, ptp);
			break;
		default:
			netif_err(efx, hw, efx->net_dev,
				  "PTP unknown event %d\n", code);
			break;
		}
		ptp->evt_frag_idx = 0;
	} else if (MAX_EVENT_FRAGS == ptp->evt_frag_idx) {
		netif_err(efx, hw, efx->net_dev,
			  "PTP too many event fragments\n");
		ptp->evt_frag_idx = 0;
	}
}

void efx_siena_time_sync_event(struct efx_channel *channel, efx_qword_t *ev)
{
	struct efx_nic *efx = channel->efx;
	struct efx_ptp_data *ptp = efx->ptp_data;

	/* When extracting the sync timestamp minor value, we should discard
	 * the least significant two bits. These are not required in order
	 * to reconstruct full-range timestamps and they are optionally used
	 * to report status depending on the options supplied when subscribing
	 * for sync events.
	 */
	channel->sync_timestamp_major = MCDI_EVENT_FIELD(*ev, PTP_TIME_MAJOR);
	channel->sync_timestamp_minor =
		(MCDI_EVENT_FIELD(*ev, PTP_TIME_MINOR_MS_8BITS) & 0xFC)
			<< ptp->nic_time.sync_event_minor_shift;

	/* if sync events have been disabled then we want to silently ignore
	 * this event, so throw away result.
	 */
	(void) cmpxchg(&channel->sync_events_state, SYNC_EVENTS_REQUESTED,
		       SYNC_EVENTS_VALID);
}

static inline u32 efx_rx_buf_timestamp_minor(struct efx_nic *efx, const u8 *eh)
{
#if defined(CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS)
	return __le32_to_cpup((const __le32 *)(eh + efx->rx_packet_ts_offset));
#else
	const u8 *data = eh + efx->rx_packet_ts_offset;
	return (u32)data[0]       |
	       (u32)data[1] << 8  |
	       (u32)data[2] << 16 |
	       (u32)data[3] << 24;
#endif
}

void __efx_siena_rx_skb_attach_timestamp(struct efx_channel *channel,
					 struct sk_buff *skb)
{
	struct efx_nic *efx = channel->efx;
	struct efx_ptp_data *ptp = efx->ptp_data;
	u32 pkt_timestamp_major, pkt_timestamp_minor;
	u32 diff, carry;
	struct skb_shared_hwtstamps *timestamps;

	if (channel->sync_events_state != SYNC_EVENTS_VALID)
		return;

	pkt_timestamp_minor = efx_rx_buf_timestamp_minor(efx, skb_mac_header(skb));

	/* get the difference between the packet and sync timestamps,
	 * modulo one second
	 */
	diff = pkt_timestamp_minor - channel->sync_timestamp_minor;
	if (pkt_timestamp_minor < channel->sync_timestamp_minor)
		diff += ptp->nic_time.minor_max;

	/* do we roll over a second boundary and need to carry the one? */
	carry = (channel->sync_timestamp_minor >= ptp->nic_time.minor_max - diff) ?
		1 : 0;

	if (diff <= ptp->nic_time.sync_event_diff_max) {
		/* packet is ahead of the sync event by a quarter of a second or
		 * less (allowing for fuzz)
		 */
		pkt_timestamp_major = channel->sync_timestamp_major + carry;
	} else if (diff >= ptp->nic_time.sync_event_diff_min) {
		/* packet is behind the sync event but within the fuzz factor.
		 * This means the RX packet and sync event crossed as they were
		 * placed on the event queue, which can sometimes happen.
		 */
		pkt_timestamp_major = channel->sync_timestamp_major - 1 + carry;
	} else {
		/* it's outside tolerance in both directions. this might be
		 * indicative of us missing sync events for some reason, so
		 * we'll call it an error rather than risk giving a bogus
		 * timestamp.
		 */
		netif_vdbg(efx, drv, efx->net_dev,
			  "packet timestamp %x too far from sync event %x:%x\n",
			  pkt_timestamp_minor, channel->sync_timestamp_major,
			  channel->sync_timestamp_minor);
		return;
	}

	/* attach the timestamps to the skb */
	timestamps = skb_hwtstamps(skb);
	timestamps->hwtstamp =
		ptp->nic_to_kernel_time(pkt_timestamp_major,
					pkt_timestamp_minor,
					ptp->ts_corrections.general_rx);
}

static int efx_phc_adjfreq(struct ptp_clock_info *ptp, s32 delta)
{
	struct efx_ptp_data *ptp_data = container_of(ptp,
						     struct efx_ptp_data,
						     phc_clock_info);
	struct efx_nic *efx = ptp_data->efx;
	MCDI_DECLARE_BUF(inadj, MC_CMD_PTP_IN_ADJUST_LEN);
	s64 adjustment_ns;
	int rc;

	if (delta > MAX_PPB)
		delta = MAX_PPB;
	else if (delta < -MAX_PPB)
		delta = -MAX_PPB;

	/* Convert ppb to fixed point ns taking care to round correctly. */
	adjustment_ns = ((s64)delta * PPB_SCALE_WORD +
			 (1 << (ptp_data->adjfreq_ppb_shift - 1))) >>
			ptp_data->adjfreq_ppb_shift;

	MCDI_SET_DWORD(inadj, PTP_IN_OP, MC_CMD_PTP_OP_ADJUST);
	MCDI_SET_DWORD(inadj, PTP_IN_PERIPH_ID, 0);
	MCDI_SET_QWORD(inadj, PTP_IN_ADJUST_FREQ, adjustment_ns);
	MCDI_SET_DWORD(inadj, PTP_IN_ADJUST_SECONDS, 0);
	MCDI_SET_DWORD(inadj, PTP_IN_ADJUST_NANOSECONDS, 0);
	rc = efx_siena_mcdi_rpc(efx, MC_CMD_PTP, inadj, sizeof(inadj),
				NULL, 0, NULL);
	if (rc != 0)
		return rc;

	ptp_data->current_adjfreq = adjustment_ns;
	return 0;
}

static int efx_phc_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	u32 nic_major, nic_minor;
	struct efx_ptp_data *ptp_data = container_of(ptp,
						     struct efx_ptp_data,
						     phc_clock_info);
	struct efx_nic *efx = ptp_data->efx;
	MCDI_DECLARE_BUF(inbuf, MC_CMD_PTP_IN_ADJUST_LEN);

	efx->ptp_data->ns_to_nic_time(delta, &nic_major, &nic_minor);

	MCDI_SET_DWORD(inbuf, PTP_IN_OP, MC_CMD_PTP_OP_ADJUST);
	MCDI_SET_DWORD(inbuf, PTP_IN_PERIPH_ID, 0);
	MCDI_SET_QWORD(inbuf, PTP_IN_ADJUST_FREQ, ptp_data->current_adjfreq);
	MCDI_SET_DWORD(inbuf, PTP_IN_ADJUST_MAJOR, nic_major);
	MCDI_SET_DWORD(inbuf, PTP_IN_ADJUST_MINOR, nic_minor);
	return efx_siena_mcdi_rpc(efx, MC_CMD_PTP, inbuf, sizeof(inbuf),
				  NULL, 0, NULL);
}

static int efx_phc_gettime(struct ptp_clock_info *ptp, struct timespec64 *ts)
{
	struct efx_ptp_data *ptp_data = container_of(ptp,
						     struct efx_ptp_data,
						     phc_clock_info);
	struct efx_nic *efx = ptp_data->efx;
	MCDI_DECLARE_BUF(inbuf, MC_CMD_PTP_IN_READ_NIC_TIME_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_PTP_OUT_READ_NIC_TIME_LEN);
	int rc;
	ktime_t kt;

	MCDI_SET_DWORD(inbuf, PTP_IN_OP, MC_CMD_PTP_OP_READ_NIC_TIME);
	MCDI_SET_DWORD(inbuf, PTP_IN_PERIPH_ID, 0);

	rc = efx_siena_mcdi_rpc(efx, MC_CMD_PTP, inbuf, sizeof(inbuf),
				outbuf, sizeof(outbuf), NULL);
	if (rc != 0)
		return rc;

	kt = ptp_data->nic_to_kernel_time(
		MCDI_DWORD(outbuf, PTP_OUT_READ_NIC_TIME_MAJOR),
		MCDI_DWORD(outbuf, PTP_OUT_READ_NIC_TIME_MINOR), 0);
	*ts = ktime_to_timespec64(kt);
	return 0;
}

static int efx_phc_settime(struct ptp_clock_info *ptp,
			   const struct timespec64 *e_ts)
{
	/* Get the current NIC time, efx_phc_gettime.
	 * Subtract from the desired time to get the offset
	 * call efx_phc_adjtime with the offset
	 */
	int rc;
	struct timespec64 time_now;
	struct timespec64 delta;

	rc = efx_phc_gettime(ptp, &time_now);
	if (rc != 0)
		return rc;

	delta = timespec64_sub(*e_ts, time_now);

	rc = efx_phc_adjtime(ptp, timespec64_to_ns(&delta));
	if (rc != 0)
		return rc;

	return 0;
}

static int efx_phc_enable(struct ptp_clock_info *ptp,
			  struct ptp_clock_request *request,
			  int enable)
{
	struct efx_ptp_data *ptp_data = container_of(ptp,
						     struct efx_ptp_data,
						     phc_clock_info);
	if (request->type != PTP_CLK_REQ_PPS)
		return -EOPNOTSUPP;

	ptp_data->nic_ts_enabled = !!enable;
	return 0;
}

static const struct efx_channel_type efx_ptp_channel_type = {
	.handle_no_channel	= efx_ptp_handle_no_channel,
	.pre_probe		= efx_ptp_probe_channel,
	.post_remove		= efx_ptp_remove_channel,
	.get_name		= efx_ptp_get_channel_name,
	/* no copy operation; there is no need to reallocate this channel */
	.receive_skb		= efx_ptp_rx,
	.want_txqs		= efx_ptp_want_txqs,
	.keep_eventq		= false,
};

void efx_siena_ptp_defer_probe_with_channel(struct efx_nic *efx)
{
	/* Check whether PTP is implemented on this NIC.  The DISABLE
	 * operation will succeed if and only if it is implemented.
	 */
	if (efx_ptp_disable(efx) == 0)
		efx->extra_channel_type[EFX_EXTRA_CHANNEL_PTP] =
			&efx_ptp_channel_type;
}

void efx_siena_ptp_start_datapath(struct efx_nic *efx)
{
	if (efx_ptp_restart(efx))
		netif_err(efx, drv, efx->net_dev, "Failed to restart PTP.\n");
	/* re-enable timestamping if it was previously enabled */
	if (efx->type->ptp_set_ts_sync_events)
		efx->type->ptp_set_ts_sync_events(efx, true, true);
}

void efx_siena_ptp_stop_datapath(struct efx_nic *efx)
{
	/* temporarily disable timestamping */
	if (efx->type->ptp_set_ts_sync_events)
		efx->type->ptp_set_ts_sync_events(efx, false, true);
	efx_ptp_stop(efx);
}
