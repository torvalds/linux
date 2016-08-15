/*
 * Copyright 2011 Tilera Corporation. All Rights Reserved.
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
#include <linux/netdevice.h>   /* struct device, and other headers */
#include <linux/etherdevice.h> /* eth_type_trans */
#include <linux/skbuff.h>
#include <linux/ioctl.h>
#include <linux/cdev.h>
#include <linux/hugetlb.h>
#include <linux/in6.h>
#include <linux/timer.h>
#include <linux/io.h>
#include <linux/u64_stats_sync.h>
#include <asm/checksum.h>
#include <asm/homecache.h>

#include <hv/drv_xgbe_intf.h>
#include <hv/drv_xgbe_impl.h>
#include <hv/hypervisor.h>
#include <hv/netio_intf.h>

/* For TSO */
#include <linux/ip.h>
#include <linux/tcp.h>


/*
 * First, "tile_net_init_module()" initializes all four "devices" which
 * can be used by linux.
 *
 * Then, "ifconfig DEVICE up" calls "tile_net_open()", which analyzes
 * the network cpus, then uses "tile_net_open_aux()" to initialize
 * LIPP/LEPP, and then uses "tile_net_open_inner()" to register all
 * the tiles, provide buffers to LIPP, allow ingress to start, and
 * turn on hypervisor interrupt handling (and NAPI) on all tiles.
 *
 * If registration fails due to the link being down, then "retry_work"
 * is used to keep calling "tile_net_open_inner()" until it succeeds.
 *
 * If "ifconfig DEVICE down" is called, it uses "tile_net_stop()" to
 * stop egress, drain the LIPP buffers, unregister all the tiles, stop
 * LIPP/LEPP, and wipe the LEPP queue.
 *
 * We start out with the ingress interrupt enabled on each CPU.  When
 * this interrupt fires, we disable it, and call "napi_schedule()".
 * This will cause "tile_net_poll()" to be called, which will pull
 * packets from the netio queue, filtering them out, or passing them
 * to "netif_receive_skb()".  If our budget is exhausted, we will
 * return, knowing we will be called again later.  Otherwise, we
 * reenable the ingress interrupt, and call "napi_complete()".
 *
 * HACK: Since disabling the ingress interrupt is not reliable, we
 * ignore the interrupt if the global "active" flag is false.
 *
 *
 * NOTE: The use of "native_driver" ensures that EPP exists, and that
 * we are using "LIPP" and "LEPP".
 *
 * NOTE: Failing to free completions for an arbitrarily long time
 * (which is defined to be illegal) does in fact cause bizarre
 * problems.  The "egress_timer" helps prevent this from happening.
 */


/* HACK: Allow use of "jumbo" packets. */
/* This should be 1500 if "jumbo" is not set in LIPP. */
/* This should be at most 10226 (10240 - 14) if "jumbo" is set in LIPP. */
/* ISSUE: This has not been thoroughly tested (except at 1500). */
#define TILE_NET_MTU 1500

/* HACK: Define this to verify incoming packets. */
/* #define TILE_NET_VERIFY_INGRESS */

/* Use 3000 to enable the Linux Traffic Control (QoS) layer, else 0. */
#define TILE_NET_TX_QUEUE_LEN 0

/* Define to dump packets (prints out the whole packet on tx and rx). */
/* #define TILE_NET_DUMP_PACKETS */

/* Define to enable debug spew (all PDEBUG's are enabled). */
/* #define TILE_NET_DEBUG */


/* Define to activate paranoia checks. */
/* #define TILE_NET_PARANOIA */

/* Default transmit lockup timeout period, in jiffies. */
#define TILE_NET_TIMEOUT (5 * HZ)

/* Default retry interval for bringing up the NetIO interface, in jiffies. */
#define TILE_NET_RETRY_INTERVAL (5 * HZ)

/* Number of ports (xgbe0, xgbe1, gbe0, gbe1). */
#define TILE_NET_DEVS 4



/* Paranoia. */
#if NET_IP_ALIGN != LIPP_PACKET_PADDING
#error "NET_IP_ALIGN must match LIPP_PACKET_PADDING."
#endif


/* Debug print. */
#ifdef TILE_NET_DEBUG
#define PDEBUG(fmt, args...) net_printk(fmt, ## args)
#else
#define PDEBUG(fmt, args...)
#endif


MODULE_AUTHOR("Tilera");
MODULE_LICENSE("GPL");


/*
 * Queue of incoming packets for a specific cpu and device.
 *
 * Includes a pointer to the "system" data, and the actual "user" data.
 */
struct tile_netio_queue {
	netio_queue_impl_t *__system_part;
	netio_queue_user_impl_t __user_part;

};


/*
 * Statistics counters for a specific cpu and device.
 */
struct tile_net_stats_t {
	struct u64_stats_sync syncp;
	u64 rx_packets;		/* total packets received	*/
	u64 tx_packets;		/* total packets transmitted	*/
	u64 rx_bytes;		/* total bytes received 	*/
	u64 tx_bytes;		/* total bytes transmitted	*/
	u64 rx_errors;		/* packets truncated or marked bad by hw */
	u64 rx_dropped;		/* packets not for us or intf not up */
};


/*
 * Info for a specific cpu and device.
 *
 * ISSUE: There is a "dev" pointer in "napi" as well.
 */
struct tile_net_cpu {
	/* The NAPI struct. */
	struct napi_struct napi;
	/* Packet queue. */
	struct tile_netio_queue queue;
	/* Statistics. */
	struct tile_net_stats_t stats;
	/* True iff NAPI is enabled. */
	bool napi_enabled;
	/* True if this tile has successfully registered with the IPP. */
	bool registered;
	/* True if the link was down last time we tried to register. */
	bool link_down;
	/* True if "egress_timer" is scheduled. */
	bool egress_timer_scheduled;
	/* Number of small sk_buffs which must still be provided. */
	unsigned int num_needed_small_buffers;
	/* Number of large sk_buffs which must still be provided. */
	unsigned int num_needed_large_buffers;
	/* A timer for handling egress completions. */
	struct timer_list egress_timer;
};


/*
 * Info for a specific device.
 */
struct tile_net_priv {
	/* Our network device. */
	struct net_device *dev;
	/* Pages making up the egress queue. */
	struct page *eq_pages;
	/* Address of the actual egress queue. */
	lepp_queue_t *eq;
	/* Protects "eq". */
	spinlock_t eq_lock;
	/* The hypervisor handle for this interface. */
	int hv_devhdl;
	/* The intr bit mask that IDs this device. */
	u32 intr_id;
	/* True iff "tile_net_open_aux()" has succeeded. */
	bool partly_opened;
	/* True iff the device is "active". */
	bool active;
	/* Effective network cpus. */
	struct cpumask network_cpus_map;
	/* Number of network cpus. */
	int network_cpus_count;
	/* Credits per network cpu. */
	int network_cpus_credits;
	/* For NetIO bringup retries. */
	struct delayed_work retry_work;
	/* Quick access to per cpu data. */
	struct tile_net_cpu *cpu[NR_CPUS];
};

/* Log2 of the number of small pages needed for the egress queue. */
#define EQ_ORDER  get_order(sizeof(lepp_queue_t))
/* Size of the egress queue's pages. */
#define EQ_SIZE   (1 << (PAGE_SHIFT + EQ_ORDER))

/*
 * The actual devices (xgbe0, xgbe1, gbe0, gbe1).
 */
static struct net_device *tile_net_devs[TILE_NET_DEVS];

/*
 * The "tile_net_cpu" structures for each device.
 */
static DEFINE_PER_CPU(struct tile_net_cpu, hv_xgbe0);
static DEFINE_PER_CPU(struct tile_net_cpu, hv_xgbe1);
static DEFINE_PER_CPU(struct tile_net_cpu, hv_gbe0);
static DEFINE_PER_CPU(struct tile_net_cpu, hv_gbe1);


/*
 * True if "network_cpus" was specified.
 */
static bool network_cpus_used;

/*
 * The actual cpus in "network_cpus".
 */
static struct cpumask network_cpus_map;



#ifdef TILE_NET_DEBUG
/*
 * printk with extra stuff.
 *
 * We print the CPU we're running in brackets.
 */
static void net_printk(char *fmt, ...)
{
	int i;
	int len;
	va_list args;
	static char buf[256];

	len = sprintf(buf, "tile_net[%2.2d]: ", smp_processor_id());
	va_start(args, fmt);
	i = vscnprintf(buf + len, sizeof(buf) - len - 1, fmt, args);
	va_end(args);
	buf[255] = '\0';
	pr_notice(buf);
}
#endif


#ifdef TILE_NET_DUMP_PACKETS
/*
 * Dump a packet.
 */
static void dump_packet(unsigned char *data, unsigned long length, char *s)
{
	int my_cpu = smp_processor_id();

	unsigned long i;
	char buf[128];

	static unsigned int count;

	pr_info("dump_packet(data %p, length 0x%lx s %s count 0x%x)\n",
	       data, length, s, count++);

	pr_info("\n");

	for (i = 0; i < length; i++) {
		if ((i & 0xf) == 0)
			sprintf(buf, "[%02d] %8.8lx:", my_cpu, i);
		sprintf(buf + strlen(buf), " %2.2x", data[i]);
		if ((i & 0xf) == 0xf || i == length - 1) {
			strcat(buf, "\n");
			pr_info("%s", buf);
		}
	}
}
#endif


