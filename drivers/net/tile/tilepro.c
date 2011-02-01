/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
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
#include <asm/checksum.h>
#include <asm/homecache.h>

#include <hv/drv_xgbe_intf.h>
#include <hv/drv_xgbe_impl.h>
#include <hv/hypervisor.h>
#include <hv/netio_intf.h>

/* For TSO */
#include <linux/ip.h>
#include <linux/tcp.h>


/* There is no singlethread_cpu, so schedule work on the current cpu. */
#define singlethread_cpu -1


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
 *
 * NOTE: The use of "native_driver" ensures that EPP exists, and that
 * "epp_sendv" is legal, and that "LIPP" is being used.
 *
 * NOTE: Failing to free completions for an arbitrarily long time
 * (which is defined to be illegal) does in fact cause bizarre
 * problems.  The "egress_timer" helps prevent this from happening.
 *
 * NOTE: The egress code can be interrupted by the interrupt handler.
 */


/* HACK: Allow use of "jumbo" packets. */
/* This should be 1500 if "jumbo" is not set in LIPP. */
/* This should be at most 10226 (10240 - 14) if "jumbo" is set in LIPP. */
/* ISSUE: This has not been thoroughly tested (except at 1500). */
#define TILE_NET_MTU 1500

/* HACK: Define to support GSO. */
/* ISSUE: This may actually hurt performance of the TCP blaster. */
/* #define TILE_NET_GSO */

