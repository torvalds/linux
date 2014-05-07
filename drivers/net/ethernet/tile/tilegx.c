/*
 * Copyright 2012 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/moduleparam.h>
#include <linux/sched.h>
#include <linux/kernel.h>      /* printk() */
#include <linux/slab.h>        /* kmalloc() */
#include <linux/errno.h>       /* error codes */
#include <linux/types.h>       /* size_t */
#include <linux/interrupt.h>
#include <linux/in.h>
#include <linux/irq.h>
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/skbuff.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/hugetlb.h>
#include <linux/in6.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/ktime.h>
#include <linux/io.h>
#include <linux/ctype.h>
#include <linux/ip.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <linux/net_tstamp.h>
#include <linux/ptp_clock_kernel.h>

#include <asm/checksum.h>
#include <asm/homecache.h>
#include <gxio/mpipe.h>
#include <arch/sim.h>

/* Default transmit lockup timeout period, in jiffies. */
#define TILE_NET_TIMEOUT (5 * HZ)

/* The maximum number of distinct channels (idesc.channel is 5 bits). */
#define TILE_NET_CHANNELS 32

/* Maximum number of idescs to handle per "poll". */
#define TILE_NET_BATCH 128

/* Maximum number of packets to handle per "poll". */
#define TILE_NET_WEIGHT 64

/* Number of entries in each iqueue. */
#define IQUEUE_ENTRIES 512

/* Number of entries in each equeue. */
#define EQUEUE_ENTRIES 2048

/* Total header bytes per equeue slot.  Must be big enough for 2 bytes
 * of NET_IP_ALIGN alignment, plus 14 bytes (?) of L2 header, plus up to
 * 60 bytes of actual TCP header.  We round up to align to cache lines.
 */
#define HEADER_BYTES 128

/* Maximum completions per cpu per device (must be a power of two).
 * ISSUE: What is the right number here?  If this is too small, then
 * egress might block waiting for free space in a completions array.
 * ISSUE: At the least, allocate these only for initialized echannels.
 */
#define TILE_NET_MAX_COMPS 64

#define MAX_FRAGS (MAX_SKB_FRAGS + 1)

/* The "kinds" of buffer stacks (small/large/jumbo). */
#define MAX_KINDS 3

/* Size of completions data to allocate.
 * ISSUE: Probably more than needed since we don't use all the channels.
 */
#define COMPS_SIZE (TILE_NET_CHANNELS * sizeof(struct tile_net_comps))

/* Size of NotifRing data to allocate. */
#define NOTIF_RING_SIZE (IQUEUE_ENTRIES * sizeof(gxio_mpipe_idesc_t))

/* Timeout to wake the per-device TX timer after we stop the queue.
 * We don't want the timeout too short (adds overhead, and might end
 * up causing stop/wake/stop/wake cycles) or too long (affects performance).
 * For the 10 Gb NIC, 30 usec means roughly 30+ 1500-byte packets.
 */
#define TX_TIMER_DELAY_USEC 30

/* Timeout to wake the per-cpu egress timer to free completions. */
#define EGRESS_TIMER_DELAY_USEC 1000

MODULE_AUTHOR("Tilera Corporation");
MODULE_LICENSE("GPL");

/* A "packet fragment" (a chunk of memory). */
struct frag {
	void *buf;
	size_t length;
};

/* A single completion. */
struct tile_net_comp {
	/* The "complete_count" when the completion will be complete. */
	s64 when;
	/* The buffer to be freed when the completion is complete. */
	struct sk_buff *skb;
};

/* The completions for a given cpu and echannel. */
struct tile_net_comps {
	/* The completions. */
	struct tile_net_comp comp_queue[TILE_NET_MAX_COMPS];
	/* The number of completions used. */
	unsigned long comp_next;
	/* The number of completions freed. */
	unsigned long comp_last;
};

/* The transmit wake timer for a given cpu and echannel. */
struct tile_net_tx_wake {
	int tx_queue_idx;
	struct hrtimer timer;
	struct net_device *dev;
};

/* Info for a specific cpu. */
struct tile_net_info {
	/* Our cpu. */
	int my_cpu;
	/* A timer for handling egress completions. */
	struct hrtimer egress_timer;
	/* True if "egress_timer" is scheduled. */
	bool egress_timer_scheduled;
	struct info_mpipe {
		/* Packet queue. */
		gxio_mpipe_iqueue_t iqueue;
		/* The NAPI struct. */
		struct napi_struct napi;
		/* Number of buffers (by kind) which must still be provided. */
		unsigned int num_needed_buffers[MAX_KINDS];
		/* instance id. */
		int instance;
		/* True if iqueue is valid. */
		bool has_iqueue;
		/* NAPI flags. */
		bool napi_added;
		bool napi_enabled;
		/* Comps for each egress channel. */
		struct tile_net_comps *comps_for_echannel[TILE_NET_CHANNELS];
		/* Transmit wake timer for each egress channel. */
		struct tile_net_tx_wake tx_wake[TILE_NET_CHANNELS];
	} mpipe[NR_MPIPE_MAX];
};

/* Info for egress on a particular egress channel. */
struct tile_net_egress {
	/* The "equeue". */
	gxio_mpipe_equeue_t *equeue;
	/* The headers for TSO. */
	unsigned char *headers;
};

/* Info for a specific device. */
struct tile_net_priv {
	/* Our network device. */
	struct net_device *dev;
	/* The primary link. */
	gxio_mpipe_link_t link;
	/* The primary channel, if open, else -1. */
	int channel;
	/* The "loopify" egress link, if needed. */
	gxio_mpipe_link_t loopify_link;
	/* The "loopify" egress channel, if open, else -1. */
	int loopify_channel;
	/* The egress channel (channel or loopify_channel). */
	int echannel;
	/* mPIPE instance, 0 or 1. */
	int instance;
	/* The timestamp config. */
	struct hwtstamp_config stamp_cfg;
};

static struct mpipe_data {
	/* The ingress irq. */
	int ingress_irq;

	/* The "context" for all devices. */
	gxio_mpipe_context_t context;

	/* Egress info, indexed by "priv->echannel"
	 * (lazily created as needed).
	 */
	struct tile_net_egress
	egress_for_echannel[TILE_NET_CHANNELS];

	/* Devices currently associated with each channel.
	 * NOTE: The array entry can become NULL after ifconfig down, but
	 * we do not free the underlying net_device structures, so it is
	 * safe to use a pointer after reading it from this array.
	 */
	struct net_device
	*tile_net_devs_for_channel[TILE_NET_CHANNELS];

	/* The actual memory allocated for the buffer stacks. */
	void *buffer_stack_vas[MAX_KINDS];

	/* The amount of memory allocated for each buffer stack. */
	size_t buffer_stack_bytes[MAX_KINDS];

	/* The first buffer stack index
	 * (small = +0, large = +1, jumbo = +2).
	 */
	int first_buffer_stack;

	/* The buckets. */
	int first_bucket;
	int num_buckets;

	/* PTP-specific data. */
	struct ptp_clock *ptp_clock;
	struct ptp_clock_info caps;

	/* Lock for ptp accessors. */
	struct mutex ptp_lock;

} mpipe_data[NR_MPIPE_MAX] = {
	[0 ... (NR_MPIPE_MAX - 1)] {
		.ingress_irq = -1,
		.first_buffer_stack = -1,
		.first_bucket = -1,
		.num_buckets = 1
	}
};

/* A mutex for "tile_net_devs_for_channel". */
static DEFINE_MUTEX(tile_net_devs_for_channel_mutex);

/* The per-cpu info. */
static DEFINE_PER_CPU(struct tile_net_info, per_cpu_info);


/* The buffer size enums for each buffer stack.
 * See arch/tile/include/gxio/mpipe.h for the set of possible values.
 * We avoid the "10384" size because it can induce "false chaining"
 * on "cut-through" jumbo packets.
 */
static gxio_mpipe_buffer_size_enum_t buffer_size_enums[MAX_KINDS] = {
	GXIO_MPIPE_BUFFER_SIZE_128,
	GXIO_MPIPE_BUFFER_SIZE_1664,
	GXIO_MPIPE_BUFFER_SIZE_16384
};

/* Text value of tile_net.cpus if passed as a module parameter. */
static char *network_cpus_string;

/* The actual cpus in "network_cpus". */
static struct cpumask network_cpus_map;

/* If "tile_net.loopify=LINK" was specified, this is "LINK". */
static char *loopify_link_name;

/* If "tile_net.custom" was specified, this is true. */
static bool custom_flag;

/* If "tile_net.jumbo=NUM" was specified, this is "NUM". */
static uint jumbo_num;

/* Obtain mpipe instance from struct tile_net_priv given struct net_device. */
static inline int mpipe_instance(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	return priv->instance;
}

/* The "tile_net.cpus" argument specifies the cpus that are dedicated
 * to handle ingress packets.
 *
 * The parameter should be in the form "tile_net.cpus=m-n[,x-y]", where
 * m, n, x, y are integer numbers that represent the cpus that can be
 * neither a dedicated cpu nor a dataplane cpu.
 */
static bool network_cpus_init(void)
{
	char buf[1024];
	int rc;

	if (network_cpus_string == NULL)
		return false;

	rc = cpulist_parse_crop(network_cpus_string, &network_cpus_map);
	if (rc != 0) {
		pr_warn("tile_net.cpus=%s: malformed cpu list\n",
			network_cpus_string);
		return false;
	}

	/* Remove dedicated cpus. */
	cpumask_and(&network_cpus_map, &network_cpus_map, cpu_possible_mask);

	if (cpumask_empty(&network_cpus_map)) {
		pr_warn("Ignoring empty tile_net.cpus='%s'.\n",
			network_cpus_string);
		return false;
	}

	cpulist_scnprintf(buf, sizeof(buf), &network_cpus_map);
	pr_info("Linux network CPUs: %s\n", buf);
	return true;
}