/*
 * Provide support for the __netio_fastio1() swint
 * (see <hv/drv_xgbe_intf.h> for how it is used).
 *
 * The fastio swint2 call may clobber all the caller-saved registers.
 * It rarely clobbers memory, but we allow for the possibility in
 * the signature just to be on the safe side.
 *
 * Also, gcc doesn't seem to allow an input operand to be
 * clobbered, so we fake it with dummy outputs.
 *
 * This function can't be static because of the way it is declared
 * in the netio header.
 */
inline int __netio_fastio1(u32 fastio_index, u32 arg0)
{
	long result, clobber_r1, clobber_r10;
	asm volatile("swint2"
		     : "=R00" (result),
		       "=R01" (clobber_r1), "=R10" (clobber_r10)
		     : "R10" (fastio_index), "R01" (arg0)
		     : "memory", "r2", "r3", "r4",
		       "r5", "r6", "r7", "r8", "r9",
		       "r11", "r12", "r13", "r14",
		       "r15", "r16", "r17", "r18", "r19",
		       "r20", "r21", "r22", "r23", "r24",
		       "r25", "r26", "r27", "r28", "r29");
	return result;
}


static void tile_net_return_credit(struct tile_net_cpu *info)
{
	struct tile_netio_queue *queue = &info->queue;
	netio_queue_user_impl_t *qup = &queue->__user_part;

	/* Return four credits after every fourth packet. */
	if (--qup->__receive_credit_remaining == 0) {
		u32 interval = qup->__receive_credit_interval;
		qup->__receive_credit_remaining = interval;
		__netio_fastio_return_credits(qup->__fastio_index, interval);
	}
}



/*
 * Provide a linux buffer to LIPP.
 */
static void tile_net_provide_linux_buffer(struct tile_net_cpu *info,
					  void *va, bool small)
{
	struct tile_netio_queue *queue = &info->queue;

	/* Convert "va" and "small" to "linux_buffer_t". */
	unsigned int buffer = ((unsigned int)(__pa(va) >> 7) << 1) + small;

	__netio_fastio_free_buffer(queue->__user_part.__fastio_index, buffer);
}


/*
 * Provide a linux buffer for LIPP.
 *
 * Note that the ACTUAL allocation for each buffer is a "struct sk_buff",
 * plus a chunk of memory that includes not only the requested bytes, but
 * also NET_SKB_PAD bytes of initial padding, and a "struct skb_shared_info".
 *
 * Note that "struct skb_shared_info" is 88 bytes with 64K pages and
 * 268 bytes with 4K pages (since the frags[] array needs 18 entries).
 *
 * Without jumbo packets, the maximum packet size will be 1536 bytes,
 * and we use 2 bytes (NET_IP_ALIGN) of padding.  ISSUE: If we told
 * the hardware to clip at 1518 bytes instead of 1536 bytes, then we
 * could save an entire cache line, but in practice, we don't need it.
 *
 * Since CPAs are 38 bits, and we can only encode the high 31 bits in
 * a "linux_buffer_t", the low 7 bits must be zero, and thus, we must
 * align the actual "va" mod 128.
 *
 * We assume that the underlying "head" will be aligned mod 64.  Note
 * that in practice, we have seen "head" NOT aligned mod 128 even when
 * using 2048 byte allocations, which is surprising.
 *
 * If "head" WAS always aligned mod 128, we could change LIPP to
 * assume that the low SIX bits are zero, and the 7th bit is one, that
 * is, align the actual "va" mod 128 plus 64, which would be "free".
 *
 * For now, the actual "head" pointer points at NET_SKB_PAD bytes of
 * padding, plus 28 or 92 bytes of extra padding, plus the sk_buff
 * pointer, plus the NET_IP_ALIGN padding, plus 126 or 1536 bytes for
 * the actual packet, plus 62 bytes of empty padding, plus some
 * padding and the "struct skb_shared_info".
 *
 * With 64K pages, a large buffer thus needs 32+92+4+2+1536+62+88
 * bytes, or 1816 bytes, which fits comfortably into 2048 bytes.
 *
 * With 64K pages, a small buffer thus needs 32+92+4+2+126+88
 * bytes, or 344 bytes, which means we are wasting 64+ bytes, and
 * could presumably increase the size of small buffers.
 *
 * With 4K pages, a large buffer thus needs 32+92+4+2+1536+62+268
 * bytes, or 1996 bytes, which fits comfortably into 2048 bytes.
 *
 * With 4K pages, a small buffer thus needs 32+92+4+2+126+268
 * bytes, or 524 bytes, which is annoyingly wasteful.
 *
 * Maybe we should increase LIPP_SMALL_PACKET_SIZE to 192?
 *
 * ISSUE: Maybe we should increase "NET_SKB_PAD" to 64?
 */
static bool tile_net_provide_needed_buffer(struct tile_net_cpu *info,
					   bool small)
{
#if TILE_NET_MTU <= 1536
	/* Without "jumbo", 2 + 1536 should be sufficient. */
	unsigned int large_size = NET_IP_ALIGN + 1536;
#else
	/* ISSUE: This has not been tested. */
	unsigned int large_size = NET_IP_ALIGN + TILE_NET_MTU + 100;
#endif

	/* Avoid "false sharing" with last cache line. */
	/* ISSUE: This is already done by "netdev_alloc_skb()". */
	unsigned int len =
		 (((small ? LIPP_SMALL_PACKET_SIZE : large_size) +
		   CHIP_L2_LINE_SIZE() - 1) & -CHIP_L2_LINE_SIZE());

	unsigned int padding = 128 - NET_SKB_PAD;
	unsigned int align;

	struct sk_buff *skb;
	void *va;

	struct sk_buff **skb_ptr;

	/* Request 96 extra bytes for alignment purposes. */
	skb = netdev_alloc_skb(info->napi.dev, len + padding);
	if (skb == NULL)
		return false;

	/* Skip 32 or 96 bytes to align "data" mod 128. */
	align = -(long)skb->data & (128 - 1);
	BUG_ON(align > padding);
	skb_reserve(skb, align);

	/* This address is given to IPP. */
	va = skb->data;

	/* Buffers must not span a huge page. */
	BUG_ON(((((long)va & ~HPAGE_MASK) + len) & HPAGE_MASK) != 0);

#ifdef TILE_NET_PARANOIA
#if CHIP_HAS_CBOX_HOME_MAP()
	if (hash_default) {
		HV_PTE pte = *virt_to_pte(current->mm, (unsigned long)va);
		if (hv_pte_get_mode(pte) != HV_PTE_MODE_CACHE_HASH_L3)
			panic("Non-HFH ingress buffer! VA=%p Mode=%d PTE=%llx",
			      va, hv_pte_get_mode(pte), hv_pte_val(pte));
	}
#endif
#endif

	/* Invalidate the packet buffer. */
	if (!hash_default)
		__inv_buffer(va, len);

	/* Skip two bytes to satisfy LIPP assumptions. */
	/* Note that this aligns IP on a 16 byte boundary. */
	/* ISSUE: Do this when the packet arrives? */
	skb_reserve(skb, NET_IP_ALIGN);

	/* Save a back-pointer to 'skb'. */
	skb_ptr = va - sizeof(*skb_ptr);
	*skb_ptr = skb;

	/* Make sure "skb_ptr" has been flushed. */
	__insn_mf();

	/* Provide the new buffer. */
	tile_net_provide_linux_buffer(info, va, small);

	return true;
}


/*
 * Provide linux buffers for LIPP.
 */
static void tile_net_provide_needed_buffers(struct tile_net_cpu *info)
{
	while (info->num_needed_small_buffers != 0) {
		if (!tile_net_provide_needed_buffer(info, true))
			goto oops;
		info->num_needed_small_buffers--;
	}

	while (info->num_needed_large_buffers != 0) {
		if (!tile_net_provide_needed_buffer(info, false))
			goto oops;
		info->num_needed_large_buffers--;
	}

	return;

oops:

	/* Add a description to the page allocation failure dump. */
	pr_notice("Could not provide a linux buffer to LIPP.\n");
}


/*
 * Grab some LEPP completions, and store them in "comps", of size
 * "comps_size", and return the number of completions which were
 * stored, so the caller can free them.
 */
static unsigned int tile_net_lepp_grab_comps(lepp_queue_t *eq,
					     struct sk_buff *comps[],
					     unsigned int comps_size,
					     unsigned int min_size)
{
	unsigned int n = 0;

	unsigned int comp_head = eq->comp_head;
	unsigned int comp_busy = eq->comp_busy;

	while (comp_head != comp_busy && n < comps_size) {
		comps[n++] = eq->comps[comp_head];
		LEPP_QINC(comp_head);
	}

	if (n < min_size)
		return 0;

	eq->comp_head = comp_head;

	return n;
}


/*
 * Free some comps, and return true iff there are still some pending.
 */
static bool tile_net_lepp_free_comps(struct net_device *dev, bool all)
{
	struct tile_net_priv *priv = netdev_priv(dev);

	lepp_queue_t *eq = priv->eq;

	struct sk_buff *olds[64];
	unsigned int wanted = 64;
	unsigned int i, n;
	bool pending;

	spin_lock(&priv->eq_lock);

	if (all)
		eq->comp_busy = eq->comp_tail;

	n = tile_net_lepp_grab_comps(eq, olds, wanted, 0);

	pending = (eq->comp_head != eq->comp_tail);

	spin_unlock(&priv->eq_lock);

	for (i = 0; i < n; i++)
		kfree_skb(olds[i]);

	return pending;
}