/* Define this to collapse "duplicate" acks. */
/* #define IGNORE_DUP_ACKS */

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
	u32 rx_packets;
	u32 rx_bytes;
	u32 tx_packets;
	u32 tx_bytes;
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
	/* ISSUE: Is this needed? */
	bool napi_enabled;
	/* True if this tile has succcessfully registered with the IPP. */
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
	/* The actual egress queue. */
	lepp_queue_t *epp_queue;
	/* Protects "epp_queue->cmd_tail" and "epp_queue->comp_tail" */
	spinlock_t cmd_lock;
	/* Protects "epp_queue->comp_head". */
	spinlock_t comp_lock;
	/* The hypervisor handle for this interface. */
	int hv_devhdl;
	/* The intr bit mask that IDs this device. */
	u32 intr_id;
	/* True iff "tile_net_open_aux()" has succeeded. */
	int partly_opened;
	/* True iff "tile_net_open_inner()" has succeeded. */
	int fully_opened;
	/* Effective network cpus. */
	struct cpumask network_cpus_map;
	/* Number of network cpus. */
	int network_cpus_count;
	/* Credits per network cpu. */
	int network_cpus_credits;
	/* Network stats. */
	struct net_device_stats stats;
	/* For NetIO bringup retries. */
	struct delayed_work retry_work;
	/* Quick access to per cpu data. */
	struct tile_net_cpu *cpu[NR_CPUS];
};


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
	unsigned long i;
	static unsigned int count;

	pr_info("dump_packet(data %p, length 0x%lx s %s count 0x%x)\n",
	       data, length, s, count++);

	pr_info("\n");

	for (i = 0; i < length; i++) {
		if ((i & 0xf) == 0)
			sprintf(buf, "%8.8lx:", i);
		sprintf(buf + strlen(buf), " %2.2x", data[i]);
		if ((i & 0xf) == 0xf || i == length - 1)
			pr_info("%s\n", buf);
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
 */
static bool tile_net_provide_needed_buffer(struct tile_net_cpu *info,
					   bool small)
{
	/* ISSUE: What should we use here? */
	unsigned int large_size = NET_IP_ALIGN + TILE_NET_MTU + 100;

	/* Round up to ensure to avoid "false sharing" with last cache line. */
	unsigned int buffer_size =
		 (((small ? LIPP_SMALL_PACKET_SIZE : large_size) +
		   CHIP_L2_LINE_SIZE() - 1) & -CHIP_L2_LINE_SIZE());

	/*
	 * ISSUE: Since CPAs are 38 bits, and we can only encode the
	 * high 31 bits in a "linux_buffer_t", the low 7 bits must be
	 * zero, and thus, we must align the actual "va" mod 128.
	 */
	const unsigned long align = 128;

	struct sk_buff *skb;
	void *va;

	struct sk_buff **skb_ptr;

	/* Note that "dev_alloc_skb()" adds NET_SKB_PAD more bytes, */
	/* and also "reserves" that many bytes. */
	/* ISSUE: Can we "share" the NET_SKB_PAD bytes with "skb_ptr"? */
	int len = sizeof(*skb_ptr) + align + buffer_size;

	while (1) {

		/* Allocate (or fail). */
		skb = dev_alloc_skb(len);
		if (skb == NULL)
			return false;

		/* Make room for a back-pointer to 'skb'. */
		skb_reserve(skb, sizeof(*skb_ptr));

		/* Make sure we are aligned. */
		skb_reserve(skb, -(long)skb->data & (align - 1));

		/* This address is given to IPP. */
		va = skb->data;

		if (small)
			break;

		/* ISSUE: This has never been observed! */
		/* Large buffers must not span a huge page. */
		if (((((long)va & ~HPAGE_MASK) + 1535) & HPAGE_MASK) == 0)
			break;
		pr_err("Leaking unaligned linux buffer at %p.\n", va);
	}

	/* Skip two bytes to satisfy LIPP assumptions. */
	/* Note that this aligns IP on a 16 byte boundary. */
	/* ISSUE: Do this when the packet arrives? */
	skb_reserve(skb, NET_IP_ALIGN);

	/* Save a back-pointer to 'skb'. */
	skb_ptr = va - sizeof(*skb_ptr);
	*skb_ptr = skb;

	/* Invalidate the packet buffer. */
	if (!hash_default)
		__inv_buffer(skb->data, buffer_size);

	/* Make sure "skb_ptr" has been flushed. */
	__insn_mf();

#ifdef TILE_NET_PARANOIA
#if CHIP_HAS_CBOX_HOME_MAP()
	if (hash_default) {
		HV_PTE pte = *virt_to_pte(current->mm, (unsigned long)va);
		if (hv_pte_get_mode(pte) != HV_PTE_MODE_CACHE_HASH_L3)
			panic("Non-coherent ingress buffer!");
	}
#endif
#endif

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
 *
 * If "pending" is not NULL, it will be set to true if there might
 * still be some pending completions caused by this tile, else false.
 */
static unsigned int tile_net_lepp_grab_comps(struct net_device *dev,
					     struct sk_buff *comps[],
					     unsigned int comps_size,
					     bool *pending)
{
	struct tile_net_priv *priv = netdev_priv(dev);

	lepp_queue_t *eq = priv->epp_queue;

	unsigned int n = 0;

	unsigned int comp_head;
	unsigned int comp_busy;
	unsigned int comp_tail;

	spin_lock(&priv->comp_lock);

	comp_head = eq->comp_head;
	comp_busy = eq->comp_busy;
	comp_tail = eq->comp_tail;

	while (comp_head != comp_busy && n < comps_size) {
		comps[n++] = eq->comps[comp_head];
		LEPP_QINC(comp_head);
	}

	if (pending != NULL)
		*pending = (comp_head != comp_tail);

	eq->comp_head = comp_head;

	spin_unlock(&priv->comp_lock);

	return n;
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
		mod_timer_pinned(&info->egress_timer, jiffies + 1);
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

	struct sk_buff *olds[32];
	unsigned int wanted = 32;
	unsigned int i, nolds = 0;
	bool pending;

	/* The timer is no longer scheduled. */
	info->egress_timer_scheduled = false;

	nolds = tile_net_lepp_grab_comps(dev, olds, wanted, &pending);

	for (i = 0; i < nolds; i++)
		kfree_skb(olds[i]);

	/* Reschedule timer if needed. */
	if (pending)
		tile_net_schedule_egress_timer(info);
}


#ifdef IGNORE_DUP_ACKS

/*
 * Help detect "duplicate" ACKs.  These are sequential packets (for a
 * given flow) which are exactly 66 bytes long, sharing everything but
 * ID=2@0x12, Hsum=2@0x18, Ack=4@0x2a, WinSize=2@0x30, Csum=2@0x32,
 * Tstamps=10@0x38.  The ID's are +1, the Hsum's are -1, the Ack's are
 * +N, and the Tstamps are usually identical.
 *
 * NOTE: Apparently truly duplicate acks (with identical "ack" values),
 * should not be collapsed, as they are used for some kind of flow control.
 */
static bool is_dup_ack(char *s1, char *s2, unsigned int len)
{
	int i;

	unsigned long long ignorable = 0;

	/* Identification. */
	ignorable |= (1ULL << 0x12);
	ignorable |= (1ULL << 0x13);

	/* Header checksum. */
	ignorable |= (1ULL << 0x18);
	ignorable |= (1ULL << 0x19);

	/* ACK. */
	ignorable |= (1ULL << 0x2a);
	ignorable |= (1ULL << 0x2b);
	ignorable |= (1ULL << 0x2c);
	ignorable |= (1ULL << 0x2d);

	/* WinSize. */
	ignorable |= (1ULL << 0x30);
	ignorable |= (1ULL << 0x31);

	/* Checksum. */
	ignorable |= (1ULL << 0x32);
	ignorable |= (1ULL << 0x33);

	for (i = 0; i < len; i++, ignorable >>= 1) {

		if ((ignorable & 1) || (s1[i] == s2[i]))
			continue;

#ifdef TILE_NET_DEBUG
		/* HACK: Mention non-timestamp diffs. */
		if (i < 0x38 && i != 0x2f &&
		    net_ratelimit())
			pr_info("Diff at 0x%x\n", i);
#endif

		return false;
	}

#ifdef TILE_NET_NO_SUPPRESS_DUP_ACKS
	/* HACK: Do not suppress truly duplicate ACKs. */
	/* ISSUE: Is this actually necessary or helpful? */
	if (s1[0x2a] == s2[0x2a] &&
	    s1[0x2b] == s2[0x2b] &&
	    s1[0x2c] == s2[0x2c] &&
	    s1[0x2d] == s2[0x2d]) {
		return false;
	}
#endif

	return true;
}

#endif



/*
 * Like "tile_net_handle_packets()", but just discard packets.
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

		int index2_aux = index + sizeof(netio_pkt_t);
		int index2 =
			((index2_aux ==
			  qsp->__packet_receive_queue.__last_packet_plus_one) ?
			 0 : index2_aux);

		netio_pkt_t *pkt = (netio_pkt_t *)
			((unsigned long) &qsp[1] + index);

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

	/* Extract the packet size. */
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

#ifdef IGNORE_DUP_ACKS

	static int other;
	static int final;
	static int keep;
	static int skip;

#endif

	/* Invalidate the packet buffer. */
	if (!hash_default)
		__inv_buffer(buf, len);

	/* ISSUE: Is this needed? */
	dev->last_rx = jiffies;

#ifdef TILE_NET_DUMP_PACKETS
	dump_packet(buf, len, "rx");
#endif /* TILE_NET_DUMP_PACKETS */

#ifdef TILE_NET_VERIFY_INGRESS
	if (!NETIO_PKT_L4_CSUM_CORRECT_M(metadata, pkt) &&
	    NETIO_PKT_L4_CSUM_CALCULATED_M(metadata, pkt)) {
		/*
		 * FIXME: This complains about UDP packets
		 * with a "zero" checksum (bug 6624).
		 */
#ifdef TILE_NET_PANIC_ON_BAD
		dump_packet(buf, len, "rx");
		panic("Bad L4 checksum.");
#else
		pr_warning("Bad L4 checksum on %d byte packet.\n", len);
#endif
	}
	if (!NETIO_PKT_L3_CSUM_CORRECT_M(metadata, pkt) &&
	    NETIO_PKT_L3_CSUM_CALCULATED_M(metadata, pkt)) {
		dump_packet(buf, len, "rx");
		panic("Bad L3 checksum.");
	}
	switch (NETIO_PKT_STATUS_M(metadata, pkt)) {
	case NETIO_PKT_STATUS_OVERSIZE:
		if (len >= 64) {
			dump_packet(buf, len, "rx");
			panic("Unexpected OVERSIZE.");
		}
		break;
	case NETIO_PKT_STATUS_BAD:
#ifdef TILE_NET_PANIC_ON_BAD
		dump_packet(buf, len, "rx");
		panic("Unexpected BAD packet.");
#else
		pr_warning("Unexpected BAD %d byte packet.\n", len);
#endif
	}
#endif

	filter = 0;

	if (!(dev->flags & IFF_UP)) {
		/* Filter packets received before we're up. */
		filter = 1;
	} else if (!(dev->flags & IFF_PROMISC)) {
		/*
		 * FIXME: Implement HW multicast filter.
		 */
		if (is_unicast_ether_addr(buf)) {
			/* Filter packets not for our address. */
			const u8 *mine = dev->dev_addr;
			filter = compare_ether_addr(mine, buf);
		}
	}

#ifdef IGNORE_DUP_ACKS

	if (len != 66) {
		/* FIXME: Must check "is_tcp_ack(buf, len)" somehow. */

		other++;

	} else if (index2 ==
		   qsp->__packet_receive_queue.__packet_write) {

		final++;

	} else {

		netio_pkt_t *pkt2 = (netio_pkt_t *)
			((unsigned long) &qsp[1] + index2);

		netio_pkt_metadata_t *metadata2 =
			NETIO_PKT_METADATA(pkt2);

		/* Extract the packet size. */
		unsigned long len2 =
			(NETIO_PKT_CUSTOM_LENGTH(pkt2) +
			 NET_IP_ALIGN - NETIO_PACKET_PADDING);

		if (len2 == 66 &&
		    NETIO_PKT_FLOW_HASH_M(metadata, pkt) ==
		    NETIO_PKT_FLOW_HASH_M(metadata2, pkt2)) {

			/* Extract the "linux_buffer_t". */
			unsigned int buffer2 = pkt2->__packet.word;

			/* Convert "linux_buffer_t" to "va". */
			void *va2 =
				__va((phys_addr_t)(buffer2 >> 1) << 7);

			/* Extract the packet data pointer. */
			/* Compare to "NETIO_PKT_CUSTOM_DATA(pkt)". */
			unsigned char *buf2 = va2 + NET_IP_ALIGN;

			/* Invalidate the packet buffer. */
			if (!hash_default)
				__inv_buffer(buf2, len2);

			if (is_dup_ack(buf, buf2, len)) {
				skip++;
				filter = 1;
			} else {
				keep++;
			}
		}
	}

	if (net_ratelimit())
		pr_info("Other %d Final %d Keep %d Skip %d.\n",
			other, final, keep, skip);

#endif

	if (filter) {

		/* ISSUE: Update "drop" statistics? */

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

		/* ISSUE: Discard corrupt packets? */
		/* ISSUE: Discard packets with bad checksums? */

		/* Avoid recomputing TCP/UDP checksums. */
		if (NETIO_PKT_L4_CSUM_CORRECT_M(metadata, pkt))
			skb->ip_summed = CHECKSUM_UNNECESSARY;

		netif_receive_skb(skb);

		stats->rx_packets++;
		stats->rx_bytes += len;

		if (small)
			info->num_needed_small_buffers++;
		else
			info->num_needed_large_buffers++;
	}

	/* Return four credits after every fourth packet. */
	if (--qup->__receive_credit_remaining == 0) {
		u32 interval = qup->__receive_credit_interval;
		qup->__receive_credit_remaining = interval;
		__netio_fastio_return_credits(qup->__fastio_index, interval);
	}

	/* Consume this packet. */
	qup->__packet_receive_read = index2;

	return !filter;
}


/*
 * Handle some packets for the given device on the current CPU.
 *
 * ISSUE: The "rotting packet" race condition occurs if a packet
 * arrives after the queue appears to be empty, and before the
 * hypervisor interrupt is re-enabled.
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

	while (1) {
		int index = qup->__packet_receive_read;
		if (index == qsp->__packet_receive_queue.__packet_write)
			break;

		if (tile_net_poll_aux(info, index)) {
			if (++work >= budget)
				goto done;
		}
	}

	napi_complete(&info->napi);

	/* Re-enable hypervisor interrupts. */
	enable_percpu_irq(priv->intr_id);

	/* HACK: Avoid the "rotting packet" problem. */
	if (qup->__packet_receive_read !=
	    qsp->__packet_receive_queue.__packet_write)
		napi_schedule(&info->napi);

	/* ISSUE: Handle completions? */

done:

	tile_net_provide_needed_buffers(info);

	return work;
}