module_param_named(cpus, network_cpus_string, charp, 0444);
MODULE_PARM_DESC(cpus, "cpulist of cores that handle network interrupts");

/* The "tile_net.loopify=LINK" argument causes the named device to
 * actually use "loop0" for ingress, and "loop1" for egress.  This
 * allows an app to sit between the actual link and linux, passing
 * (some) packets along to linux, and forwarding (some) packets sent
 * out by linux.
 */
module_param_named(loopify, loopify_link_name, charp, 0444);
MODULE_PARM_DESC(loopify, "name the device to use loop0/1 for ingress/egress");

/* The "tile_net.custom" argument causes us to ignore the "conventional"
 * classifier metadata, in particular, the "l2_offset".
 */
module_param_named(custom, custom_flag, bool, 0444);
MODULE_PARM_DESC(custom, "indicates a (heavily) customized classifier");

/* The "tile_net.jumbo" argument causes us to support "jumbo" packets,
 * and to allocate the given number of "jumbo" buffers.
 */
module_param_named(jumbo, jumbo_num, uint, 0444);
MODULE_PARM_DESC(jumbo, "the number of buffers to support jumbo packets");

/* Atomically update a statistics field.
 * Note that on TILE-Gx, this operation is fire-and-forget on the
 * issuing core (single-cycle dispatch) and takes only a few cycles
 * longer than a regular store when the request reaches the home cache.
 * No expensive bus management overhead is required.
 */
static void tile_net_stats_add(unsigned long value, unsigned long *field)
{
	BUILD_BUG_ON(sizeof(atomic_long_t) != sizeof(unsigned long));
	atomic_long_add(value, (atomic_long_t *)field);
}

/* Allocate and push a buffer. */
static bool tile_net_provide_buffer(int instance, int kind)
{
	struct mpipe_data *md = &mpipe_data[instance];
	gxio_mpipe_buffer_size_enum_t bse = buffer_size_enums[kind];
	size_t bs = gxio_mpipe_buffer_size_enum_to_buffer_size(bse);
	const unsigned long buffer_alignment = 128;
	struct sk_buff *skb;
	int len;

	len = sizeof(struct sk_buff **) + buffer_alignment + bs;
	skb = dev_alloc_skb(len);
	if (skb == NULL)
		return false;

	/* Make room for a back-pointer to 'skb' and guarantee alignment. */
	skb_reserve(skb, sizeof(struct sk_buff **));
	skb_reserve(skb, -(long)skb->data & (buffer_alignment - 1));

	/* Save a back-pointer to 'skb'. */
	*(struct sk_buff **)(skb->data - sizeof(struct sk_buff **)) = skb;

	/* Make sure "skb" and the back-pointer have been flushed. */
	wmb();

	gxio_mpipe_push_buffer(&md->context, md->first_buffer_stack + kind,
			       (void *)va_to_tile_io_addr(skb->data));

	return true;
}

/* Convert a raw mpipe buffer to its matching skb pointer. */
static struct sk_buff *mpipe_buf_to_skb(void *va)
{
	/* Acquire the associated "skb". */
	struct sk_buff **skb_ptr = va - sizeof(*skb_ptr);
	struct sk_buff *skb = *skb_ptr;

	/* Paranoia. */
	if (skb->data != va) {
		/* Panic here since there's a reasonable chance
		 * that corrupt buffers means generic memory
		 * corruption, with unpredictable system effects.
		 */
		panic("Corrupt linux buffer! va=%p, skb=%p, skb->data=%p",
		      va, skb, skb->data);
	}

	return skb;
}

static void tile_net_pop_all_buffers(int instance, int stack)
{
	struct mpipe_data *md = &mpipe_data[instance];

	for (;;) {
		tile_io_addr_t addr =
			(tile_io_addr_t)gxio_mpipe_pop_buffer(&md->context,
							      stack);
		if (addr == 0)
			break;
		dev_kfree_skb_irq(mpipe_buf_to_skb(tile_io_addr_to_va(addr)));
	}
}

/* Provide linux buffers to mPIPE. */
static void tile_net_provide_needed_buffers(void)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	int instance, kind;
	for (instance = 0; instance < NR_MPIPE_MAX &&
		     info->mpipe[instance].has_iqueue; instance++)	{
		for (kind = 0; kind < MAX_KINDS; kind++) {
			while (info->mpipe[instance].num_needed_buffers[kind]
			       != 0) {
				if (!tile_net_provide_buffer(instance, kind)) {
					pr_notice("Tile %d still needs"
						  " some buffers\n",
						  info->my_cpu);
					return;
				}
				info->mpipe[instance].
					num_needed_buffers[kind]--;
			}
		}
	}
}

/* Get RX timestamp, and store it in the skb. */
static void tile_rx_timestamp(struct tile_net_priv *priv, struct sk_buff *skb,
			      gxio_mpipe_idesc_t *idesc)
{
	if (unlikely(priv->stamp_cfg.rx_filter != HWTSTAMP_FILTER_NONE)) {
		struct skb_shared_hwtstamps *shhwtstamps = skb_hwtstamps(skb);
		memset(shhwtstamps, 0, sizeof(*shhwtstamps));
		shhwtstamps->hwtstamp = ktime_set(idesc->time_stamp_sec,
						  idesc->time_stamp_ns);
	}
}

/* Get TX timestamp, and store it in the skb. */
static void tile_tx_timestamp(struct sk_buff *skb, int instance)
{
	struct skb_shared_info *shtx = skb_shinfo(skb);
	if (unlikely((shtx->tx_flags & SKBTX_HW_TSTAMP) != 0)) {
		struct mpipe_data *md = &mpipe_data[instance];
		struct skb_shared_hwtstamps shhwtstamps;
		struct timespec ts;

		shtx->tx_flags |= SKBTX_IN_PROGRESS;
		gxio_mpipe_get_timestamp(&md->context, &ts);
		memset(&shhwtstamps, 0, sizeof(shhwtstamps));
		shhwtstamps.hwtstamp = ktime_set(ts.tv_sec, ts.tv_nsec);
		skb_tstamp_tx(skb, &shhwtstamps);
	}
}

/* Use ioctl() to enable or disable TX or RX timestamping. */
static int tile_hwtstamp_set(struct net_device *dev, struct ifreq *rq)
{
	struct hwtstamp_config config;
	struct tile_net_priv *priv = netdev_priv(dev);

	if (copy_from_user(&config, rq->ifr_data, sizeof(config)))
		return -EFAULT;

	if (config.flags)  /* reserved for future extensions */
		return -EINVAL;

	switch (config.tx_type) {
	case HWTSTAMP_TX_OFF:
	case HWTSTAMP_TX_ON:
		break;
	default:
		return -ERANGE;
	}

	switch (config.rx_filter) {
	case HWTSTAMP_FILTER_NONE:
		break;
	case HWTSTAMP_FILTER_ALL:
	case HWTSTAMP_FILTER_SOME:
	case HWTSTAMP_FILTER_PTP_V1_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V1_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V1_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L4_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L4_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L4_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_L2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_L2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_L2_DELAY_REQ:
	case HWTSTAMP_FILTER_PTP_V2_EVENT:
	case HWTSTAMP_FILTER_PTP_V2_SYNC:
	case HWTSTAMP_FILTER_PTP_V2_DELAY_REQ:
		config.rx_filter = HWTSTAMP_FILTER_ALL;
		break;
	default:
		return -ERANGE;
	}

	if (copy_to_user(rq->ifr_data, &config, sizeof(config)))
		return -EFAULT;

	priv->stamp_cfg = config;
	return 0;
}

static int tile_hwtstamp_get(struct net_device *dev, struct ifreq *rq)
{
	struct tile_net_priv *priv = netdev_priv(dev);

	if (copy_to_user(rq->ifr_data, &priv->stamp_cfg,
			 sizeof(priv->stamp_cfg)))
		return -EFAULT;

	return 0;
}

static inline bool filter_packet(struct net_device *dev, void *buf)
{
	/* Filter packets received before we're up. */
	if (dev == NULL || !(dev->flags & IFF_UP))
		return true;

	/* Filter out packets that aren't for us. */
	if (!(dev->flags & IFF_PROMISC) &&
	    !is_multicast_ether_addr(buf) &&
	    !ether_addr_equal(dev->dev_addr, buf))
		return true;

	return false;
}

static void tile_net_receive_skb(struct net_device *dev, struct sk_buff *skb,
				 gxio_mpipe_idesc_t *idesc, unsigned long len)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	struct tile_net_priv *priv = netdev_priv(dev);
	int instance = priv->instance;

	/* Encode the actual packet length. */
	skb_put(skb, len);

	skb->protocol = eth_type_trans(skb, dev);

	/* Acknowledge "good" hardware checksums. */
	if (idesc->cs && idesc->csum_seed_val == 0xFFFF)
		skb->ip_summed = CHECKSUM_UNNECESSARY;

	/* Get RX timestamp from idesc. */
	tile_rx_timestamp(priv, skb, idesc);

	napi_gro_receive(&info->mpipe[instance].napi, skb);

	/* Update stats. */
	tile_net_stats_add(1, &dev->stats.rx_packets);
	tile_net_stats_add(len, &dev->stats.rx_bytes);

	/* Need a new buffer. */
	if (idesc->size == buffer_size_enums[0])
		info->mpipe[instance].num_needed_buffers[0]++;
	else if (idesc->size == buffer_size_enums[1])
		info->mpipe[instance].num_needed_buffers[1]++;
	else
		info->mpipe[instance].num_needed_buffers[2]++;
}