/*
 * Make sure the egress timer is scheduled.
 *
 * Note that we use "schedule if not scheduled" logic instead of the more
 * obvious "reschedule" logic, because "reschedule" is fairly expensive.
 */
static void tile_net_schedule_egress_timer(struct tile_net_cpu *info)
{
	if (!info->egress_timer_scheduled) {
		mod_timer(&info->egress_timer, jiffies + 1);
		info->egress_timer_scheduled = true;
	}
}


/*
 * The "function" for "info->egress_timer".
 *
 * This timer will reschedule itself as long as there are any pending
 * completions expected (on behalf of any tile).
 *
 * ISSUE: Realistically, will the timer ever stop scheduling itself?
 *
 * ISSUE: This timer is almost never actually needed, so just use a global
 * timer that can run on any tile.
 *
 * ISSUE: Maybe instead track number of expected completions, and free
 * only that many, resetting to zero if "pending" is ever false.
 */
static void tile_net_handle_egress_timer(unsigned long arg)
{
	struct tile_net_cpu *info = (struct tile_net_cpu *)arg;
	struct net_device *dev = info->napi.dev;

	/* The timer is no longer scheduled. */
	info->egress_timer_scheduled = false;

	/* Free comps, and reschedule timer if more are pending. */
	if (tile_net_lepp_free_comps(dev, false))
		tile_net_schedule_egress_timer(info);
}


static void tile_net_discard_aux(struct tile_net_cpu *info, int index)
{
	struct tile_netio_queue *queue = &info->queue;
	netio_queue_impl_t *qsp = queue->__system_part;
	netio_queue_user_impl_t *qup = &queue->__user_part;

	int index2_aux = index + sizeof(netio_pkt_t);
	int index2 =
		((index2_aux ==
		  qsp->__packet_receive_queue.__last_packet_plus_one) ?
		 0 : index2_aux);

	netio_pkt_t *pkt = (netio_pkt_t *)((unsigned long) &qsp[1] + index);

	/* Extract the "linux_buffer_t". */
	unsigned int buffer = pkt->__packet.word;

	/* Convert "linux_buffer_t" to "va". */
	void *va = __va((phys_addr_t)(buffer >> 1) << 7);

	/* Acquire the associated "skb". */
	struct sk_buff **skb_ptr = va - sizeof(*skb_ptr);
	struct sk_buff *skb = *skb_ptr;

	kfree_skb(skb);

	/* Consume this packet. */
	qup->__packet_receive_read = index2;
}


/*
 * Like "tile_net_poll()", but just discard packets.
 */
static void tile_net_discard_packets(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];
	struct tile_netio_queue *queue = &info->queue;
	netio_queue_impl_t *qsp = queue->__system_part;
	netio_queue_user_impl_t *qup = &queue->__user_part;

	while (qup->__packet_receive_read !=
	       qsp->__packet_receive_queue.__packet_write) {
		int index = qup->__packet_receive_read;
		tile_net_discard_aux(info, index);
	}
}


/*
 * Handle the next packet.  Return true if "processed", false if "filtered".
 */
static bool tile_net_poll_aux(struct tile_net_cpu *info, int index)
{
	struct net_device *dev = info->napi.dev;

	struct tile_netio_queue *queue = &info->queue;
	netio_queue_impl_t *qsp = queue->__system_part;
	netio_queue_user_impl_t *qup = &queue->__user_part;
	struct tile_net_stats_t *stats = &info->stats;

	int filter;

	int index2_aux = index + sizeof(netio_pkt_t);
	int index2 =
		((index2_aux ==
		  qsp->__packet_receive_queue.__last_packet_plus_one) ?
		 0 : index2_aux);

	netio_pkt_t *pkt = (netio_pkt_t *)((unsigned long) &qsp[1] + index);

	netio_pkt_metadata_t *metadata = NETIO_PKT_METADATA(pkt);
	netio_pkt_status_t pkt_status = NETIO_PKT_STATUS_M(metadata, pkt);

	/* Extract the packet size.  FIXME: Shouldn't the second line */
	/* get subtracted?  Mostly moot, since it should be "zero". */
	unsigned long len =
		(NETIO_PKT_CUSTOM_LENGTH(pkt) +
		 NET_IP_ALIGN - NETIO_PACKET_PADDING);

	/* Extract the "linux_buffer_t". */
	unsigned int buffer = pkt->__packet.word;

	/* Extract "small" (vs "large"). */
	bool small = ((buffer & 1) != 0);

	/* Convert "linux_buffer_t" to "va". */
	void *va = __va((phys_addr_t)(buffer >> 1) << 7);

	/* Extract the packet data pointer. */
	/* Compare to "NETIO_PKT_CUSTOM_DATA(pkt)". */
	unsigned char *buf = va + NET_IP_ALIGN;

	/* Invalidate the packet buffer. */
	if (!hash_default)
		__inv_buffer(buf, len);

#ifdef TILE_NET_DUMP_PACKETS
	dump_packet(buf, len, "rx");
#endif /* TILE_NET_DUMP_PACKETS */

#ifdef TILE_NET_VERIFY_INGRESS
	if (pkt_status == NETIO_PKT_STATUS_OVERSIZE && len >= 64) {
		dump_packet(buf, len, "rx");
		panic("Unexpected OVERSIZE.");
	}
#endif

	filter = 0;

	if (pkt_status == NETIO_PKT_STATUS_BAD) {
		/* Handle CRC error and hardware truncation. */
		filter = 2;
	} else if (!(dev->flags & IFF_UP)) {
		/* Filter packets received before we're up. */
		filter = 1;
	} else if (NETIO_PKT_ETHERTYPE_RECOGNIZED_M(metadata, pkt) &&
		   pkt_status == NETIO_PKT_STATUS_UNDERSIZE) {
		/* Filter "truncated" packets. */
		filter = 2;
	} else if (!(dev->flags & IFF_PROMISC)) {
		if (!is_multicast_ether_addr(buf)) {
			/* Filter packets not for our address. */
			const u8 *mine = dev->dev_addr;
			filter = !ether_addr_equal(mine, buf);
		}
	}

	u64_stats_update_begin(&stats->syncp);

	if (filter != 0) {

		if (filter == 1)
			stats->rx_dropped++;
		else
			stats->rx_errors++;

		tile_net_provide_linux_buffer(info, va, small);

	} else {

		/* Acquire the associated "skb". */
		struct sk_buff **skb_ptr = va - sizeof(*skb_ptr);
		struct sk_buff *skb = *skb_ptr;

		/* Paranoia. */
		if (skb->data != buf)
			panic("Corrupt linux buffer from LIPP! "
			      "VA=%p, skb=%p, skb->data=%p\n",
			      va, skb, skb->data);

		/* Encode the actual packet length. */
		skb_put(skb, len);

		/* NOTE: This call also sets "skb->dev = dev". */
		skb->protocol = eth_type_trans(skb, dev);

		/* Avoid recomputing "good" TCP/UDP checksums. */
		if (NETIO_PKT_L4_CSUM_CORRECT_M(metadata, pkt))
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		netif_receive_skb(skb);

		stats->rx_packets++;
		stats->rx_bytes += len;
	}

	u64_stats_update_end(&stats->syncp);

	/* ISSUE: It would be nice to defer this until the packet has */
	/* actually been processed. */
	tile_net_return_credit(info);

	/* Consume this packet. */
	qup->__packet_receive_read = index2;

	return !filter;
}


/*
 * Handle some packets for the given device on the current CPU.
 *
 * If "tile_net_stop()" is called on some other tile while this
 * function is running, we will return, hopefully before that
 * other tile asks us to call "napi_disable()".
 *
 * The "rotting packet" race condition occurs if a packet arrives
 * during the extremely narrow window between the queue appearing to
 * be empty, and the ingress interrupt being re-enabled.  This happens
 * a LOT under heavy network load.
 */
static int tile_net_poll(struct napi_struct *napi, int budget)
{
	struct net_device *dev = napi->dev;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];
	struct tile_netio_queue *queue = &info->queue;
	netio_queue_impl_t *qsp = queue->__system_part;
	netio_queue_user_impl_t *qup = &queue->__user_part;

	unsigned int work = 0;

	if (budget <= 0)
		goto done;

	while (priv->active) {
		int index = qup->__packet_receive_read;
		if (index == qsp->__packet_receive_queue.__packet_write)
			break;

		if (tile_net_poll_aux(info, index)) {
			if (++work >= budget)
				goto done;
		}
	}

	napi_complete(&info->napi);

	if (!priv->active)
		goto done;

	/* Re-enable the ingress interrupt. */
	enable_percpu_irq(priv->intr_id, 0);

	/* HACK: Avoid the "rotting packet" problem (see above). */
	if (qup->__packet_receive_read !=
	    qsp->__packet_receive_queue.__packet_write) {
		/* ISSUE: Sometimes this returns zero, presumably */
		/* because an interrupt was handled for this tile. */
		(void)napi_reschedule(&info->napi);
	}

done:

	if (priv->active)
		tile_net_provide_needed_buffers(info);

	return work;
}


/*
 * Handle an ingress interrupt for the given device on the current cpu.
 *
 * ISSUE: Sometimes this gets called after "disable_percpu_irq()" has
 * been called!  This is probably due to "pending hypervisor downcalls".
 *
 * ISSUE: Is there any race condition between the "napi_schedule()" here
 * and the "napi_complete()" call above?
 */