/*
 * Handle an ingress interrupt for the given device on the current cpu.
 */
static irqreturn_t tile_net_handle_ingress_interrupt(int irq, void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	/* Disable hypervisor interrupt. */
	disable_percpu_irq(priv->intr_id);

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
		struct page *page = virt_to_page(priv->epp_queue);
		homecache_change_page_home(page, 0, epp_home);
	}

	/*
	 * Register the EPP shared memory queue.
	 */
	{
		netio_ipp_address_t ea = {
			.va = 0,
			.pa = __pa(priv->epp_queue),
			.pte = hv_pte(0),
			.size = PAGE_SIZE,
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
		pr_warning("Failed to start LIPP/LEPP.\n");
		return -EIO;
	}

	return 0;
}


/*
 * Register with hypervisor on each CPU.
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
		info = &__get_cpu_var(hv_xgbe0);
	else if (!strcmp(dev->name, "xgbe1"))
		info = &__get_cpu_var(hv_xgbe1);
	else if (!strcmp(dev->name, "gbe0"))
		info = &__get_cpu_var(hv_gbe0);
	else if (!strcmp(dev->name, "gbe1"))
		info = &__get_cpu_var(hv_gbe1);
	else
		BUG();

	/* Initialize the egress timer. */
	init_timer(&info->egress_timer);
	info->egress_timer.data = (long)info;
	info->egress_timer.function = tile_net_handle_egress_timer;

	priv->cpu[my_cpu] = info;

	/*
	 * Register ourselves with the IPP.
	 */
	ret = hv_dev_pwrite(priv->hv_devhdl, 0,
			    (HV_VirtAddr)&config,
			    sizeof(netio_input_config_t),
			    NETIO_IPP_INPUT_REGISTER_OFF);
	PDEBUG("hv_dev_pwrite(NETIO_IPP_INPUT_REGISTER_OFF) returned %d\n",
	       ret);
	if (ret < 0) {
		printk(KERN_DEBUG "hv_dev_pwrite NETIO_IPP_INPUT_REGISTER_OFF"
		       " failure %d\n", ret);
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

	netif_napi_add(dev, &info->napi, tile_net_poll, 64);

	/* Now we are registered. */
	info->registered = true;
}