/* Handle a packet.  Return true if "processed", false if "filtered". */
static bool tile_net_handle_packet(int instance, gxio_mpipe_idesc_t *idesc)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	struct mpipe_data *md = &mpipe_data[instance];
	struct net_device *dev = md->tile_net_devs_for_channel[idesc->channel];
	uint8_t l2_offset;
	void *va;
	void *buf;
	unsigned long len;
	bool filter;

	/* Drop packets for which no buffer was available (which can
	 * happen under heavy load), or for which the me/tr/ce flags
	 * are set (which can happen for jumbo cut-through packets,
	 * or with a customized classifier).
	 */
	if (idesc->be || idesc->me || idesc->tr || idesc->ce) {
		if (dev)
			tile_net_stats_add(1, &dev->stats.rx_errors);
		goto drop;
	}

	/* Get the "l2_offset", if allowed. */
	l2_offset = custom_flag ? 0 : gxio_mpipe_idesc_get_l2_offset(idesc);

	/* Get the VA (including NET_IP_ALIGN bytes of "headroom"). */
	va = tile_io_addr_to_va((unsigned long)idesc->va);

	/* Get the actual packet start/length. */
	buf = va + l2_offset;
	len = idesc->l2_size - l2_offset;

	/* Point "va" at the raw buffer. */
	va -= NET_IP_ALIGN;

	filter = filter_packet(dev, buf);
	if (filter) {
		if (dev)
			tile_net_stats_add(1, &dev->stats.rx_dropped);
drop:
		gxio_mpipe_iqueue_drop(&info->mpipe[instance].iqueue, idesc);
	} else {
		struct sk_buff *skb = mpipe_buf_to_skb(va);

		/* Skip headroom, and any custom header. */
		skb_reserve(skb, NET_IP_ALIGN + l2_offset);

		tile_net_receive_skb(dev, skb, idesc, len);
	}

	gxio_mpipe_iqueue_consume(&info->mpipe[instance].iqueue, idesc);
	return !filter;
}

/* Handle some packets for the current CPU.
 *
 * This function handles up to TILE_NET_BATCH idescs per call.
 *
 * ISSUE: Since we do not provide new buffers until this function is
 * complete, we must initially provide enough buffers for each network
 * cpu to fill its iqueue and also its batched idescs.
 *
 * ISSUE: The "rotting packet" race condition occurs if a packet
 * arrives after the queue appears to be empty, and before the
 * hypervisor interrupt is re-enabled.
 */
static int tile_net_poll(struct napi_struct *napi, int budget)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	unsigned int work = 0;
	gxio_mpipe_idesc_t *idesc;
	int instance, i, n;
	struct mpipe_data *md;
	struct info_mpipe *info_mpipe =
		container_of(napi, struct info_mpipe, napi);

	if (budget <= 0)
		goto done;

	instance = info_mpipe->instance;
	while ((n = gxio_mpipe_iqueue_try_peek(
			&info_mpipe->iqueue,
			&idesc)) > 0) {
		for (i = 0; i < n; i++) {
			if (i == TILE_NET_BATCH)
				goto done;
			if (tile_net_handle_packet(instance,
						   idesc + i)) {
				if (++work >= budget)
					goto done;
			}
		}
	}

	/* There are no packets left. */
	napi_complete(&info_mpipe->napi);

	md = &mpipe_data[instance];
	/* Re-enable hypervisor interrupts. */
	gxio_mpipe_enable_notif_ring_interrupt(
		&md->context, info->mpipe[instance].iqueue.ring);

	/* HACK: Avoid the "rotting packet" problem. */
	if (gxio_mpipe_iqueue_try_peek(&info_mpipe->iqueue, &idesc) > 0)
		napi_schedule(&info_mpipe->napi);

	/* ISSUE: Handle completions? */

done:
	tile_net_provide_needed_buffers();

	return work;
}

/* Handle an ingress interrupt from an instance on the current cpu. */
static irqreturn_t tile_net_handle_ingress_irq(int irq, void *id)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	napi_schedule(&info->mpipe[(uint64_t)id].napi);
	return IRQ_HANDLED;
}

/* Free some completions.  This must be called with interrupts blocked. */
static int tile_net_free_comps(gxio_mpipe_equeue_t *equeue,
				struct tile_net_comps *comps,
				int limit, bool force_update)
{
	int n = 0;
	while (comps->comp_last < comps->comp_next) {
		unsigned int cid = comps->comp_last % TILE_NET_MAX_COMPS;
		struct tile_net_comp *comp = &comps->comp_queue[cid];
		if (!gxio_mpipe_equeue_is_complete(equeue, comp->when,
						   force_update || n == 0))
			break;
		dev_kfree_skb_irq(comp->skb);
		comps->comp_last++;
		if (++n == limit)
			break;
	}
	return n;
}

/* Add a completion.  This must be called with interrupts blocked.
 * tile_net_equeue_try_reserve() will have ensured a free completion entry.
 */
static void add_comp(gxio_mpipe_equeue_t *equeue,
		     struct tile_net_comps *comps,
		     uint64_t when, struct sk_buff *skb)
{
	int cid = comps->comp_next % TILE_NET_MAX_COMPS;
	comps->comp_queue[cid].when = when;
	comps->comp_queue[cid].skb = skb;
	comps->comp_next++;
}

static void tile_net_schedule_tx_wake_timer(struct net_device *dev,
                                            int tx_queue_idx)
{
	struct tile_net_info *info = &per_cpu(per_cpu_info, tx_queue_idx);
	struct tile_net_priv *priv = netdev_priv(dev);
	int instance = priv->instance;
	struct tile_net_tx_wake *tx_wake =
		&info->mpipe[instance].tx_wake[priv->echannel];

	hrtimer_start(&tx_wake->timer,
		      ktime_set(0, TX_TIMER_DELAY_USEC * 1000UL),
		      HRTIMER_MODE_REL_PINNED);
}

static enum hrtimer_restart tile_net_handle_tx_wake_timer(struct hrtimer *t)
{
	struct tile_net_tx_wake *tx_wake =
		container_of(t, struct tile_net_tx_wake, timer);
	netif_wake_subqueue(tx_wake->dev, tx_wake->tx_queue_idx);
	return HRTIMER_NORESTART;
}

/* Make sure the egress timer is scheduled. */
static void tile_net_schedule_egress_timer(void)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);

	if (!info->egress_timer_scheduled) {
		hrtimer_start(&info->egress_timer,
			      ktime_set(0, EGRESS_TIMER_DELAY_USEC * 1000UL),
			      HRTIMER_MODE_REL_PINNED);
		info->egress_timer_scheduled = true;
	}
}

/* The "function" for "info->egress_timer".
 *
 * This timer will reschedule itself as long as there are any pending
 * completions expected for this tile.
 */
static enum hrtimer_restart tile_net_handle_egress_timer(struct hrtimer *t)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	unsigned long irqflags;
	bool pending = false;
	int i, instance;

	local_irq_save(irqflags);

	/* The timer is no longer scheduled. */
	info->egress_timer_scheduled = false;

	/* Free all possible comps for this tile. */
	for (instance = 0; instance < NR_MPIPE_MAX &&
		     info->mpipe[instance].has_iqueue; instance++) {
		for (i = 0; i < TILE_NET_CHANNELS; i++) {
			struct tile_net_egress *egress =
				&mpipe_data[instance].egress_for_echannel[i];
			struct tile_net_comps *comps =
				info->mpipe[instance].comps_for_echannel[i];
			if (!egress || comps->comp_last >= comps->comp_next)
				continue;
			tile_net_free_comps(egress->equeue, comps, -1, true);
			pending = pending ||
				(comps->comp_last < comps->comp_next);
		}
	}

	/* Reschedule timer if needed. */
	if (pending)
		tile_net_schedule_egress_timer();

	local_irq_restore(irqflags);

	return HRTIMER_NORESTART;
}

/* PTP clock operations. */

static int ptp_mpipe_adjfreq(struct ptp_clock_info *ptp, s32 ppb)
{
	int ret = 0;
	struct mpipe_data *md = container_of(ptp, struct mpipe_data, caps);
	mutex_lock(&md->ptp_lock);
	if (gxio_mpipe_adjust_timestamp_freq(&md->context, ppb))
		ret = -EINVAL;
	mutex_unlock(&md->ptp_lock);
	return ret;
}

static int ptp_mpipe_adjtime(struct ptp_clock_info *ptp, s64 delta)
{
	int ret = 0;
	struct mpipe_data *md = container_of(ptp, struct mpipe_data, caps);
	mutex_lock(&md->ptp_lock);
	if (gxio_mpipe_adjust_timestamp(&md->context, delta))
		ret = -EBUSY;
	mutex_unlock(&md->ptp_lock);
	return ret;
}

static int ptp_mpipe_gettime(struct ptp_clock_info *ptp, struct timespec *ts)
{
	int ret = 0;
	struct mpipe_data *md = container_of(ptp, struct mpipe_data, caps);
	mutex_lock(&md->ptp_lock);
	if (gxio_mpipe_get_timestamp(&md->context, ts))
		ret = -EBUSY;
	mutex_unlock(&md->ptp_lock);
	return ret;
}