static irqreturn_t tile_net_handle_ingress_interrupt(int irq, void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	/* Disable the ingress interrupt. */
	disable_percpu_irq(priv->intr_id);

	/* Ignore unwanted interrupts. */
	if (!priv->active)
		return IRQ_HANDLED;

	/* ISSUE: Sometimes "info->napi_enabled" is false here. */

	napi_schedule(&info->napi);

	return IRQ_HANDLED;
}


/*
 * One time initialization per interface.
 */
static int tile_net_open_aux(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);

	int ret;
	int dummy;
	unsigned int epp_lotar;

	/*
	 * Find out where EPP memory should be homed.
	 */
	ret = hv_dev_pread(priv->hv_devhdl, 0,
			   (HV_VirtAddr)&epp_lotar, sizeof(epp_lotar),
			   NETIO_EPP_SHM_OFF);
	if (ret < 0) {
		pr_err("could not read epp_shm_queue lotar.\n");
		return -EIO;
	}

	/*
	 * Home the page on the EPP.
	 */
	{
		int epp_home = hv_lotar_to_cpu(epp_lotar);
		homecache_change_page_home(priv->eq_pages, EQ_ORDER, epp_home);
	}

	/*
	 * Register the EPP shared memory queue.
	 */
	{
		netio_ipp_address_t ea = {
			.va = 0,
			.pa = __pa(priv->eq),
			.pte = hv_pte(0),
			.size = EQ_SIZE,
		};
		ea.pte = hv_pte_set_lotar(ea.pte, epp_lotar);
		ea.pte = hv_pte_set_mode(ea.pte, HV_PTE_MODE_CACHE_TILE_L3);
		ret = hv_dev_pwrite(priv->hv_devhdl, 0,
				    (HV_VirtAddr)&ea,
				    sizeof(ea),
				    NETIO_EPP_SHM_OFF);
		if (ret < 0)
			return -EIO;
	}

	/*
	 * Start LIPP/LEPP.
	 */
	if (hv_dev_pwrite(priv->hv_devhdl, 0, (HV_VirtAddr)&dummy,
			  sizeof(dummy), NETIO_IPP_START_SHIM_OFF) < 0) {
		pr_warn("Failed to start LIPP/LEPP\n");
		return -EIO;
	}

	return 0;
}


/*
 * Register with hypervisor on the current CPU.
 *
 * Strangely, this function does important things even if it "fails",
 * which is especially common if the link is not up yet.  Hopefully
 * these things are all "harmless" if done twice!
 */
static void tile_net_register(void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info;

	struct tile_netio_queue *queue;

	/* Only network cpus can receive packets. */
	int queue_id =
		cpumask_test_cpu(my_cpu, &priv->network_cpus_map) ? 0 : 255;

	netio_input_config_t config = {
		.flags = 0,
		.num_receive_packets = priv->network_cpus_credits,
		.queue_id = queue_id
	};

	int ret = 0;
	netio_queue_impl_t *queuep;

	PDEBUG("tile_net_register(queue_id %d)\n", queue_id);

	if (!strcmp(dev->name, "xgbe0"))
		info = this_cpu_ptr(&hv_xgbe0);
	else if (!strcmp(dev->name, "xgbe1"))
		info = this_cpu_ptr(&hv_xgbe1);
	else if (!strcmp(dev->name, "gbe0"))
		info = this_cpu_ptr(&hv_gbe0);
	else if (!strcmp(dev->name, "gbe1"))
		info = this_cpu_ptr(&hv_gbe1);
	else
		BUG();

	/* Initialize the egress timer. */
	init_timer_pinned(&info->egress_timer);
	info->egress_timer.data = (long)info;
	info->egress_timer.function = tile_net_handle_egress_timer;

	u64_stats_init(&info->stats.syncp);

	priv->cpu[my_cpu] = info;

	/*
	 * Register ourselves with LIPP.  This does a lot of stuff,
	 * including invoking the LIPP registration code.
	 */
	ret = hv_dev_pwrite(priv->hv_devhdl, 0,
			    (HV_VirtAddr)&config,
			    sizeof(netio_input_config_t),
			    NETIO_IPP_INPUT_REGISTER_OFF);
	PDEBUG("hv_dev_pwrite(NETIO_IPP_INPUT_REGISTER_OFF) returned %d\n",
	       ret);
	if (ret < 0) {
		if (ret != NETIO_LINK_DOWN) {
			printk(KERN_DEBUG "hv_dev_pwrite "
			       "NETIO_IPP_INPUT_REGISTER_OFF failure %d\n",
			       ret);
		}
		info->link_down = (ret == NETIO_LINK_DOWN);
		return;
	}

	/*
	 * Get the pointer to our queue's system part.
	 */

	ret = hv_dev_pread(priv->hv_devhdl, 0,
			   (HV_VirtAddr)&queuep,
			   sizeof(netio_queue_impl_t *),
			   NETIO_IPP_INPUT_REGISTER_OFF);
	PDEBUG("hv_dev_pread(NETIO_IPP_INPUT_REGISTER_OFF) returned %d\n",
	       ret);
	PDEBUG("queuep %p\n", queuep);
	if (ret <= 0) {
		/* ISSUE: Shouldn't this be a fatal error? */
		pr_err("hv_dev_pread NETIO_IPP_INPUT_REGISTER_OFF failure\n");
		return;
	}

	queue = &info->queue;

	queue->__system_part = queuep;

	memset(&queue->__user_part, 0, sizeof(netio_queue_user_impl_t));

	/* This is traditionally "config.num_receive_packets / 2". */
	queue->__user_part.__receive_credit_interval = 4;
	queue->__user_part.__receive_credit_remaining =
		queue->__user_part.__receive_credit_interval;

	/*
	 * Get a fastio index from the hypervisor.
	 * ISSUE: Shouldn't this check the result?
	 */
	ret = hv_dev_pread(priv->hv_devhdl, 0,
			   (HV_VirtAddr)&queue->__user_part.__fastio_index,
			   sizeof(queue->__user_part.__fastio_index),
			   NETIO_IPP_GET_FASTIO_OFF);
	PDEBUG("hv_dev_pread(NETIO_IPP_GET_FASTIO_OFF) returned %d\n", ret);

	/* Now we are registered. */
	info->registered = true;
}


/*
 * Deregister with hypervisor on the current CPU.
 *
 * This simply discards all our credits, so no more packets will be
 * delivered to this tile.  There may still be packets in our queue.
 *
 * Also, disable the ingress interrupt.
 */
static void tile_net_deregister(void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	/* Disable the ingress interrupt. */
	disable_percpu_irq(priv->intr_id);

	/* Do nothing else if not registered. */
	if (info == NULL || !info->registered)
		return;

	{
		struct tile_netio_queue *queue = &info->queue;
		netio_queue_user_impl_t *qup = &queue->__user_part;

		/* Discard all our credits. */
		__netio_fastio_return_credits(qup->__fastio_index, -1);
	}
}


/*
 * Unregister with hypervisor on the current CPU.
 *
 * Also, disable the ingress interrupt.
 */
static void tile_net_unregister(void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	int ret;
	int dummy = 0;

	/* Disable the ingress interrupt. */
	disable_percpu_irq(priv->intr_id);

	/* Do nothing else if not registered. */
	if (info == NULL || !info->registered)
		return;

	/* Unregister ourselves with LIPP/LEPP. */
	ret = hv_dev_pwrite(priv->hv_devhdl, 0, (HV_VirtAddr)&dummy,
			    sizeof(dummy), NETIO_IPP_INPUT_UNREGISTER_OFF);
	if (ret < 0)
		panic("Failed to unregister with LIPP/LEPP!\n");

	/* Discard all packets still in our NetIO queue. */
	tile_net_discard_packets(dev);

	/* Reset state. */
	info->num_needed_small_buffers = 0;
	info->num_needed_large_buffers = 0;

	/* Cancel egress timer. */
	del_timer(&info->egress_timer);
	info->egress_timer_scheduled = false;
}


/*
 * Helper function for "tile_net_stop()".
 *
 * Also used to handle registration failure in "tile_net_open_inner()",
 * when the various extra steps in "tile_net_stop()" are not necessary.
 */
static void tile_net_stop_aux(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	int i;

	int dummy = 0;

	/*
	 * Unregister all tiles, so LIPP will stop delivering packets.
	 * Also, delete all the "napi" objects (sequentially, to protect
	 * "dev->napi_list").
	 */
	on_each_cpu(tile_net_unregister, (void *)dev, 1);
	for_each_online_cpu(i) {
		struct tile_net_cpu *info = priv->cpu[i];
		if (info != NULL && info->registered) {
			netif_napi_del(&info->napi);
			info->registered = false;
		}
	}

	/* Stop LIPP/LEPP. */
	if (hv_dev_pwrite(priv->hv_devhdl, 0, (HV_VirtAddr)&dummy,
			  sizeof(dummy), NETIO_IPP_STOP_SHIM_OFF) < 0)
		panic("Failed to stop LIPP/LEPP!\n");

	priv->partly_opened = false;
}


/*
 * Disable NAPI for the given device on the current cpu.
 */
static void tile_net_stop_disable(void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	/* Disable NAPI if needed. */
	if (info != NULL && info->napi_enabled) {
		napi_disable(&info->napi);
		info->napi_enabled = false;
	}
}