/*
 * Unregister with hypervisor on each CPU.
 */
static void tile_net_unregister(void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	int ret = 0;
	int dummy = 0;

	/* Do nothing if never registered. */
	if (info == NULL)
		return;

	/* Do nothing if already unregistered. */
	if (!info->registered)
		return;

	/*
	 * Unregister ourselves with LIPP.
	 */
	ret = hv_dev_pwrite(priv->hv_devhdl, 0, (HV_VirtAddr)&dummy,
			    sizeof(dummy), NETIO_IPP_INPUT_UNREGISTER_OFF);
	PDEBUG("hv_dev_pwrite(NETIO_IPP_INPUT_UNREGISTER_OFF) returned %d\n",
	       ret);
	if (ret < 0) {
		/* FIXME: Just panic? */
		pr_err("hv_dev_pwrite NETIO_IPP_INPUT_UNREGISTER_OFF"
		       " failure %d\n", ret);
	}

	/*
	 * Discard all packets still in our NetIO queue.  Hopefully,
	 * once the unregister call is complete, there will be no
	 * packets still in flight on the IDN.
	 */
	tile_net_discard_packets(dev);

	/* Reset state. */
	info->num_needed_small_buffers = 0;
	info->num_needed_large_buffers = 0;

	/* Cancel egress timer. */
	del_timer(&info->egress_timer);
	info->egress_timer_scheduled = false;

	netif_napi_del(&info->napi);

	/* Now we are unregistered. */
	info->registered = false;
}