static int ptp_mpipe_settime(struct ptp_clock_info *ptp,
			     const struct timespec *ts)
{
	int ret = 0;
	struct mpipe_data *md = container_of(ptp, struct mpipe_data, caps);
	mutex_lock(&md->ptp_lock);
	if (gxio_mpipe_set_timestamp(&md->context, ts))
		ret = -EBUSY;
	mutex_unlock(&md->ptp_lock);
	return ret;
}

static int ptp_mpipe_enable(struct ptp_clock_info *ptp,
			    struct ptp_clock_request *request, int on)
{
	return -EOPNOTSUPP;
}

static struct ptp_clock_info ptp_mpipe_caps = {
	.owner		= THIS_MODULE,
	.name		= "mPIPE clock",
	.max_adj	= 999999999,
	.n_ext_ts	= 0,
	.n_pins		= 0,
	.pps		= 0,
	.adjfreq	= ptp_mpipe_adjfreq,
	.adjtime	= ptp_mpipe_adjtime,
	.gettime	= ptp_mpipe_gettime,
	.settime	= ptp_mpipe_settime,
	.enable		= ptp_mpipe_enable,
};

/* Sync mPIPE's timestamp up with Linux system time and register PTP clock. */
static void register_ptp_clock(struct net_device *dev, struct mpipe_data *md)
{
	struct timespec ts;

	getnstimeofday(&ts);
	gxio_mpipe_set_timestamp(&md->context, &ts);

	mutex_init(&md->ptp_lock);
	md->caps = ptp_mpipe_caps;
	md->ptp_clock = ptp_clock_register(&md->caps, NULL);
	if (IS_ERR(md->ptp_clock))
		netdev_err(dev, "ptp_clock_register failed %ld\n",
			   PTR_ERR(md->ptp_clock));
}

/* Initialize PTP fields in a new device. */
static void init_ptp_dev(struct tile_net_priv *priv)
{
	priv->stamp_cfg.rx_filter = HWTSTAMP_FILTER_NONE;
	priv->stamp_cfg.tx_type = HWTSTAMP_TX_OFF;
}

/* Helper functions for "tile_net_update()". */
static void enable_ingress_irq(void *irq)
{
	enable_percpu_irq((long)irq, 0);
}

static void disable_ingress_irq(void *irq)
{
	disable_percpu_irq((long)irq);
}

/* Helper function for tile_net_open() and tile_net_stop().
 * Always called under tile_net_devs_for_channel_mutex.
 */
static int tile_net_update(struct net_device *dev)
{
	static gxio_mpipe_rules_t rules;  /* too big to fit on the stack */
	bool saw_channel = false;
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];
	int channel;
	int rc;
	int cpu;

	saw_channel = false;
	gxio_mpipe_rules_init(&rules, &md->context);

	for (channel = 0; channel < TILE_NET_CHANNELS; channel++) {
		if (md->tile_net_devs_for_channel[channel] == NULL)
			continue;
		if (!saw_channel) {
			saw_channel = true;
			gxio_mpipe_rules_begin(&rules, md->first_bucket,
					       md->num_buckets, NULL);
			gxio_mpipe_rules_set_headroom(&rules, NET_IP_ALIGN);
		}
		gxio_mpipe_rules_add_channel(&rules, channel);
	}

	/* NOTE: This can fail if there is no classifier.
	 * ISSUE: Can anything else cause it to fail?
	 */
	rc = gxio_mpipe_rules_commit(&rules);
	if (rc != 0) {
		netdev_warn(dev, "gxio_mpipe_rules_commit: mpipe[%d] %d\n",
			    instance, rc);
		return -EIO;
	}

	/* Update all cpus, sequentially (to protect "netif_napi_add()").
	 * We use on_each_cpu to handle the IPI mask or unmask.
	 */
	if (!saw_channel)
		on_each_cpu(disable_ingress_irq,
			    (void *)(long)(md->ingress_irq), 1);
	for_each_online_cpu(cpu) {
		struct tile_net_info *info = &per_cpu(per_cpu_info, cpu);

		if (!info->mpipe[instance].has_iqueue)
			continue;
		if (saw_channel) {
			if (!info->mpipe[instance].napi_added) {
				netif_napi_add(dev, &info->mpipe[instance].napi,
					       tile_net_poll, TILE_NET_WEIGHT);
				info->mpipe[instance].napi_added = true;
			}
			if (!info->mpipe[instance].napi_enabled) {
				napi_enable(&info->mpipe[instance].napi);
				info->mpipe[instance].napi_enabled = true;
			}
		} else {
			if (info->mpipe[instance].napi_enabled) {
				napi_disable(&info->mpipe[instance].napi);
				info->mpipe[instance].napi_enabled = false;
			}
			/* FIXME: Drain the iqueue. */
		}
	}
	if (saw_channel)
		on_each_cpu(enable_ingress_irq,
			    (void *)(long)(md->ingress_irq), 1);

	/* HACK: Allow packets to flow in the simulator. */
	if (saw_channel)
		sim_enable_mpipe_links(instance, -1);

	return 0;
}

/* Initialize a buffer stack. */
static int create_buffer_stack(struct net_device *dev,
			       int kind, size_t num_buffers)
{
	pte_t hash_pte = pte_set_home((pte_t) { 0 }, PAGE_HOME_HASH);
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];
	size_t needed = gxio_mpipe_calc_buffer_stack_bytes(num_buffers);
	int stack_idx = md->first_buffer_stack + kind;
	void *va;
	int i, rc;

	/* Round up to 64KB and then use alloc_pages() so we get the
	 * required 64KB alignment.
	 */
	md->buffer_stack_bytes[kind] =
		ALIGN(needed, 64 * 1024);

	va = alloc_pages_exact(md->buffer_stack_bytes[kind], GFP_KERNEL);
	if (va == NULL) {
		netdev_err(dev,
			   "Could not alloc %zd bytes for buffer stack %d\n",
			   md->buffer_stack_bytes[kind], kind);
		return -ENOMEM;
	}

	/* Initialize the buffer stack. */
	rc = gxio_mpipe_init_buffer_stack(&md->context, stack_idx,
					  buffer_size_enums[kind],  va,
					  md->buffer_stack_bytes[kind], 0);
	if (rc != 0) {
		netdev_err(dev, "gxio_mpipe_init_buffer_stack: mpipe[%d] %d\n",
			   instance, rc);
		free_pages_exact(va, md->buffer_stack_bytes[kind]);
		return rc;
	}

	md->buffer_stack_vas[kind] = va;

	rc = gxio_mpipe_register_client_memory(&md->context, stack_idx,
					       hash_pte, 0);
	if (rc != 0) {
		netdev_err(dev,
			   "gxio_mpipe_register_client_memory: mpipe[%d] %d\n",
			   instance, rc);
		return rc;
	}

	/* Provide initial buffers. */
	for (i = 0; i < num_buffers; i++) {
		if (!tile_net_provide_buffer(instance, kind)) {
			netdev_err(dev, "Cannot allocate initial sk_bufs!\n");
			return -ENOMEM;
		}
	}

	return 0;
}

/* Allocate and initialize mpipe buffer stacks, and register them in
 * the mPIPE TLBs, for small, large, and (possibly) jumbo packet sizes.
 * This routine supports tile_net_init_mpipe(), below.
 */
static int init_buffer_stacks(struct net_device *dev,
			      int network_cpus_count)
{
	int num_kinds = MAX_KINDS - (jumbo_num == 0);
	size_t num_buffers;
	int rc;
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];

	/* Allocate the buffer stacks. */
	rc = gxio_mpipe_alloc_buffer_stacks(&md->context, num_kinds, 0, 0);
	if (rc < 0) {
		netdev_err(dev,
			   "gxio_mpipe_alloc_buffer_stacks: mpipe[%d] %d\n",
			   instance, rc);
		return rc;
	}
	md->first_buffer_stack = rc;

	/* Enough small/large buffers to (normally) avoid buffer errors. */
	num_buffers =
		network_cpus_count * (IQUEUE_ENTRIES + TILE_NET_BATCH);

	/* Allocate the small memory stack. */
	if (rc >= 0)
		rc = create_buffer_stack(dev, 0, num_buffers);

	/* Allocate the large buffer stack. */
	if (rc >= 0)
		rc = create_buffer_stack(dev, 1, num_buffers);

	/* Allocate the jumbo buffer stack if needed. */
	if (rc >= 0 && jumbo_num != 0)
		rc = create_buffer_stack(dev, 2, jumbo_num);

	return rc;
}

/* Allocate per-cpu resources (memory for completions and idescs).
 * This routine supports tile_net_init_mpipe(), below.
 */
static int alloc_percpu_mpipe_resources(struct net_device *dev,
					int cpu, int ring)
{
	struct tile_net_info *info = &per_cpu(per_cpu_info, cpu);
	int order, i, rc;
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];
	struct page *page;
	void *addr;

	/* Allocate the "comps". */
	order = get_order(COMPS_SIZE);
	page = homecache_alloc_pages(GFP_KERNEL, order, cpu);
	if (page == NULL) {
		netdev_err(dev, "Failed to alloc %zd bytes comps memory\n",
			   COMPS_SIZE);
		return -ENOMEM;
	}
	addr = pfn_to_kaddr(page_to_pfn(page));
	memset(addr, 0, COMPS_SIZE);
	for (i = 0; i < TILE_NET_CHANNELS; i++)
		info->mpipe[instance].comps_for_echannel[i] =
			addr + i * sizeof(struct tile_net_comps);

	/* If this is a network cpu, create an iqueue. */
	if (cpu_isset(cpu, network_cpus_map)) {
		order = get_order(NOTIF_RING_SIZE);
		page = homecache_alloc_pages(GFP_KERNEL, order, cpu);
		if (page == NULL) {
			netdev_err(dev,
				   "Failed to alloc %zd bytes iqueue memory\n",
				   NOTIF_RING_SIZE);
			return -ENOMEM;
		}
		addr = pfn_to_kaddr(page_to_pfn(page));
		rc = gxio_mpipe_iqueue_init(&info->mpipe[instance].iqueue,
					    &md->context, ring++, addr,
					    NOTIF_RING_SIZE, 0);
		if (rc < 0) {
			netdev_err(dev,
				   "gxio_mpipe_iqueue_init failed: %d\n", rc);
			return rc;
		}
		info->mpipe[instance].has_iqueue = true;
	}

	return ring;
}