/*
 * Enable NAPI and the ingress interrupt for the given device
 * on the current cpu.
 *
 * ISSUE: Only do this for "network cpus"?
 */
static void tile_net_open_enable(void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	/* Enable NAPI. */
	napi_enable(&info->napi);
	info->napi_enabled = true;

	/* Enable the ingress interrupt. */
	enable_percpu_irq(priv->intr_id, 0);
}


/*
 * tile_net_open_inner does most of the work of bringing up the interface.
 * It's called from tile_net_open(), and also from tile_net_retry_open().
 * The return value is 0 if the interface was brought up, < 0 if
 * tile_net_open() should return the return value as an error, and > 0 if
 * tile_net_open() should return success and schedule a work item to
 * periodically retry the bringup.
 */
static int tile_net_open_inner(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info;
	struct tile_netio_queue *queue;
	int result = 0;
	int i;
	int dummy = 0;

	/*
	 * First try to register just on the local CPU, and handle any
	 * semi-expected "link down" failure specially.  Note that we
	 * do NOT call "tile_net_stop_aux()", unlike below.
	 */
	tile_net_register(dev);
	info = priv->cpu[my_cpu];
	if (!info->registered) {
		if (info->link_down)
			return 1;
		return -EAGAIN;
	}

	/*
	 * Now register everywhere else.  If any registration fails,
	 * even for "link down" (which might not be possible), we
	 * clean up using "tile_net_stop_aux()".  Also, add all the
	 * "napi" objects (sequentially, to protect "dev->napi_list").
	 * ISSUE: Only use "netif_napi_add()" for "network cpus"?
	 */
	smp_call_function(tile_net_register, (void *)dev, 1);
	for_each_online_cpu(i) {
		struct tile_net_cpu *info = priv->cpu[i];
		if (info->registered)
			netif_napi_add(dev, &info->napi, tile_net_poll, 64);
		else
			result = -EAGAIN;
	}
	if (result != 0) {
		tile_net_stop_aux(dev);
		return result;
	}

	queue = &info->queue;

	if (priv->intr_id == 0) {
		unsigned int irq;

		/*
		 * Acquire the irq allocated by the hypervisor.  Every
		 * queue gets the same irq.  The "__intr_id" field is
		 * "1 << irq", so we use "__ffs()" to extract "irq".
		 */
		priv->intr_id = queue->__system_part->__intr_id;
		BUG_ON(priv->intr_id == 0);
		irq = __ffs(priv->intr_id);

		/*
		 * Register the ingress interrupt handler for this
		 * device, permanently.
		 *
		 * We used to call "free_irq()" in "tile_net_stop()",
		 * and then re-register the handler here every time,
		 * but that caused DNP errors in "handle_IRQ_event()"
		 * because "desc->action" was NULL.  See bug 9143.
		 */
		tile_irq_activate(irq, TILE_IRQ_PERCPU);
		BUG_ON(request_irq(irq, tile_net_handle_ingress_interrupt,
				   0, dev->name, (void *)dev) != 0);
	}

	{
		/* Allocate initial buffers. */

		int max_buffers =
			priv->network_cpus_count * priv->network_cpus_credits;

		info->num_needed_small_buffers =
			min(LIPP_SMALL_BUFFERS, max_buffers);

		info->num_needed_large_buffers =
			min(LIPP_LARGE_BUFFERS, max_buffers);

		tile_net_provide_needed_buffers(info);

		if (info->num_needed_small_buffers != 0 ||
		    info->num_needed_large_buffers != 0)
			panic("Insufficient memory for buffer stack!");
	}

	/* We are about to be active. */
	priv->active = true;

	/* Make sure "active" is visible to all tiles. */
	mb();

	/* On each tile, enable NAPI and the ingress interrupt. */
	on_each_cpu(tile_net_open_enable, (void *)dev, 1);

	/* Start LIPP/LEPP and activate "ingress" at the shim. */
	if (hv_dev_pwrite(priv->hv_devhdl, 0, (HV_VirtAddr)&dummy,
			  sizeof(dummy), NETIO_IPP_INPUT_INIT_OFF) < 0)
		panic("Failed to activate the LIPP Shim!\n");

	/* Start our transmit queue. */
	netif_start_queue(dev);

	return 0;
}


/*
 * Called periodically to retry bringing up the NetIO interface,
 * if it doesn't come up cleanly during tile_net_open().
 */
static void tile_net_open_retry(struct work_struct *w)
{
	struct delayed_work *dw = to_delayed_work(w);

	struct tile_net_priv *priv =
		container_of(dw, struct tile_net_priv, retry_work);

	/*
	 * Try to bring the NetIO interface up.  If it fails, reschedule
	 * ourselves to try again later; otherwise, tell Linux we now have
	 * a working link.  ISSUE: What if the return value is negative?
	 */
	if (tile_net_open_inner(priv->dev) != 0)
		schedule_delayed_work(&priv->retry_work,
				      TILE_NET_RETRY_INTERVAL);
	else
		netif_carrier_on(priv->dev);
}


/*
 * Called when a network interface is made active.
 *
 * Returns 0 on success, negative value on failure.
 *
 * The open entry point is called when a network interface is made
 * active by the system (IFF_UP).  At this point all resources needed
 * for transmit and receive operations are allocated, the interrupt
 * handler is registered with the OS (if needed), the watchdog timer
 * is started, and the stack is notified that the interface is ready.
 *
 * If the actual link is not available yet, then we tell Linux that
 * we have no carrier, and we keep checking until the link comes up.
 */
static int tile_net_open(struct net_device *dev)
{
	int ret = 0;
	struct tile_net_priv *priv = netdev_priv(dev);

	/*
	 * We rely on priv->partly_opened to tell us if this is the
	 * first time this interface is being brought up. If it is
	 * set, the IPP was already initialized and should not be
	 * initialized again.
	 */
	if (!priv->partly_opened) {

		int count;
		int credits;

		/* Initialize LIPP/LEPP, and start the Shim. */
		ret = tile_net_open_aux(dev);
		if (ret < 0) {
			pr_err("tile_net_open_aux failed: %d\n", ret);
			return ret;
		}

		/* Analyze the network cpus. */

		if (network_cpus_used)
			cpumask_copy(&priv->network_cpus_map,
				     &network_cpus_map);
		else
			cpumask_copy(&priv->network_cpus_map, cpu_online_mask);


		count = cpumask_weight(&priv->network_cpus_map);

		/* Limit credits to available buffers, and apply min. */
		credits = max(16, (LIPP_LARGE_BUFFERS / count) & ~1);

		/* Apply "GBE" max limit. */
		/* ISSUE: Use higher limit for XGBE? */
		credits = min(NETIO_MAX_RECEIVE_PKTS, credits);

		priv->network_cpus_count = count;
		priv->network_cpus_credits = credits;

#ifdef TILE_NET_DEBUG
		pr_info("Using %d network cpus, with %d credits each\n",
		       priv->network_cpus_count, priv->network_cpus_credits);
#endif

		priv->partly_opened = true;

	} else {
		/* FIXME: Is this possible? */
		/* printk("Already partly opened.\n"); */
	}

	/*
	 * Attempt to bring up the link.
	 */
	ret = tile_net_open_inner(dev);
	if (ret <= 0) {
		if (ret == 0)
			netif_carrier_on(dev);
		return ret;
	}

	/*
	 * We were unable to bring up the NetIO interface, but we want to
	 * try again in a little bit.  Tell Linux that we have no carrier
	 * so it doesn't try to use the interface before the link comes up
	 * and then remember to try again later.
	 */
	netif_carrier_off(dev);
	schedule_delayed_work(&priv->retry_work, TILE_NET_RETRY_INTERVAL);

	return 0;
}


static int tile_net_drain_lipp_buffers(struct tile_net_priv *priv)
{
	int n = 0;

	/* Drain all the LIPP buffers. */
	while (true) {
		unsigned int buffer;

		/* NOTE: This should never fail. */
		if (hv_dev_pread(priv->hv_devhdl, 0, (HV_VirtAddr)&buffer,
				 sizeof(buffer), NETIO_IPP_DRAIN_OFF) < 0)
			break;

		/* Stop when done. */
		if (buffer == 0)
			break;

		{
			/* Convert "linux_buffer_t" to "va". */
			void *va = __va((phys_addr_t)(buffer >> 1) << 7);

			/* Acquire the associated "skb". */
			struct sk_buff **skb_ptr = va - sizeof(*skb_ptr);
			struct sk_buff *skb = *skb_ptr;

			kfree_skb(skb);
		}

		n++;
	}

	return n;
}