/*
 * Helper function for "tile_net_stop()".
 *
 * Also used to handle registration failure in "tile_net_open_inner()",
 * when "fully_opened" is known to be false, and the various extra
 * steps in "tile_net_stop()" are not necessary.  ISSUE: It might be
 * simpler if we could just call "tile_net_stop()" anyway.
 */
static void tile_net_stop_aux(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);

	int dummy = 0;

	/* Unregister all tiles, so LIPP will stop delivering packets. */
	on_each_cpu(tile_net_unregister, (void *)dev, 1);

	/* Stop LIPP/LEPP. */
	if (hv_dev_pwrite(priv->hv_devhdl, 0, (HV_VirtAddr)&dummy,
			  sizeof(dummy), NETIO_IPP_STOP_SHIM_OFF) < 0)
		panic("Failed to stop LIPP/LEPP!\n");

	priv->partly_opened = 0;
}


/*
 * Disable ingress interrupts for the given device on the current cpu.
 */
static void tile_net_disable_intr(void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	/* Disable hypervisor interrupt. */
	disable_percpu_irq(priv->intr_id);

	/* Disable NAPI if needed. */
	if (info != NULL && info->napi_enabled) {
		napi_disable(&info->napi);
		info->napi_enabled = false;
	}
}


/*
 * Enable ingress interrupts for the given device on the current cpu.
 */