/* Initialize NotifGroup and buckets.
 * This routine supports tile_net_init_mpipe(), below.
 */
static int init_notif_group_and_buckets(struct net_device *dev,
					int ring, int network_cpus_count)
{
	int group, rc;
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];

	/* Allocate one NotifGroup. */
	rc = gxio_mpipe_alloc_notif_groups(&md->context, 1, 0, 0);
	if (rc < 0) {
		netdev_err(dev, "gxio_mpipe_alloc_notif_groups: mpipe[%d] %d\n",
			   instance, rc);
		return rc;
	}
	group = rc;

	/* Initialize global num_buckets value. */
	if (network_cpus_count > 4)
		md->num_buckets = 256;
	else if (network_cpus_count > 1)
		md->num_buckets = 16;

	/* Allocate some buckets, and set global first_bucket value. */
	rc = gxio_mpipe_alloc_buckets(&md->context, md->num_buckets, 0, 0);
	if (rc < 0) {
		netdev_err(dev, "gxio_mpipe_alloc_buckets: mpipe[%d] %d\n",
			   instance, rc);
		return rc;
	}
	md->first_bucket = rc;

	/* Init group and buckets. */
	rc = gxio_mpipe_init_notif_group_and_buckets(
		&md->context, group, ring, network_cpus_count,
		md->first_bucket, md->num_buckets,
		GXIO_MPIPE_BUCKET_STICKY_FLOW_LOCALITY);
	if (rc != 0) {
		netdev_err(dev,	"gxio_mpipe_init_notif_group_and_buckets: "
			   "mpipe[%d] %d\n", instance, rc);
		return rc;
	}

	return 0;
}

/* Create an irq and register it, then activate the irq and request
 * interrupts on all cores.  Note that "ingress_irq" being initialized
 * is how we know not to call tile_net_init_mpipe() again.
 * This routine supports tile_net_init_mpipe(), below.
 */
static int tile_net_setup_interrupts(struct net_device *dev)
{
	int cpu, rc, irq;
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];

	irq = md->ingress_irq;
	if (irq < 0) {
		irq = irq_alloc_hwirq(-1);
		if (!irq) {
			netdev_err(dev,
				   "create_irq failed: mpipe[%d] %d\n",
				   instance, irq);
			return irq;
		}
		tile_irq_activate(irq, TILE_IRQ_PERCPU);

		rc = request_irq(irq, tile_net_handle_ingress_irq,
				 0, "tile_net", (void *)((uint64_t)instance));

		if (rc != 0) {
			netdev_err(dev, "request_irq failed: mpipe[%d] %d\n",
				   instance, rc);
			irq_free_hwirq(irq);
			return rc;
		}
		md->ingress_irq = irq;
	}

	for_each_online_cpu(cpu) {
		struct tile_net_info *info = &per_cpu(per_cpu_info, cpu);
		if (info->mpipe[instance].has_iqueue) {
			gxio_mpipe_request_notif_ring_interrupt(&md->context,
				cpu_x(cpu), cpu_y(cpu), KERNEL_PL, irq,
				info->mpipe[instance].iqueue.ring);
		}
	}

	return 0;
}

/* Undo any state set up partially by a failed call to tile_net_init_mpipe. */
static void tile_net_init_mpipe_fail(int instance)
{
	int kind, cpu;
	struct mpipe_data *md = &mpipe_data[instance];

	/* Do cleanups that require the mpipe context first. */
	for (kind = 0; kind < MAX_KINDS; kind++) {
		if (md->buffer_stack_vas[kind] != NULL) {
			tile_net_pop_all_buffers(instance,
						 md->first_buffer_stack +
						 kind);
		}
	}

	/* Destroy mpipe context so the hardware no longer owns any memory. */
	gxio_mpipe_destroy(&md->context);

	for_each_online_cpu(cpu) {
		struct tile_net_info *info = &per_cpu(per_cpu_info, cpu);
		free_pages(
			(unsigned long)(
				info->mpipe[instance].comps_for_echannel[0]),
			get_order(COMPS_SIZE));
		info->mpipe[instance].comps_for_echannel[0] = NULL;
		free_pages((unsigned long)(info->mpipe[instance].iqueue.idescs),
			   get_order(NOTIF_RING_SIZE));
		info->mpipe[instance].iqueue.idescs = NULL;
	}

	for (kind = 0; kind < MAX_KINDS; kind++) {
		if (md->buffer_stack_vas[kind] != NULL) {
			free_pages_exact(md->buffer_stack_vas[kind],
					 md->buffer_stack_bytes[kind]);
			md->buffer_stack_vas[kind] = NULL;
		}
	}

	md->first_buffer_stack = -1;
	md->first_bucket = -1;
}

/* The first time any tilegx network device is opened, we initialize
 * the global mpipe state.  If this step fails, we fail to open the
 * device, but if it succeeds, we never need to do it again, and since
 * tile_net can't be unloaded, we never undo it.
 *
 * Note that some resources in this path (buffer stack indices,
 * bindings from init_buffer_stack, etc.) are hypervisor resources
 * that are freed implicitly by gxio_mpipe_destroy().
 */
static int tile_net_init_mpipe(struct net_device *dev)
{
	int rc;
	int cpu;
	int first_ring, ring;
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];
	int network_cpus_count = cpus_weight(network_cpus_map);

	if (!hash_default) {
		netdev_err(dev, "Networking requires hash_default!\n");
		return -EIO;
	}

	rc = gxio_mpipe_init(&md->context, instance);
	if (rc != 0) {
		netdev_err(dev, "gxio_mpipe_init: mpipe[%d] %d\n",
			   instance, rc);
		return -EIO;
	}

	/* Set up the buffer stacks. */
	rc = init_buffer_stacks(dev, network_cpus_count);
	if (rc != 0)
		goto fail;

	/* Allocate one NotifRing for each network cpu. */
	rc = gxio_mpipe_alloc_notif_rings(&md->context,
					  network_cpus_count, 0, 0);
	if (rc < 0) {
		netdev_err(dev, "gxio_mpipe_alloc_notif_rings failed %d\n",
			   rc);
		goto fail;
	}

	/* Init NotifRings per-cpu. */
	first_ring = rc;
	ring = first_ring;
	for_each_online_cpu(cpu) {
		rc = alloc_percpu_mpipe_resources(dev, cpu, ring);
		if (rc < 0)
			goto fail;
		ring = rc;
	}

	/* Initialize NotifGroup and buckets. */
	rc = init_notif_group_and_buckets(dev, first_ring, network_cpus_count);
	if (rc != 0)
		goto fail;

	/* Create and enable interrupts. */
	rc = tile_net_setup_interrupts(dev);
	if (rc != 0)
		goto fail;

	/* Register PTP clock and set mPIPE timestamp, if configured. */
	register_ptp_clock(dev, md);

	return 0;

fail:
	tile_net_init_mpipe_fail(instance);
	return rc;
}

/* Create persistent egress info for a given egress channel.
 * Note that this may be shared between, say, "gbe0" and "xgbe0".
 * ISSUE: Defer header allocation until TSO is actually needed?
 */
static int tile_net_init_egress(struct net_device *dev, int echannel)
{
	static int ering = -1;
	struct page *headers_page, *edescs_page, *equeue_page;
	gxio_mpipe_edesc_t *edescs;
	gxio_mpipe_equeue_t *equeue;
	unsigned char *headers;
	int headers_order, edescs_order, equeue_order;
	size_t edescs_size;
	int rc = -ENOMEM;
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];

	/* Only initialize once. */
	if (md->egress_for_echannel[echannel].equeue != NULL)
		return 0;

	/* Allocate memory for the "headers". */
	headers_order = get_order(EQUEUE_ENTRIES * HEADER_BYTES);
	headers_page = alloc_pages(GFP_KERNEL, headers_order);
	if (headers_page == NULL) {
		netdev_warn(dev,
			    "Could not alloc %zd bytes for TSO headers.\n",
			    PAGE_SIZE << headers_order);
		goto fail;
	}
	headers = pfn_to_kaddr(page_to_pfn(headers_page));

	/* Allocate memory for the "edescs". */
	edescs_size = EQUEUE_ENTRIES * sizeof(*edescs);
	edescs_order = get_order(edescs_size);
	edescs_page = alloc_pages(GFP_KERNEL, edescs_order);
	if (edescs_page == NULL) {
		netdev_warn(dev,
			    "Could not alloc %zd bytes for eDMA ring.\n",
			    edescs_size);
		goto fail_headers;
	}
	edescs = pfn_to_kaddr(page_to_pfn(edescs_page));

	/* Allocate memory for the "equeue". */
	equeue_order = get_order(sizeof(*equeue));
	equeue_page = alloc_pages(GFP_KERNEL, equeue_order);
	if (equeue_page == NULL) {
		netdev_warn(dev,
			    "Could not alloc %zd bytes for equeue info.\n",
			    PAGE_SIZE << equeue_order);
		goto fail_edescs;
	}
	equeue = pfn_to_kaddr(page_to_pfn(equeue_page));

	/* Allocate an edma ring (using a one entry "free list"). */
	if (ering < 0) {
		rc = gxio_mpipe_alloc_edma_rings(&md->context, 1, 0, 0);
		if (rc < 0) {
			netdev_warn(dev, "gxio_mpipe_alloc_edma_rings: "
				    "mpipe[%d] %d\n", instance, rc);
			goto fail_equeue;
		}
		ering = rc;
	}

	/* Initialize the equeue. */
	rc = gxio_mpipe_equeue_init(equeue, &md->context, ering, echannel,
				    edescs, edescs_size, 0);
	if (rc != 0) {
		netdev_err(dev, "gxio_mpipe_equeue_init: mpipe[%d] %d\n",
			   instance, rc);
		goto fail_equeue;
	}

	/* Don't reuse the ering later. */
	ering = -1;

	if (jumbo_num != 0) {
		/* Make sure "jumbo" packets can be egressed safely. */
		if (gxio_mpipe_equeue_set_snf_size(equeue, 10368) < 0) {
			/* ISSUE: There is no "gxio_mpipe_equeue_destroy()". */
			netdev_warn(dev, "Jumbo packets may not be egressed"
				    " properly on channel %d\n", echannel);
		}
	}

	/* Done. */
	md->egress_for_echannel[echannel].equeue = equeue;
	md->egress_for_echannel[echannel].headers = headers;
	return 0;