/*
 * Disables a network interface.
 *
 * Returns 0, this is not allowed to fail.
 *
 * The close entry point is called when an interface is de-activated
 * by the OS.  The hardware is still under the drivers control, but
 * needs to be disabled.  A global MAC reset is issued to stop the
 * hardware, and all transmit and receive resources are freed.
 *
 * ISSUE: How closely does "netif_running(dev)" mirror "priv->active"?
 *
 * Before we are called by "__dev_close()", "netif_running()" will
 * have been cleared, so no NEW calls to "tile_net_poll()" will be
 * made by "netpoll_poll_dev()".
 *
 * Often, this can cause some tiles to still have packets in their
 * queues, so we must call "tile_net_discard_packets()" later.
 *
 * Note that some other tile may still be INSIDE "tile_net_poll()",
 * and in fact, many will be, if there is heavy network load.
 *
 * Calling "on_each_cpu(tile_net_stop_disable, (void *)dev, 1)" when
 * any tile is still "napi_schedule()"'d will induce a horrible crash
 * when "msleep()" is called.  This includes tiles which are inside
 * "tile_net_poll()" which have not yet called "napi_complete()".
 *
 * So, we must first try to wait long enough for other tiles to finish
 * with any current "tile_net_poll()" call, and, hopefully, to clear
 * the "scheduled" flag.  ISSUE: It is unclear what happens to tiles
 * which have called "napi_schedule()" but which had not yet tried to
 * call "tile_net_poll()", or which exhausted their budget inside
 * "tile_net_poll()" just before this function was called.
 */
static int tile_net_stop(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);

	PDEBUG("tile_net_stop()\n");

	/* Start discarding packets. */
	priv->active = false;

	/* Make sure "active" is visible to all tiles. */
	mb();

	/*
	 * On each tile, make sure no NEW packets get delivered, and
	 * disable the ingress interrupt.
	 *
	 * Note that the ingress interrupt can fire AFTER this,
	 * presumably due to packets which were recently delivered,
	 * but it will have no effect.
	 */
	on_each_cpu(tile_net_deregister, (void *)dev, 1);

	/* Optimistically drain LIPP buffers. */
	(void)tile_net_drain_lipp_buffers(priv);

	/* ISSUE: Only needed if not yet fully open. */
	cancel_delayed_work_sync(&priv->retry_work);

	/* Can't transmit any more. */
	netif_stop_queue(dev);

	/* Disable NAPI on each tile. */
	on_each_cpu(tile_net_stop_disable, (void *)dev, 1);

	/*
	 * Drain any remaining LIPP buffers.  NOTE: This "printk()"
	 * has never been observed, but in theory it could happen.
	 */
	if (tile_net_drain_lipp_buffers(priv) != 0)
		printk("Had to drain some extra LIPP buffers!\n");

	/* Stop LIPP/LEPP. */
	tile_net_stop_aux(dev);

	/*
	 * ISSUE: It appears that, in practice anyway, by the time we
	 * get here, there are no pending completions, but just in case,
	 * we free (all of) them anyway.
	 */
	while (tile_net_lepp_free_comps(dev, true))
		/* loop */;

	/* Wipe the EPP queue, and wait till the stores hit the EPP. */
	memset(priv->eq, 0, sizeof(lepp_queue_t));
	mb();

	return 0;
}


/*
 * Prepare the "frags" info for the resulting LEPP command.
 *
 * If needed, flush the memory used by the frags.
 */
static unsigned int tile_net_tx_frags(lepp_frag_t *frags,
				      struct sk_buff *skb,
				      void *b_data, unsigned int b_len)
{
	unsigned int i, n = 0;

	struct skb_shared_info *sh = skb_shinfo(skb);

	phys_addr_t cpa;

	if (b_len != 0) {

		if (!hash_default)
			finv_buffer_remote(b_data, b_len, 0);

		cpa = __pa(b_data);
		frags[n].cpa_lo = cpa;
		frags[n].cpa_hi = cpa >> 32;
		frags[n].length = b_len;
		frags[n].hash_for_home = hash_default;
		n++;
	}

	for (i = 0; i < sh->nr_frags; i++) {

		skb_frag_t *f = &sh->frags[i];
		unsigned long pfn = page_to_pfn(skb_frag_page(f));

		/* FIXME: Compute "hash_for_home" properly. */
		/* ISSUE: The hypervisor checks CHIP_HAS_REV1_DMA_PACKETS(). */
		int hash_for_home = hash_default;

		/* FIXME: Hmmm. */
		if (!hash_default) {
			void *va = pfn_to_kaddr(pfn) + f->page_offset;
			BUG_ON(PageHighMem(skb_frag_page(f)));
			finv_buffer_remote(va, skb_frag_size(f), 0);
		}

		cpa = ((phys_addr_t)pfn << PAGE_SHIFT) + f->page_offset;
		frags[n].cpa_lo = cpa;
		frags[n].cpa_hi = cpa >> 32;
		frags[n].length = skb_frag_size(f);
		frags[n].hash_for_home = hash_for_home;
		n++;
	}

	return n;
}


/*
 * This function takes "skb", consisting of a header template and a
 * payload, and hands it to LEPP, to emit as one or more segments,
 * each consisting of a possibly modified header, plus a piece of the
 * payload, via a process known as "tcp segmentation offload".
 *
 * Usually, "data" will contain the header template, of size "sh_len",
 * and "sh->frags" will contain "skb->data_len" bytes of payload, and
 * there will be "sh->gso_segs" segments.
 *
 * Sometimes, if "sendfile()" requires copying, we will be called with
 * "data" containing the header and payload, with "frags" being empty.
 *
 * Sometimes, for example when using NFS over TCP, a single segment can
 * span 3 fragments, which must be handled carefully in LEPP.
 *
 * See "emulate_large_send_offload()" for some reference code, which
 * does not handle checksumming.
 *
 * ISSUE: How do we make sure that high memory DMA does not migrate?
 */
static int tile_net_tx_tso(struct sk_buff *skb, struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];
	struct tile_net_stats_t *stats = &info->stats;

	struct skb_shared_info *sh = skb_shinfo(skb);

	unsigned char *data = skb->data;

	/* The ip header follows the ethernet header. */
	struct iphdr *ih = ip_hdr(skb);
	unsigned int ih_len = ih->ihl * 4;

	/* Note that "nh == ih", by definition. */
	unsigned char *nh = skb_network_header(skb);
	unsigned int eh_len = nh - data;

	/* The tcp header follows the ip header. */
	struct tcphdr *th = (struct tcphdr *)(nh + ih_len);
	unsigned int th_len = th->doff * 4;

	/* The total number of header bytes. */
	/* NOTE: This may be less than skb_headlen(skb). */
	unsigned int sh_len = eh_len + ih_len + th_len;

	/* The number of payload bytes at "skb->data + sh_len". */
	/* This is non-zero for sendfile() without HIGHDMA. */
	unsigned int b_len = skb_headlen(skb) - sh_len;

	/* The total number of payload bytes. */
	unsigned int d_len = b_len + skb->data_len;

	/* The maximum payload size. */
	unsigned int p_len = sh->gso_size;

	/* The total number of segments. */
	unsigned int num_segs = sh->gso_segs;

	/* The temporary copy of the command. */
	u32 cmd_body[(LEPP_MAX_CMD_SIZE + 3) / 4];
	lepp_tso_cmd_t *cmd = (lepp_tso_cmd_t *)cmd_body;

	/* Analyze the "frags". */
	unsigned int num_frags =
		tile_net_tx_frags(cmd->frags, skb, data + sh_len, b_len);

	/* The size of the command, including frags and header. */
	size_t cmd_size = LEPP_TSO_CMD_SIZE(num_frags, sh_len);

	/* The command header. */
	lepp_tso_cmd_t cmd_init = {
		.tso = true,
		.header_size = sh_len,
		.ip_offset = eh_len,
		.tcp_offset = eh_len + ih_len,
		.payload_size = p_len,
		.num_frags = num_frags,
	};

	unsigned long irqflags;

	lepp_queue_t *eq = priv->eq;

	struct sk_buff *olds[8];
	unsigned int wanted = 8;
	unsigned int i, nolds = 0;

	unsigned int cmd_head, cmd_tail, cmd_next;
	unsigned int comp_tail;


	/* Paranoia. */
	BUG_ON(skb->protocol != htons(ETH_P_IP));
	BUG_ON(ih->protocol != IPPROTO_TCP);
	BUG_ON(skb->ip_summed != CHECKSUM_PARTIAL);
	BUG_ON(num_frags > LEPP_MAX_FRAGS);
	/*--BUG_ON(num_segs != (d_len + (p_len - 1)) / p_len); */
	BUG_ON(num_segs <= 1);


	/* Finish preparing the command. */

	/* Copy the command header. */
	*cmd = cmd_init;

	/* Copy the "header". */
	memcpy(&cmd->frags[num_frags], data, sh_len);


	/* Prefetch and wait, to minimize time spent holding the spinlock. */
	prefetch_L1(&eq->comp_tail);
	prefetch_L1(&eq->cmd_tail);
	mb();


	/* Enqueue the command. */

	spin_lock_irqsave(&priv->eq_lock, irqflags);

	/* Handle completions if needed to make room. */
	/* NOTE: Return NETDEV_TX_BUSY if there is still no room. */
	if (lepp_num_free_comp_slots(eq) == 0) {
		nolds = tile_net_lepp_grab_comps(eq, olds, wanted, 0);
		if (nolds == 0) {
busy:
			spin_unlock_irqrestore(&priv->eq_lock, irqflags);
			return NETDEV_TX_BUSY;
		}
	}

	cmd_head = eq->cmd_head;
	cmd_tail = eq->cmd_tail;

	/* Prepare to advance, detecting full queue. */
	/* NOTE: Return NETDEV_TX_BUSY if the queue is full. */
	cmd_next = cmd_tail + cmd_size;
	if (cmd_tail < cmd_head && cmd_next >= cmd_head)
		goto busy;
	if (cmd_next > LEPP_CMD_LIMIT) {
		cmd_next = 0;
		if (cmd_next == cmd_head)
			goto busy;
	}

	/* Copy the command. */
	memcpy(&eq->cmds[cmd_tail], cmd, cmd_size);

	/* Advance. */
	cmd_tail = cmd_next;

	/* Record "skb" for eventual freeing. */
	comp_tail = eq->comp_tail;
	eq->comps[comp_tail] = skb;
	LEPP_QINC(comp_tail);
	eq->comp_tail = comp_tail;

	/* Flush before allowing LEPP to handle the command. */
	/* ISSUE: Is this the optimal location for the flush? */
	__insn_mf();

	eq->cmd_tail = cmd_tail;

	/* NOTE: Using "4" here is more efficient than "0" or "2", */
	/* and, strangely, more efficient than pre-checking the number */
	/* of available completions, and comparing it to 4. */
	if (nolds == 0)
		nolds = tile_net_lepp_grab_comps(eq, olds, wanted, 4);

	spin_unlock_irqrestore(&priv->eq_lock, irqflags);

	/* Handle completions. */
	for (i = 0; i < nolds; i++)
		dev_consume_skb_any(olds[i]);

	/* Update stats. */
	u64_stats_update_begin(&stats->syncp);
	stats->tx_packets += num_segs;
	stats->tx_bytes += (num_segs * sh_len) + d_len;
	u64_stats_update_end(&stats->syncp);

	/* Make sure the egress timer is scheduled. */
	tile_net_schedule_egress_timer(info);

	return NETDEV_TX_OK;
}