static void tile_net_enable_intr(void *dev_ptr)
{
	struct net_device *dev = (struct net_device *)dev_ptr;
	struct tile_net_priv *priv = netdev_priv(dev);
	int my_cpu = smp_processor_id();
	struct tile_net_cpu *info = priv->cpu[my_cpu];

	/* Enable hypervisor interrupt. */
	enable_percpu_irq(priv->intr_id);

	/* Enable NAPI. */
	napi_enable(&info->napi);
	info->napi_enabled = true;
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
	unsigned int irq;
	int i;

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
	 * clean up using "tile_net_stop_aux()".
	 */
	smp_call_function(tile_net_register, (void *)dev, 1);
	for_each_online_cpu(i) {
		if (!priv->cpu[i]->registered) {
			tile_net_stop_aux(dev);
			return -EAGAIN;
		}
	}

	queue = &info->queue;

	/*
	 * Set the device intr bit mask.
	 * The tile_net_register above sets per tile __intr_id.
	 */
	priv->intr_id = queue->__system_part->__intr_id;
	BUG_ON(!priv->intr_id);

	/*
	 * Register the device interrupt handler.
	 * The __ffs() function returns the index into the interrupt handler
	 * table from the interrupt bit mask which should have one bit
	 * and one bit only set.
	 */
	irq = __ffs(priv->intr_id);
	tile_irq_activate(irq, TILE_IRQ_PERCPU);
	BUG_ON(request_irq(irq, tile_net_handle_ingress_interrupt,
			   0, dev->name, (void *)dev) != 0);

	/* ISSUE: How could "priv->fully_opened" ever be "true" here? */

	if (!priv->fully_opened) {

		int dummy = 0;

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

		/* Start LIPP/LEPP and activate "ingress" at the shim. */
		if (hv_dev_pwrite(priv->hv_devhdl, 0, (HV_VirtAddr)&dummy,
				  sizeof(dummy), NETIO_IPP_INPUT_INIT_OFF) < 0)
			panic("Failed to activate the LIPP Shim!\n");

		priv->fully_opened = 1;
	}

	/* On each tile, enable the hypervisor to trigger interrupts. */
	/* ISSUE: Do this before starting LIPP/LEPP? */
	on_each_cpu(tile_net_enable_intr, (void *)dev, 1);

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
	struct delayed_work *dw =
		container_of(w, struct delayed_work, work);

	struct tile_net_priv *priv =
		container_of(dw, struct tile_net_priv, retry_work);

	/*
	 * Try to bring the NetIO interface up.  If it fails, reschedule
	 * ourselves to try again later; otherwise, tell Linux we now have
	 * a working link.  ISSUE: What if the return value is negative?
	 */
	if (tile_net_open_inner(priv->dev))
		schedule_delayed_work_on(singlethread_cpu, &priv->retry_work,
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
 * handler is registered with the OS, the watchdog timer is started,
 * and the stack is notified that the interface is ready.
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

		priv->partly_opened = 1;
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
	schedule_delayed_work_on(singlethread_cpu, &priv->retry_work,
				 TILE_NET_RETRY_INTERVAL);

	return 0;
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
 * ISSUE: Can this can be called while "tile_net_poll()" is running?
 */
static int tile_net_stop(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);

	bool pending = true;

	PDEBUG("tile_net_stop()\n");

	/* ISSUE: Only needed if not yet fully open. */
	cancel_delayed_work_sync(&priv->retry_work);

	/* Can't transmit any more. */
	netif_stop_queue(dev);

	/*
	 * Disable hypervisor interrupts on each tile.
	 */
	on_each_cpu(tile_net_disable_intr, (void *)dev, 1);

	/*
	 * Unregister the interrupt handler.
	 * The __ffs() function returns the index into the interrupt handler
	 * table from the interrupt bit mask which should have one bit
	 * and one bit only set.
	 */
	if (priv->intr_id)
		free_irq(__ffs(priv->intr_id), dev);

	/*
	 * Drain all the LIPP buffers.
	 */

	while (true) {
		int buffer;

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
	}

	/* Stop LIPP/LEPP. */
	tile_net_stop_aux(dev);


	priv->fully_opened = 0;


	/*
	 * XXX: ISSUE: It appears that, in practice anyway, by the
	 * time we get here, there are no pending completions.
	 */
	while (pending) {

		struct sk_buff *olds[32];
		unsigned int wanted = 32;
		unsigned int i, nolds = 0;

		nolds = tile_net_lepp_grab_comps(dev, olds,
						 wanted, &pending);

		/* ISSUE: We have never actually seen this debug spew. */
		if (nolds != 0)
			pr_info("During tile_net_stop(), grabbed %d comps.\n",
			       nolds);

		for (i = 0; i < nolds; i++)
			kfree_skb(olds[i]);
	}


	/* Wipe the EPP queue. */
	memset(priv->epp_queue, 0, sizeof(lepp_queue_t));

	/* Evict the EPP queue. */
	finv_buffer(priv->epp_queue, PAGE_SIZE);

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
			finv_buffer_remote(b_data, b_len);

		cpa = __pa(b_data);
		frags[n].cpa_lo = cpa;
		frags[n].cpa_hi = cpa >> 32;
		frags[n].length = b_len;
		frags[n].hash_for_home = hash_default;
		n++;
	}

	for (i = 0; i < sh->nr_frags; i++) {

		skb_frag_t *f = &sh->frags[i];
		unsigned long pfn = page_to_pfn(f->page);

		/* FIXME: Compute "hash_for_home" properly. */
		/* ISSUE: The hypervisor checks CHIP_HAS_REV1_DMA_PACKETS(). */
		int hash_for_home = hash_default;

		/* FIXME: Hmmm. */
		if (!hash_default) {
			void *va = pfn_to_kaddr(pfn) + f->page_offset;
			BUG_ON(PageHighMem(f->page));
			finv_buffer_remote(va, f->size);
		}

		cpa = ((phys_addr_t)pfn << PAGE_SHIFT) + f->page_offset;
		frags[n].cpa_lo = cpa;
		frags[n].cpa_hi = cpa >> 32;
		frags[n].length = f->size;
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
 * In theory, "sh->nr_frags" could be 3, but in practice, it seems
 * that this will never actually happen.
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

	lepp_queue_t *eq = priv->epp_queue;

	struct sk_buff *olds[4];
	unsigned int wanted = 4;
	unsigned int i, nolds = 0;

	unsigned int cmd_head, cmd_tail, cmd_next;
	unsigned int comp_tail;

	unsigned int free_slots;


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

	spin_lock_irqsave(&priv->cmd_lock, irqflags);

	/*
	 * Handle completions if needed to make room.
	 * HACK: Spin until there is sufficient room.
	 */
	free_slots = lepp_num_free_comp_slots(eq);
	if (free_slots < 1) {
spin:
		nolds += tile_net_lepp_grab_comps(dev, olds + nolds,
						  wanted - nolds, NULL);
		if (lepp_num_free_comp_slots(eq) < 1)
			goto spin;
	}

	cmd_head = eq->cmd_head;
	cmd_tail = eq->cmd_tail;

	/* NOTE: The "gotos" below are untested. */

	/* Prepare to advance, detecting full queue. */
	cmd_next = cmd_tail + cmd_size;
	if (cmd_tail < cmd_head && cmd_next >= cmd_head)
		goto spin;
	if (cmd_next > LEPP_CMD_LIMIT) {
		cmd_next = 0;
		if (cmd_next == cmd_head)
			goto spin;
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
	__insn_mf();

	eq->cmd_tail = cmd_tail;

	spin_unlock_irqrestore(&priv->cmd_lock, irqflags);

	if (nolds == 0)
		nolds = tile_net_lepp_grab_comps(dev, olds, wanted, NULL);

	/* Handle completions. */
	for (i = 0; i < nolds; i++)
		kfree_skb(olds[i]);

	/* Update stats. */
	stats->tx_packets += num_segs;
	stats->tx_bytes += (num_segs * sh_len) + d_len;

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

	unsigned int csum_start = skb->csum_start - skb_headroom(skb);

	lepp_frag_t frags[LEPP_MAX_FRAGS];

	unsigned int num_frags;

	lepp_queue_t *eq = priv->epp_queue;

	struct sk_buff *olds[4];
	unsigned int wanted = 4;
	unsigned int i, nolds = 0;

	unsigned int cmd_size = sizeof(lepp_cmd_t);

	unsigned int cmd_head, cmd_tail, cmd_next;
	unsigned int comp_tail;

	lepp_cmd_t cmds[LEPP_MAX_FRAGS];

	unsigned int free_slots;


	/*
	 * This is paranoia, since we think that if the link doesn't come
	 * up, telling Linux we have no carrier will keep it from trying
	 * to transmit.  If it does, though, we can't execute this routine,
	 * since data structures we depend on aren't set up yet.
	 */
	if (!info->registered)
		return NETDEV_TX_BUSY;


	/* Save the timestamp. */
	dev->trans_start = jiffies;


#ifdef TILE_NET_PARANOIA
#if CHIP_HAS_CBOX_HOME_MAP()
	if (hash_default) {
		HV_PTE pte = *virt_to_pte(current->mm, (unsigned long)data);
		if (hv_pte_get_mode(pte) != HV_PTE_MODE_CACHE_HASH_L3)
			panic("Non-coherent egress buffer!");
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

	spin_lock_irqsave(&priv->cmd_lock, irqflags);

	/*
	 * Handle completions if needed to make room.
	 * HACK: Spin until there is sufficient room.
	 */
	free_slots = lepp_num_free_comp_slots(eq);
	if (free_slots < 1) {
spin:
		nolds += tile_net_lepp_grab_comps(dev, olds + nolds,
						  wanted - nolds, NULL);
		if (lepp_num_free_comp_slots(eq) < 1)
			goto spin;
	}

	cmd_head = eq->cmd_head;
	cmd_tail = eq->cmd_tail;

	/* NOTE: The "gotos" below are untested. */

	/* Copy the commands, or fail. */
	for (i = 0; i < num_frags; i++) {

		/* Prepare to advance, detecting full queue. */
		cmd_next = cmd_tail + cmd_size;
		if (cmd_tail < cmd_head && cmd_next >= cmd_head)
			goto spin;
		if (cmd_next > LEPP_CMD_LIMIT) {
			cmd_next = 0;
			if (cmd_next == cmd_head)
				goto spin;
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
	__insn_mf();

	eq->cmd_tail = cmd_tail;

	spin_unlock_irqrestore(&priv->cmd_lock, irqflags);

	if (nolds == 0)
		nolds = tile_net_lepp_grab_comps(dev, olds, wanted, NULL);

	/* Handle completions. */
	for (i = 0; i < nolds; i++)
		kfree_skb(olds[i]);

	/* HACK: Track "expanded" size for short packets (e.g. 42 < 60). */
	stats->tx_packets++;
	stats->tx_bytes += ((len >= ETH_ZLEN) ? len : ETH_ZLEN);

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
	       jiffies - dev->trans_start);

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
static struct net_device_stats *tile_net_get_stats(struct net_device *dev)
{
	struct tile_net_priv *priv = netdev_priv(dev);
	u32 rx_packets = 0;
	u32 tx_packets = 0;
	u32 rx_bytes = 0;
	u32 tx_bytes = 0;
	int i;

	for_each_online_cpu(i) {
		if (priv->cpu[i]) {
			rx_packets += priv->cpu[i]->stats.rx_packets;
			rx_bytes += priv->cpu[i]->stats.rx_bytes;
			tx_packets += priv->cpu[i]->stats.tx_packets;
			tx_bytes += priv->cpu[i]->stats.tx_bytes;
		}
	}

	priv->stats.rx_packets = rx_packets;
	priv->stats.rx_bytes = rx_bytes;
	priv->stats.tx_packets = tx_packets;
	priv->stats.tx_bytes = tx_bytes;

	return &priv->stats;
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
		return -EINVAL;

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
		random_ether_addr(dev->dev_addr);
	}

	return 0;
}


static struct net_device_ops tile_net_ops = {
	.ndo_open = tile_net_open,
	.ndo_stop = tile_net_stop,
	.ndo_start_xmit = tile_net_tx,
	.ndo_do_ioctl = tile_net_ioctl,
	.ndo_get_stats = tile_net_get_stats,
	.ndo_change_mtu = tile_net_change_mtu,
	.ndo_tx_timeout = tile_net_tx_timeout,
	.ndo_set_mac_address = tile_net_set_mac_address
};


/*
 * The setup function.
 *
 * This uses ether_setup() to assign various fields in dev, including
 * setting IFF_BROADCAST and IFF_MULTICAST, then sets some extra fields.
 */
static void tile_net_setup(struct net_device *dev)
{
	PDEBUG("tile_net_setup()\n");

	ether_setup(dev);

	dev->netdev_ops = &tile_net_ops;

	dev->watchdog_timeo = TILE_NET_TIMEOUT;

	/* We want lockless xmit. */
	dev->features |= NETIF_F_LLTX;

	/* We support hardware tx checksums. */
	dev->features |= NETIF_F_HW_CSUM;

	/* We support scatter/gather. */
	dev->features |= NETIF_F_SG;

	/* We support TSO. */
	dev->features |= NETIF_F_TSO;

#ifdef TILE_NET_GSO
	/* We support GSO. */
	dev->features |= NETIF_F_GSO;
#endif

	if (hash_default)
		dev->features |= NETIF_F_HIGHDMA;

	/* ISSUE: We should support NETIF_F_UFO. */

	dev->tx_queue_len = TILE_NET_TX_QUEUE_LEN;

	dev->mtu = TILE_NET_MTU;
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
	struct page *page;

	/*
	 * Allocate the device structure.  This allocates "priv", calls
	 * tile_net_setup(), and saves "name".  Normally, "name" is a
	 * template, instantiated by register_netdev(), but not for us.
	 */
	dev = alloc_netdev(sizeof(*priv), name, tile_net_setup);
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

	spin_lock_init(&priv->cmd_lock);
	spin_lock_init(&priv->comp_lock);

	/* Allocate "epp_queue". */
	BUG_ON(get_order(sizeof(lepp_queue_t)) != 0);
	page = alloc_pages(GFP_KERNEL | __GFP_ZERO, 0);
	if (!page) {
		free_netdev(dev);
		return NULL;
	}
	priv->epp_queue = page_address(page);

	/* Register the network device. */
	ret = register_netdev(dev);
	if (ret) {
		pr_err("register_netdev %s failed %d\n", dev->name, ret);
		free_page((unsigned long)priv->epp_queue);
		free_netdev(dev);
		return NULL;
	}

	/* Get the MAC address. */
	ret = tile_net_get_mac(dev);
	if (ret < 0) {
		unregister_netdev(dev);
		free_page((unsigned long)priv->epp_queue);
		free_netdev(dev);
		return NULL;
	}

	return dev;
}


/*
 * Module cleanup.
 */
static void tile_net_cleanup(void)
{
	int i;

	for (i = 0; i < TILE_NET_DEVS; i++) {
		if (tile_net_devs[i]) {
			struct net_device *dev = tile_net_devs[i];
			struct tile_net_priv *priv = netdev_priv(dev);
			unregister_netdev(dev);
			finv_buffer(priv->epp_queue, PAGE_SIZE);
			free_page((unsigned long)priv->epp_queue);
			free_netdev(dev);
		}
	}
}


/*
 * Module initialization.
 */
static int tile_net_init_module(void)
{
	pr_info("Tilera IPP Net Driver\n");

	tile_net_devs[0] = tile_net_dev_init("xgbe0");
	tile_net_devs[1] = tile_net_dev_init("xgbe1");
	tile_net_devs[2] = tile_net_dev_init("gbe0");
	tile_net_devs[3] = tile_net_dev_init("gbe1");

	return 0;
}


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
		pr_warning("network_cpus=%s: malformed cpu list\n",
		       str);
	} else {

		/* Remove dedicated cpus. */
		cpumask_and(&network_cpus_map, &network_cpus_map,
			    cpu_possible_mask);


		if (cpumask_empty(&network_cpus_map)) {
			pr_warning("Ignoring network_cpus='%s'.\n",
			       str);
		} else {
			char buf[1024];
			cpulist_scnprintf(buf, sizeof(buf), &network_cpus_map);
			pr_info("Linux network CPUs: %s\n", buf);
			network_cpus_used = true;
		}
	}

	return 0;
}
__setup("network_cpus=", network_cpus_setup);
#endif


module_init(tile_net_init_module);
module_exit(tile_net_cleanup);