fail_equeue:
	__free_pages(equeue_page, equeue_order);

fail_edescs:
	__free_pages(edescs_page, edescs_order);

fail_headers:
	__free_pages(headers_page, headers_order);

fail:
	return rc;
}

/* Return channel number for a newly-opened link. */
static int tile_net_link_open(struct net_device *dev, gxio_mpipe_link_t *link,
			      const char *link_name)
{
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];
	int rc = gxio_mpipe_link_open(link, &md->context, link_name, 0);
	if (rc < 0) {
		netdev_err(dev, "Failed to open '%s', mpipe[%d], %d\n",
			   link_name, instance, rc);
		return rc;
	}
	if (jumbo_num != 0) {
		u32 attr = GXIO_MPIPE_LINK_RECEIVE_JUMBO;
		rc = gxio_mpipe_link_set_attr(link, attr, 1);
		if (rc != 0) {
			netdev_err(dev,
				   "Cannot receive jumbo packets on '%s'\n",
				   link_name);
			gxio_mpipe_link_close(link);
			return rc;
		}
	}
	rc = gxio_mpipe_link_channel(link);
	if (rc < 0 || rc >= TILE_NET_CHANNELS) {
		netdev_err(dev, "gxio_mpipe_link_channel bad value: %d\n", rc);
		gxio_mpipe_link_close(link);
		return -EINVAL;
	}
	return rc;
}

/* Help the kernel activate the given network interface. */
static int tile_net_open(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	int cpu, rc, instance;

	mutex_lock(&tile_net_devs_for_channel_mutex);

	/* Get the instance info. */
	rc = gxio_mpipe_link_instance(dev->name);
	if (rc < 0 || rc >= NR_MPIPE_MAX) {
		mutex_unlock(&tile_net_devs_for_channel_mutex);
		return -EIO;
	}

	priv->instance = rc;
	instance = rc;
	if (!mpipe_data[rc].context.mmio_fast_base) {
		/* Do one-time initialization per instance the first time
		 * any device is opened.
		 */
		rc = tile_net_init_mpipe(dev);
		if (rc != 0)
			goto fail;
	}

	/* Determine if this is the "loopify" device. */
	if (unlikely((loopify_link_name != NULL) &&
		     !strcmp(dev->name, loopify_link_name))) {
		rc = tile_net_link_open(dev, &priv->link, "loop0");
		if (rc < 0)
			goto fail;
		priv->channel = rc;
		rc = tile_net_link_open(dev, &priv->loopify_link, "loop1");
		if (rc < 0)
			goto fail;
		priv->loopify_channel = rc;
		priv->echannel = rc;
	} else {
		rc = tile_net_link_open(dev, &priv->link, dev->name);
		if (rc < 0)
			goto fail;
		priv->channel = rc;
		priv->echannel = rc;
	}

	/* Initialize egress info (if needed).  Once ever, per echannel. */
	rc = tile_net_init_egress(dev, priv->echannel);
	if (rc != 0)
		goto fail;

	mpipe_data[instance].tile_net_devs_for_channel[priv->channel] = dev;

	rc = tile_net_update(dev);
	if (rc != 0)
		goto fail;

	mutex_unlock(&tile_net_devs_for_channel_mutex);

	/* Initialize the transmit wake timer for this device for each cpu. */
	for_each_online_cpu(cpu) {
		struct tile_net_info *info = &per_cpu(per_cpu_info, cpu);
		struct tile_net_tx_wake *tx_wake =
			&info->mpipe[instance].tx_wake[priv->echannel];

		hrtimer_init(&tx_wake->timer, CLOCK_MONOTONIC,
			     HRTIMER_MODE_REL);
		tx_wake->tx_queue_idx = cpu;
		tx_wake->timer.function = tile_net_handle_tx_wake_timer;
		tx_wake->dev = dev;
	}

	for_each_online_cpu(cpu)
		netif_start_subqueue(dev, cpu);
	netif_carrier_on(dev);
	return 0;

fail:
	if (priv->loopify_channel >= 0) {
		if (gxio_mpipe_link_close(&priv->loopify_link) != 0)
			netdev_warn(dev, "Failed to close loopify link!\n");
		priv->loopify_channel = -1;
	}
	if (priv->channel >= 0) {
		if (gxio_mpipe_link_close(&priv->link) != 0)
			netdev_warn(dev, "Failed to close link!\n");
		priv->channel = -1;
	}
	priv->echannel = -1;
	mpipe_data[instance].tile_net_devs_for_channel[priv->channel] =	NULL;
	mutex_unlock(&tile_net_devs_for_channel_mutex);

	/* Don't return raw gxio error codes to generic Linux. */
	return (rc > -512) ? rc : -EIO;
}

/* Help the kernel deactivate the given network interface. */
static int tile_net_stop(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	int cpu;
	int instance = priv->instance;
	struct mpipe_data *md = &mpipe_data[instance];

	for_each_online_cpu(cpu) {
		struct tile_net_info *info = &per_cpu(per_cpu_info, cpu);
		struct tile_net_tx_wake *tx_wake =
			&info->mpipe[instance].tx_wake[priv->echannel];

		hrtimer_cancel(&tx_wake->timer);
		netif_stop_subqueue(dev, cpu);
	}

	mutex_lock(&tile_net_devs_for_channel_mutex);
	md->tile_net_devs_for_channel[priv->channel] = NULL;
	(void)tile_net_update(dev);
	if (priv->loopify_channel >= 0) {
		if (gxio_mpipe_link_close(&priv->loopify_link) != 0)
			netdev_warn(dev, "Failed to close loopify link!\n");
		priv->loopify_channel = -1;
	}
	if (priv->channel >= 0) {
		if (gxio_mpipe_link_close(&priv->link) != 0)
			netdev_warn(dev, "Failed to close link!\n");
		priv->channel = -1;
	}
	priv->echannel = -1;
	mutex_unlock(&tile_net_devs_for_channel_mutex);

	return 0;
}

/* Determine the VA for a fragment. */
static inline void *tile_net_frag_buf(skb_frag_t *f)
{
	unsigned long pfn = page_to_pfn(skb_frag_page(f));
	return pfn_to_kaddr(pfn) + f->page_offset;
}

/* Acquire a completion entry and an egress slot, or if we can't,
 * stop the queue and schedule the tx_wake timer.
 */
static s64 tile_net_equeue_try_reserve(struct net_device *dev,
				       int tx_queue_idx,
				       struct tile_net_comps *comps,
				       gxio_mpipe_equeue_t *equeue,
				       int num_edescs)
{
	/* Try to acquire a completion entry. */
	if (comps->comp_next - comps->comp_last < TILE_NET_MAX_COMPS - 1 ||
	    tile_net_free_comps(equeue, comps, 32, false) != 0) {

		/* Try to acquire an egress slot. */
		s64 slot = gxio_mpipe_equeue_try_reserve(equeue, num_edescs);
		if (slot >= 0)
			return slot;

		/* Freeing some completions gives the equeue time to drain. */
		tile_net_free_comps(equeue, comps, TILE_NET_MAX_COMPS, false);

		slot = gxio_mpipe_equeue_try_reserve(equeue, num_edescs);
		if (slot >= 0)
			return slot;
	}

	/* Still nothing; give up and stop the queue for a short while. */
	netif_stop_subqueue(dev, tx_queue_idx);
	tile_net_schedule_tx_wake_timer(dev, tx_queue_idx);
	return -1;
}

/* Determine how many edesc's are needed for TSO.
 *
 * Sometimes, if "sendfile()" requires copying, we will be called with
 * "data" containing the header and payload, with "frags" being empty.
 * Sometimes, for example when using NFS over TCP, a single segment can
 * span 3 fragments.  This requires special care.
 */