/*
 * Transmit a packet (called by the kernel via "hard_start_xmit" hook).
 */
static int tile_net_tx(struct sk_buff *skb, struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];
	struct tile_net_stats_t *stats = &info->stats;

	unsigned long irqflags;

	struct skb_shared_info *sh = skb_shinfo(skb);

	unsigned int len = skb->len;
	unsigned char *data = skb->data;

	unsigned int csum_start = skb_checksum_start_offset(skb);

	lepp_frag_t frags[1 + MAX_SKB_FRAGS];

	unsigned int num_frags;

	lepp_queue_t *eq = priv->eq;

	struct sk_buff *olds[8];
	unsigned int wanted = 8;
	unsigned int i, nolds = 0;

	unsigned int cmd_size = sizeof(lepp_cmd_t);

	unsigned int cmd_head, cmd_tail, cmd_next;
	unsigned int comp_tail;

	lepp_cmd_t cmds[1 + MAX_SKB_FRAGS];


	/*
	 * This is paranoia, since we think that if the link doesn't come
	 * up, telling Linux we have no carrier will keep it from trying
	 * to transmit.  If it does, though, we can't execute this routine,
	 * since data structures we depend on aren't set up yet.
	 */
	if (!info->registered)
		return NETDEV_TX_BUSY;


	/* Save the timestamp. */
	netif_trans_update(dev);


#ifdef TILE_NET_PARANOIA
#if CHIP_HAS_CBOX_HOME_MAP()
	if (hash_default) {
		HV_PTE pte = *virt_to_pte(current->mm, (unsigned long)data);
		if (hv_pte_get_mode(pte) != HV_PTE_MODE_CACHE_HASH_L3)
			panic("Non-HFH egress buffer! VA=%p Mode=%d PTE=%llx",
			      data, hv_pte_get_mode(pte), hv_pte_val(pte));
	}
#endif
#endif


#ifdef TILE_NET_DUMP_PACKETS
	/* ISSUE: Does not dump the "frags". */
	dump_packet(data, skb_headlen(skb), "tx");
#endif /* TILE_NET_DUMP_PACKETS */


	if (sh->gso_size != 0)
		return tile_net_tx_tso(skb, dev);


	/* Prepare the commands. */

	num_frags = tile_net_tx_frags(frags, skb, data, skb_headlen(skb));

	for (i = 0; i < num_frags; i++) {

		bool final = (i == num_frags - 1);

		lepp_cmd_t cmd = {
			.cpa_lo = frags[i].cpa_lo,
			.cpa_hi = frags[i].cpa_hi,
			.length = frags[i].length,
			.hash_for_home = frags[i].hash_for_home,
			.send_completion = final,
			.end_of_packet = final
		};

		if (i == 0 && skb->ip_summed == CHECKSUM_PARTIAL) {
			cmd.compute_checksum = 1;
			cmd.checksum_data.bits.start_byte = csum_start;
			cmd.checksum_data.bits.count = len - csum_start;
			cmd.checksum_data.bits.destination_byte =
				csum_start + skb->csum_offset;
		}

		cmds[i] = cmd;
	}


	/* Prefetch and wait, to minimize time spent holding the spinlock. */
	prefetch_L1(&eq->comp_tail);
	prefetch_L1(&eq->cmd_tail);
	mb();


	/* Enqueue the commands. */

	spin_lock_irqsave(&priv->eq_lock, irqflags);

	/* Handle completions if needed to make room. */
	/* NOTE: Return NETDEV_TX_BUSY if there is still no room. */
	if (lepp_num_free_comp_slots(eq) == 0) {
		nolds = tile_net_lepp_grab_comps(eq, olds, wanted, 0);
		if (nolds == 0) {
busy:
			spin_unlock_irqrestore(&priv->eq_lock, irqflags);
			return NETDEV_TX_BUSY;
		}
	}

	cmd_head = eq->cmd_head;
	cmd_tail = eq->cmd_tail;

	/* Copy the commands, or fail. */
	/* NOTE: Return NETDEV_TX_BUSY if the queue is full. */
	for (i = 0; i < num_frags; i++) {

		/* Prepare to advance, detecting full queue. */
		cmd_next = cmd_tail + cmd_size;
		if (cmd_tail < cmd_head && cmd_next >= cmd_head)
			goto busy;
		if (cmd_next > LEPP_CMD_LIMIT) {
			cmd_next = 0;
			if (cmd_next == cmd_head)
				goto busy;
		}

		/* Copy the command. */
		*(lepp_cmd_t *)&eq->cmds[cmd_tail] = cmds[i];

		/* Advance. */
		cmd_tail = cmd_next;
	}

	/* Record "skb" for eventual freeing. */
	comp_tail = eq->comp_tail;
	eq->comps[comp_tail] = skb;
	LEPP_QINC(comp_tail);
	eq->comp_tail = comp_tail;

	/* Flush before allowing LEPP to handle the command. */
	/* ISSUE: Is this the optimal location for the flush? */
	__insn_mf();

	eq->cmd_tail = cmd_tail;

	/* NOTE: Using "4" here is more efficient than "0" or "2", */
	/* and, strangely, more efficient than pre-checking the number */
	/* of available completions, and comparing it to 4. */
	if (nolds == 0)
		nolds = tile_net_lepp_grab_comps(eq, olds, wanted, 4);

	spin_unlock_irqrestore(&priv->eq_lock, irqflags);

	/* Handle completions. */
	for (i = 0; i < nolds; i++)
		dev_consume_skb_any(olds[i]);

	/* HACK: Track "expanded" size for short packets (e.g. 42 < 60). */
	u64_stats_update_begin(&stats->syncp);
	stats->tx_packets++;
	stats->tx_bytes += ((len >= ETH_ZLEN) ? len : ETH_ZLEN);
	u64_stats_update_end(&stats->syncp);

	/* Make sure the egress timer is scheduled. */
	tile_net_schedule_egress_timer(info);

	return NETDEV_TX_OK;
}


/*
 * Deal with a transmit timeout.
 */
static void tile_net_tx_timeout(struct net_device *dev)
{
	PDEBUG("tile_net_tx_timeout()\n");
	PDEBUG("Transmit timeout at %ld, latency %ld\n", jiffies,
	       jiffies - dev_trans_start(dev));

	/* XXX: ISSUE: This doesn't seem useful for us. */
	netif_wake_queue(dev);
}


/*
 * Ioctl commands.
 */
static int tile_net_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	return -EOPNOTSUPP;
}


/*
 * Get System Network Statistics.
 *
 * Returns the address of the device statistics structure.
 */
static struct rtnl_link_stats64 *tile_net_get_stats64(struct net_device *dev,
		struct rtnl_link_stats64 *stats)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	u64 rx_packets = 0, tx_packets = 0;
	u64 rx_bytes = 0, tx_bytes = 0;
	u64 rx_errors = 0, rx_dropped = 0;
	int i;

	for_each_online_cpu(i) {
		struct tile_net_stats_t *cpu_stats;
		u64 trx_packets, ttx_packets, trx_bytes, ttx_bytes;
		u64 trx_errors, trx_dropped;
		unsigned int start;

		if (priv->cpu[i] == NULL)
			continue;
		cpu_stats = &priv->cpu[i]->stats;

		do {
			start = u64_stats_fetch_begin_irq(&cpu_stats->syncp);
			trx_packets = cpu_stats->rx_packets;
			ttx_packets = cpu_stats->tx_packets;
			trx_bytes   = cpu_stats->rx_bytes;
			ttx_bytes   = cpu_stats->tx_bytes;
			trx_errors  = cpu_stats->rx_errors;
			trx_dropped = cpu_stats->rx_dropped;
		} while (u64_stats_fetch_retry_irq(&cpu_stats->syncp, start));

		rx_packets += trx_packets;
		tx_packets += ttx_packets;
		rx_bytes   += trx_bytes;
		tx_bytes   += ttx_bytes;
		rx_errors  += trx_errors;
		rx_dropped += trx_dropped;
	}

	stats->rx_packets = rx_packets;
	stats->tx_packets = tx_packets;
	stats->rx_bytes   = rx_bytes;
	stats->tx_bytes   = tx_bytes;
	stats->rx_errors  = rx_errors;
	stats->rx_dropped = rx_dropped;

	return stats;
}