static int tso_count_edescs(struct sk_buff *skb)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	unsigned int sh_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	unsigned int data_len = skb->len - sh_len;
	unsigned int p_len = sh->gso_size;
	long f_id = -1;    /* id of the current fragment */
	long f_size = skb_headlen(skb) - sh_len;  /* current fragment size */
	long f_used = 0;  /* bytes used from the current fragment */
	long n;            /* size of the current piece of payload */
	int num_edescs = 0;
	int segment;

	for (segment = 0; segment < sh->gso_segs; segment++) {

		unsigned int p_used = 0;

		/* One edesc for header and for each piece of the payload. */
		for (num_edescs++; p_used < p_len; num_edescs++) {

			/* Advance as needed. */
			while (f_used >= f_size) {
				f_id++;
				f_size = skb_frag_size(&sh->frags[f_id]);
				f_used = 0;
			}

			/* Use bytes from the current fragment. */
			n = p_len - p_used;
			if (n > f_size - f_used)
				n = f_size - f_used;
			f_used += n;
			p_used += n;
		}

		/* The last segment may be less than gso_size. */
		data_len -= p_len;
		if (data_len < p_len)
			p_len = data_len;
	}

	return num_edescs;
}

/* Prepare modified copies of the skbuff headers. */
static void tso_headers_prepare(struct sk_buff *skb, unsigned char *headers,
				s64 slot)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	struct iphdr *ih;
	struct ipv6hdr *ih6;
	struct tcphdr *th;
	unsigned int sh_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	unsigned int data_len = skb->len - sh_len;
	unsigned char *data = skb->data;
	unsigned int ih_off, th_off, p_len;
	unsigned int isum_seed, tsum_seed, seq;
	unsigned int uninitialized_var(id);
	int is_ipv6;
	long f_id = -1;    /* id of the current fragment */
	long f_size = skb_headlen(skb) - sh_len;  /* current fragment size */
	long f_used = 0;  /* bytes used from the current fragment */
	long n;            /* size of the current piece of payload */
	int segment;

	/* Locate original headers and compute various lengths. */
	is_ipv6 = skb_is_gso_v6(skb);
	if (is_ipv6) {
		ih6 = ipv6_hdr(skb);
		ih_off = skb_network_offset(skb);
	} else {
		ih = ip_hdr(skb);
		ih_off = skb_network_offset(skb);
		isum_seed = ((0xFFFF - ih->check) +
			     (0xFFFF - ih->tot_len) +
			     (0xFFFF - ih->id));
		id = ntohs(ih->id);
	}

	th = tcp_hdr(skb);
	th_off = skb_transport_offset(skb);
	p_len = sh->gso_size;

	tsum_seed = th->check + (0xFFFF ^ htons(skb->len));
	seq = ntohl(th->seq);

	/* Prepare all the headers. */
	for (segment = 0; segment < sh->gso_segs; segment++) {
		unsigned char *buf;
		unsigned int p_used = 0;

		/* Copy to the header memory for this segment. */
		buf = headers + (slot % EQUEUE_ENTRIES) * HEADER_BYTES +
			NET_IP_ALIGN;
		memcpy(buf, data, sh_len);

		/* Update copied ip header. */
		if (is_ipv6) {
			ih6 = (struct ipv6hdr *)(buf + ih_off);
			ih6->payload_len = htons(sh_len + p_len - ih_off -
						 sizeof(*ih6));
		} else {
			ih = (struct iphdr *)(buf + ih_off);
			ih->tot_len = htons(sh_len + p_len - ih_off);
			ih->id = htons(id++);
			ih->check = csum_long(isum_seed + ih->tot_len +
					      ih->id) ^ 0xffff;
		}

		/* Update copied tcp header. */
		th = (struct tcphdr *)(buf + th_off);
		th->seq = htonl(seq);
		th->check = csum_long(tsum_seed + htons(sh_len + p_len));
		if (segment != sh->gso_segs - 1) {
			th->fin = 0;
			th->psh = 0;
		}

		/* Skip past the header. */
		slot++;

		/* Skip past the payload. */
		while (p_used < p_len) {

			/* Advance as needed. */
			while (f_used >= f_size) {
				f_id++;
				f_size = skb_frag_size(&sh->frags[f_id]);
				f_used = 0;
			}

			/* Use bytes from the current fragment. */
			n = p_len - p_used;
			if (n > f_size - f_used)
				n = f_size - f_used;
			f_used += n;
			p_used += n;

			slot++;
		}

		seq += p_len;

		/* The last segment may be less than gso_size. */
		data_len -= p_len;
		if (data_len < p_len)
			p_len = data_len;
	}

	/* Flush the headers so they are ready for hardware DMA. */
	wmb();
}

/* Pass all the data to mpipe for egress. */
static void tso_egress(struct net_device *dev, gxio_mpipe_equeue_t *equeue,
		       struct sk_buff *skb, unsigned char *headers, s64 slot)
{
	struct skb_shared_info *sh = skb_shinfo(skb);
	int instance = mpipe_instance(dev);
	struct mpipe_data *md = &mpipe_data[instance];
	unsigned int sh_len = skb_transport_offset(skb) + tcp_hdrlen(skb);
	unsigned int data_len = skb->len - sh_len;
	unsigned int p_len = sh->gso_size;
	gxio_mpipe_edesc_t edesc_head = { { 0 } };
	gxio_mpipe_edesc_t edesc_body = { { 0 } };
	long f_id = -1;    /* id of the current fragment */
	long f_size = skb_headlen(skb) - sh_len;  /* current fragment size */
	long f_used = 0;  /* bytes used from the current fragment */
	void *f_data = skb->data + sh_len;
	long n;            /* size of the current piece of payload */
	unsigned long tx_packets = 0, tx_bytes = 0;
	unsigned int csum_start;
	int segment;

	/* Prepare to egress the headers: set up header edesc. */
	csum_start = skb_checksum_start_offset(skb);
	edesc_head.csum = 1;
	edesc_head.csum_start = csum_start;
	edesc_head.csum_dest = csum_start + skb->csum_offset;
	edesc_head.xfer_size = sh_len;

	/* This is only used to specify the TLB. */
	edesc_head.stack_idx = md->first_buffer_stack;
	edesc_body.stack_idx = md->first_buffer_stack;

	/* Egress all the edescs. */
	for (segment = 0; segment < sh->gso_segs; segment++) {
		unsigned char *buf;
		unsigned int p_used = 0;

		/* Egress the header. */
		buf = headers + (slot % EQUEUE_ENTRIES) * HEADER_BYTES +
			NET_IP_ALIGN;
		edesc_head.va = va_to_tile_io_addr(buf);
		gxio_mpipe_equeue_put_at(equeue, edesc_head, slot);
		slot++;

		/* Egress the payload. */
		while (p_used < p_len) {
			void *va;

			/* Advance as needed. */
			while (f_used >= f_size) {
				f_id++;
				f_size = skb_frag_size(&sh->frags[f_id]);
				f_data = tile_net_frag_buf(&sh->frags[f_id]);
				f_used = 0;
			}

			va = f_data + f_used;

			/* Use bytes from the current fragment. */
			n = p_len - p_used;
			if (n > f_size - f_used)
				n = f_size - f_used;
			f_used += n;
			p_used += n;

			/* Egress a piece of the payload. */
			edesc_body.va = va_to_tile_io_addr(va);
			edesc_body.xfer_size = n;
			edesc_body.bound = !(p_used < p_len);
			gxio_mpipe_equeue_put_at(equeue, edesc_body, slot);
			slot++;
		}

		tx_packets++;
		tx_bytes += sh_len + p_len;

		/* The last segment may be less than gso_size. */
		data_len -= p_len;
		if (data_len < p_len)
			p_len = data_len;
	}

	/* Update stats. */
	tile_net_stats_add(tx_packets, &dev->stats.tx_packets);
	tile_net_stats_add(tx_bytes, &dev->stats.tx_bytes);
}

/* Do "TSO" handling for egress.
 *
 * Normally drivers set NETIF_F_TSO only to support hardware TSO;
 * otherwise the stack uses scatter-gather to implement GSO in software.
 * On our testing, enabling GSO support (via NETIF_F_SG) drops network
 * performance down to around 7.5 Gbps on the 10G interfaces, although
 * also dropping cpu utilization way down, to under 8%.  But
 * implementing "TSO" in the driver brings performance back up to line
 * rate, while dropping cpu usage even further, to less than 4%.  In
 * practice, profiling of GSO shows that skb_segment() is what causes
 * the performance overheads; we benefit in the driver from using
 * preallocated memory to duplicate the TCP/IP headers.
 */
static int tile_net_tx_tso(struct sk_buff *skb, struct net_device *dev)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	struct tile_net_priv *priv = netdev_priv(dev);
	int channel = priv->echannel;
	int instance = priv->instance;
	struct mpipe_data *md = &mpipe_data[instance];
	struct tile_net_egress *egress = &md->egress_for_echannel[channel];
	struct tile_net_comps *comps =
		info->mpipe[instance].comps_for_echannel[channel];
	gxio_mpipe_equeue_t *equeue = egress->equeue;
	unsigned long irqflags;
	int num_edescs;
	s64 slot;

	/* Determine how many mpipe edesc's are needed. */
	num_edescs = tso_count_edescs(skb);

	local_irq_save(irqflags);

	/* Try to acquire a completion entry and an egress slot. */
	slot = tile_net_equeue_try_reserve(dev, skb->queue_mapping, comps,
					   equeue, num_edescs);
	if (slot < 0) {
		local_irq_restore(irqflags);
		return NETDEV_TX_BUSY;
	}

	/* Set up copies of header data properly. */
	tso_headers_prepare(skb, egress->headers, slot);

	/* Actually pass the data to the network hardware. */
	tso_egress(dev, equeue, skb, egress->headers, slot);

	/* Add a completion record. */
	add_comp(equeue, comps, slot + num_edescs - 1, skb);

	local_irq_restore(irqflags);

	/* Make sure the egress timer is scheduled. */
	tile_net_schedule_egress_timer();

	return NETDEV_TX_OK;
}