/*
 * Change the "mtu".
 *
 * The "change_mtu" method is usually not needed.
 * If you need it, it must be like this.
 */
static int tile_net_change_mtu(struct net_device *dev, int new_mtu)
{
	PDEBUG("tile_net_change_mtu()\n");

	/* Check ranges. */
	if ((new_mtu < 68) || (new_mtu > 1500))
		return -EINVAL;

	/* Accept the value. */
	dev->mtu = new_mtu;

	return 0;
}


/*
 * Change the Ethernet Address of the NIC.
 *
 * The hypervisor driver does not support changing MAC address.  However,
 * the IPP does not do anything with the MAC address, so the address which
 * gets used on outgoing packets, and which is accepted on incoming packets,
 * is completely up to the NetIO program or kernel driver which is actually
 * handling them.
 *
 * Returns 0 on success, negative on failure.
 */
static int tile_net_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	/* ISSUE: Note that "dev_addr" is now a pointer. */
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	return 0;
}


/*
 * Obtain the MAC address from the hypervisor.
 * This must be done before opening the device.
 */
static int tile_net_get_mac(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);

	char hv_dev_name[32];
	int len;

	__netio_getset_offset_t offset = { .word = NETIO_IPP_PARAM_OFF };

	int ret;

	/* For example, "xgbe0". */
	strcpy(hv_dev_name, dev->name);
	len = strlen(hv_dev_name);

	/* For example, "xgbe/0". */
	hv_dev_name[len] = hv_dev_name[len - 1];
	hv_dev_name[len - 1] = '/';
	len++;

	/* For example, "xgbe/0/native_hash". */
	strcpy(hv_dev_name + len, hash_default ? "/native_hash" : "/native");

	/* Get the hypervisor handle for this device. */
	priv->hv_devhdl = hv_dev_open((HV_VirtAddr)hv_dev_name, 0);
	PDEBUG("hv_dev_open(%s) returned %d %p\n",
	       hv_dev_name, priv->hv_devhdl, &priv->hv_devhdl);
	if (priv->hv_devhdl < 0) {
		if (priv->hv_devhdl == HV_ENODEV)
			printk(KERN_DEBUG "Ignoring unconfigured device %s\n",
				 hv_dev_name);
		else
			printk(KERN_DEBUG "hv_dev_open(%s) returned %d\n",
				 hv_dev_name, priv->hv_devhdl);
		return -1;
	}

	/*
	 * Read the hardware address from the hypervisor.
	 * ISSUE: Note that "dev_addr" is now a pointer.
	 */
	offset.bits.class = NETIO_PARAM;
	offset.bits.addr = NETIO_PARAM_MAC;
	ret = hv_dev_pread(priv->hv_devhdl, 0,
			   (HV_VirtAddr)dev->dev_addr, dev->addr_len,
			   offset.word);
	PDEBUG("hv_dev_pread(NETIO_PARAM_MAC) returned %d\n", ret);
	if (ret <= 0) {
		printk(KERN_DEBUG "hv_dev_pread(NETIO_PARAM_MAC) %s failed\n",
		       dev->name);
		/*
		 * Since the device is configured by the hypervisor but we
		 * can't get its MAC address, we are most likely running
		 * the simulator, so let's generate a random MAC address.
		 */
		eth_hw_addr_random(dev);
	}

	return 0;
}


#ifdef CONFIG_NET_POLL_CONTROLLER
/*
 * Polling 'interrupt' - used by things like netconsole to send skbs
 * without having to re-enable interrupts. It's not called while
 * the interrupt routine is executing.
 */
static void tile_net_netpoll(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	disable_percpu_irq(priv->intr_id);
	tile_net_handle_ingress_interrupt(priv->intr_id, dev);
	enable_percpu_irq(priv->intr_id, 0);
}
#endif


static const struct net_device_ops tile_net_ops = {
	.ndo_open = tile_net_open,
	.ndo_stop = tile_net_stop,
	.ndo_start_xmit = tile_net_tx,
	.ndo_do_ioctl = tile_net_ioctl,
	.ndo_get_stats64 = tile_net_get_stats64,
	.ndo_change_mtu = tile_net_change_mtu,
	.ndo_tx_timeout = tile_net_tx_timeout,
	.ndo_set_mac_address = tile_net_set_mac_address,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = tile_net_netpoll,
#endif
};


/*
 * The setup function.
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
	dev->tx_queue_len = TILE_NET_TX_QUEUE_LEN;
	dev->mtu = TILE_NET_MTU;

	features |= NETIF_F_HW_CSUM;
	features |= NETIF_F_SG;

	/* We support TSO iff the HV supports sufficient frags. */
	if (LEPP_MAX_FRAGS >= 1 + MAX_SKB_FRAGS)
		features |= NETIF_F_TSO;

	/* We can't support HIGHDMA without hash_default, since we need
	 * to be able to finv() with a VA if we don't have hash_default.
	 */
	if (hash_default)
		features |= NETIF_F_HIGHDMA;

	dev->hw_features   |= features;
	dev->vlan_features |= features;
	dev->features      |= features;
}


/*
 * Allocate the device structure, register the device, and obtain the
 * MAC address from the hypervisor.
 */
static struct net_device *tile_net_dev_init(const char *name)
{
	int ret;
	struct net_device *dev;
	struct tile_net_priv *priv;

	/*
	 * Allocate the device structure.  This allocates "priv", calls
	 * tile_net_setup(), and saves "name".  Normally, "name" is a
	 * template, instantiated by register_netdev(), but not for us.
	 */
	dev = alloc_netdev(sizeof(*priv), name, NET_NAME_UNKNOWN,
			   tile_net_setup);
	if (!dev) {
		pr_err("alloc_netdev(%s) failed\n", name);
		return NULL;
	}

	priv = netdev_priv(dev);

	/* Initialize "priv". */

	memset(priv, 0, sizeof(*priv));

	/* Save "dev" for "tile_net_open_retry()". */
	priv->dev = dev;

	INIT_DELAYED_WORK(&priv->retry_work, tile_net_open_retry);

	spin_lock_init(&priv->eq_lock);

	/* Allocate "eq". */
	priv->eq_pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, EQ_ORDER);
	if (!priv->eq_pages) {
		free_netdev(dev);
		return NULL;
	}
	priv->eq = page_address(priv->eq_pages);

	/* Register the network device. */
	ret = register_netdev(dev);
	if (ret) {
		pr_err("register_netdev %s failed %d\n", dev->name, ret);
		__free_pages(priv->eq_pages, EQ_ORDER);
		free_netdev(dev);
		return NULL;
	}

	/* Get the MAC address. */
	ret = tile_net_get_mac(dev);
	if (ret < 0) {
		unregister_netdev(dev);
		__free_pages(priv->eq_pages, EQ_ORDER);
		free_netdev(dev);
		return NULL;
	}

	return dev;
}


/*
 * Module cleanup.
 *
 * FIXME: If compiled as a module, this module cannot be "unloaded",
 * because the "ingress interrupt handler" is registered permanently.
 */
static void tile_net_cleanup(void)
{
	int i;

	for (i = 0; i < TILE_NET_DEVS; i++) {
		if (tile_net_devs[i]) {
			struct net_device *dev = tile_net_devs[i];
			struct tile_net_priv *priv = netdev_priv(dev);
			unregister_netdev(dev);
			finv_buffer_remote(priv->eq, EQ_SIZE, 0);
			__free_pages(priv->eq_pages, EQ_ORDER);
			free_netdev(dev);
		}
	}
}


/*
 * Module initialization.
 */
static int tile_net_init_module(void)
{
	pr_info("Tilera Network Driver\n");

	tile_net_devs[0] = tile_net_dev_init("xgbe0");
	tile_net_devs[1] = tile_net_dev_init("xgbe1");
	tile_net_devs[2] = tile_net_dev_init("gbe0");
	tile_net_devs[3] = tile_net_dev_init("gbe1");

	return 0;
}


module_init(tile_net_init_module);
module_exit(tile_net_cleanup);


#ifndef MODULE

/*
 * The "network_cpus" boot argument specifies the cpus that are dedicated
 * to handle ingress packets.
 *
 * The parameter should be in the form "network_cpus=m-n[,x-y]", where
 * m, n, x, y are integer numbers that represent the cpus that can be
 * neither a dedicated cpu nor a dataplane cpu.
 */
static int __init network_cpus_setup(char *str)
{
	int rc = cpulist_parse_crop(str, &network_cpus_map);
	if (rc != 0) {
		pr_warn("network_cpus=%s: malformed cpu list\n", str);
	} else {

		/* Remove dedicated cpus. */
		cpumask_and(&network_cpus_map, &network_cpus_map,
			    cpu_possible_mask);


		if (cpumask_empty(&network_cpus_map)) {
			pr_warn("Ignoring network_cpus='%s'\n", str);
		} else {
			pr_info("Linux network CPUs: %*pbl\n",
				cpumask_pr_args(&network_cpus_map));
			network_cpus_used = true;
		}
	}

	return 0;
}
__setup("network_cpus=", network_cpus_setup);

#endif