/* Analyze the body and frags for a transmit request. */
static unsigned int tile_net_tx_frags(struct frag *frags,
				       struct sk_buff *skb,
				       void *b_data, unsigned int b_len)
{
	unsigned int i, n = 0;

	struct skb_shared_info *sh = skb_shinfo(skb);

	if (b_len != 0) {
		frags[n].buf = b_data;
		frags[n++].length = b_len;
	}

	for (i = 0; i < sh->nr_frags; i++) {
		skb_frag_t *f = &sh->frags[i];
		frags[n].buf = tile_net_frag_buf(f);
		frags[n++].length = skb_frag_size(f);
	}

	return n;
}

/* Help the kernel transmit a packet. */
static int tile_net_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	struct tile_net_priv *priv = netdev_priv(dev);
	int instance = priv->instance;
	struct mpipe_data *md = &mpipe_data[instance];
	struct tile_net_egress *egress =
		&md->egress_for_echannel[priv->echannel];
	gxio_mpipe_equeue_t *equeue = egress->equeue;
	struct tile_net_comps *comps =
		info->mpipe[instance].comps_for_echannel[priv->echannel];
	unsigned int len = skb->len;
	unsigned char *data = skb->data;
	unsigned int num_edescs;
	struct frag frags[MAX_FRAGS];
	gxio_mpipe_edesc_t edescs[MAX_FRAGS];
	unsigned long irqflags;
	gxio_mpipe_edesc_t edesc = { { 0 } };
	unsigned int i;
	s64 slot;

	if (skb_is_gso(skb))
		return tile_net_tx_tso(skb, dev);

	num_edescs = tile_net_tx_frags(frags, skb, data, skb_headlen(skb));

	/* This is only used to specify the TLB. */
	edesc.stack_idx = md->first_buffer_stack;

	/* Prepare the edescs. */
	for (i = 0; i < num_edescs; i++) {
		edesc.xfer_size = frags[i].length;
		edesc.va = va_to_tile_io_addr(frags[i].buf);
		edescs[i] = edesc;
	}

	/* Mark the final edesc. */
	edescs[num_edescs - 1].bound = 1;

	/* Add checksum info to the initial edesc, if needed. */
	if (skb->ip_summed == CHECKSUM_PARTIAL) {
		unsigned int csum_start = skb_checksum_start_offset(skb);
		edescs[0].csum = 1;
		edescs[0].csum_start = csum_start;
		edescs[0].csum_dest = csum_start + skb->csum_offset;
	}

	local_irq_save(irqflags);

	/* Try to acquire a completion entry and an egress slot. */
	slot = tile_net_equeue_try_reserve(dev, skb->queue_mapping, comps,
					   equeue, num_edescs);
	if (slot < 0) {
		local_irq_restore(irqflags);
		return NETDEV_TX_BUSY;
	}

	for (i = 0; i < num_edescs; i++)
		gxio_mpipe_equeue_put_at(equeue, edescs[i], slot++);

	/* Store TX timestamp if needed. */
	tile_tx_timestamp(skb, instance);

	/* Add a completion record. */
	add_comp(equeue, comps, slot - 1, skb);

	/* NOTE: Use ETH_ZLEN for short packets (e.g. 42 < 60). */
	tile_net_stats_add(1, &dev->stats.tx_packets);
	tile_net_stats_add(max_t(unsigned int, len, ETH_ZLEN),
			   &dev->stats.tx_bytes);

	local_irq_restore(irqflags);

	/* Make sure the egress timer is scheduled. */
	tile_net_schedule_egress_timer();

	return NETDEV_TX_OK;
}

/* Return subqueue id on this core (one per core). */
static u16 tile_net_select_queue(struct net_device *dev, struct sk_buff *skb,
				 void *accel_priv, select_queue_fallback_t fallback)
{
	return smp_processor_id();
}

/* Deal with a transmit timeout. */
static void tile_net_tx_timeout(struct net_device *dev)
{
	int cpu;

	for_each_online_cpu(cpu)
		netif_wake_subqueue(dev, cpu);
}

/* Ioctl commands. */
static int tile_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	if (cmd == SIOCSHWTSTAMP)
		return tile_hwtstamp_set(dev, rq);
	if (cmd == SIOCGHWTSTAMP)
		return tile_hwtstamp_get(dev, rq);

	return -EOPNOTSUPP;
}

/* Change the MTU. */
static int tile_net_change_mtu(struct net_device *dev, int new_mtu)
{
	if (new_mtu < 68)
		return -EINVAL;
	if (new_mtu > ((jumbo_num != 0) ? 9000 : 1500))
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

/* Change the Ethernet address of the NIC.
 *
 * The hypervisor driver does not support changing MAC address.  However,
 * the hardware does not do anything with the MAC address, so the address
 * which gets used on outgoing packets, and which is accepted on incoming
 * packets, is completely up to us.
 *
 * Returns 0 on success, negative on failure.
 */
static int tile_net_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EINVAL;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
/* Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void tile_net_netpoll(struct net_device *dev)
{
	int instance = mpipe_instance(dev);
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	struct mpipe_data *md = &mpipe_data[instance];

	disable_percpu_irq(md->ingress_irq);
	napi_schedule(&info->mpipe[instance].napi);
	enable_percpu_irq(md->ingress_irq, 0);
}
#endif

static const struct net_device_ops tile_net_ops = {
	.ndo_open = tile_net_open,
	.ndo_stop = tile_net_stop,
	.ndo_start_xmit = tile_net_tx,
	.ndo_select_queue = tile_net_select_queue,
	.ndo_do_ioctl = tile_net_ioctl,
	.ndo_change_mtu = tile_net_change_mtu,
	.ndo_tx_timeout = tile_net_tx_timeout,
	.ndo_set_mac_address = tile_net_set_mac_address,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = tile_net_netpoll,
#endif
};

/* The setup function.
 *
 * This uses ether_setup() to assign various fields in dev, including
 * setting IFF_BROADCAST and IFF_MULTICAST, then sets some extra fields.
 */
static void tile_net_setup(struct net_device *dev)
{
	netdev_features_t features = 0;

	ether_setup(dev);
	dev->netdev_ops = &tile_net_ops;
	dev->watchdog_timeo = TILE_NET_TIMEOUT;
	dev->mtu = 1500;

	features |= NETIF_F_HW_CSUM;
	features |= NETIF_F_SG;
	features |= NETIF_F_TSO;
	features |= NETIF_F_TSO6;

	dev->hw_features   |= features;
	dev->vlan_features |= features;
	dev->features      |= features;
}

/* Allocate the device structure, register the device, and obtain the
 * MAC address from the hypervisor.
 */
static void tile_net_dev_init(const char *name, const uint8_t *mac)
{
	int ret;
	int i;
	int nz_addr = 0;
	struct net_device *dev;
	struct tile_net_priv *priv;

	/* HACK: Ignore "loop" links. */
	if (strncmp(name, "loop", 4) == 0)
		return;

	/* Allocate the device structure.  Normally, "name" is a
	 * template, instantiated by register_netdev(), but not for us.
	 */
	dev = alloc_netdev_mqs(sizeof(*priv), name, tile_net_setup,
			       NR_CPUS, 1);
	if (!dev) {
		pr_err("alloc_netdev_mqs(%s) failed\n", name);
		return;
	}

	/* Initialize "priv". */
	priv = netdev_priv(dev);
	memset(priv, 0, sizeof(*priv));
	priv->dev = dev;
	priv->channel = -1;
	priv->loopify_channel = -1;
	priv->echannel = -1;
	init_ptp_dev(priv);

	/* Get the MAC address and set it in the device struct; this must
	 * be done before the device is opened.  If the MAC is all zeroes,
	 * we use a random address, since we're probably on the simulator.
	 */
	for (i = 0; i < 6; i++)
		nz_addr |= mac[i];

	if (nz_addr) {
		memcpy(dev->dev_addr, mac, ETH_ALEN);
		dev->addr_len = 6;
	} else {
		eth_hw_addr_random(dev);
	}

	/* Register the network device. */
	ret = register_netdev(dev);
	if (ret) {
		netdev_err(dev, "register_netdev failed %d\n", ret);
		free_netdev(dev);
		return;
	}
}

/* Per-cpu module initialization. */
static void tile_net_init_module_percpu(void *unused)
{
	struct tile_net_info *info = &__get_cpu_var(per_cpu_info);
	int my_cpu = smp_processor_id();
	int instance;

	for (instance = 0; instance < NR_MPIPE_MAX; instance++) {
		info->mpipe[instance].has_iqueue = false;
		info->mpipe[instance].instance = instance;
	}
	info->my_cpu = my_cpu;

	/* Initialize the egress timer. */
	hrtimer_init(&info->egress_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	info->egress_timer.function = tile_net_handle_egress_timer;
}

/* Module initialization. */
static int __init tile_net_init_module(void)
{
	int i;
	char name[GXIO_MPIPE_LINK_NAME_LEN];
	uint8_t mac[6];

	pr_info("Tilera Network Driver\n");

	BUILD_BUG_ON(NR_MPIPE_MAX != 2);

	mutex_init(&tile_net_devs_for_channel_mutex);

	/* Initialize each CPU. */
	on_each_cpu(tile_net_init_module_percpu, NULL, 1);

	/* Find out what devices we have, and initialize them. */
	for (i = 0; gxio_mpipe_link_enumerate_mac(i, name, mac) >= 0; i++)
		tile_net_dev_init(name, mac);

	if (!network_cpus_init())
		network_cpus_map = *cpu_online_mask;

	return 0;
}

module_init(tile_net_init_module);
