/*
 * ntp_monitor - monitor ntpd statistics
 */
#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "ntpd.h"
#include "ntp_io.h"
#include "ntp_if.h"
#include "ntp_lists.h"
#include "ntp_stdlib.h"
#include <ntp_random.h>

#include <stdio.h>
#include <signal.h>
#ifdef HAVE_SYS_IOCTL_H
# include <sys/ioctl.h>
#endif

/*
 * Record statistics based on source address, mode and version. The
 * receive procedure calls us with the incoming rbufp before it does
 * anything else. While at it, implement rate controls for inbound
 * traffic.
 *
 * Each entry is doubly linked into two lists, a hash table and a most-
 * recently-used (MRU) list. When a packet arrives it is looked up in
 * the hash table. If found, the statistics are updated and the entry
 * relinked at the head of the MRU list. If not found, a new entry is
 * allocated, initialized and linked into both the hash table and at the
 * head of the MRU list.
 *
 * Memory is usually allocated by grabbing a big chunk of new memory and
 * cutting it up into littler pieces. The exception to this when we hit
 * the memory limit. Then we free memory by grabbing entries off the
 * tail for the MRU list, unlinking from the hash table, and
 * reinitializing.
 *
 * INC_MONLIST is the default allocation granularity in entries.
 * INIT_MONLIST is the default initial allocation in entries.
 */
#ifdef MONMEMINC		/* old name */
# define	INC_MONLIST	MONMEMINC
#elif !defined(INC_MONLIST)
# define	INC_MONLIST	(4 * 1024 / sizeof(mon_entry))
#endif
#ifndef INIT_MONLIST
# define	INIT_MONLIST	(4 * 1024 / sizeof(mon_entry))
#endif
#ifndef MRU_MAXDEPTH_DEF
# define MRU_MAXDEPTH_DEF	(1024 * 1024 / sizeof(mon_entry))
#endif

/*
 * Hashing stuff
 */
u_char	mon_hash_bits;

/*
 * Pointers to the hash table and the MRU list.  Memory for the hash
 * table is allocated only if monitoring is enabled.
 */
mon_entry **	mon_hash;	/* MRU hash table */
mon_entry	mon_mru_list;	/* mru listhead */

/*
 * List of free structures structures, and counters of in-use and total
 * structures. The free structures are linked with the hash_next field.
 */
static  mon_entry *mon_free;		/* free list or null if none */
	u_int mru_alloc;		/* mru list + free list count */
	u_int mru_entries;		/* mru list count */
	u_int mru_peakentries;		/* highest mru_entries seen */
	u_int mru_initalloc = INIT_MONLIST;/* entries to preallocate */
	u_int mru_incalloc = INC_MONLIST;/* allocation batch factor */
static	u_int mon_mem_increments;	/* times called malloc() */

/*
 * Parameters of the RES_LIMITED restriction option. We define headway
 * as the idle time between packets. A packet is discarded if the
 * headway is less than the minimum, as well as if the average headway
 * is less than eight times the increment.
 */
int	ntp_minpkt = NTP_MINPKT;	/* minimum (log 2 s) */
u_char	ntp_minpoll = NTP_MINPOLL;	/* increment (log 2 s) */

/*
 * Initialization state.  We may be monitoring, we may not.  If
 * we aren't, we may not even have allocated any memory yet.
 */
	u_int	mon_enabled;		/* enable switch */
	u_int	mru_mindepth = 600;	/* preempt above this */
	int	mru_maxage = 64;	/* for entries older than */
	u_int	mru_maxdepth = 		/* MRU count hard limit */
			MRU_MAXDEPTH_DEF;
	int	mon_age = 3000;		/* preemption limit */

static	void		mon_getmoremem(void);
static	void		remove_from_hash(mon_entry *);
static	inline void	mon_free_entry(mon_entry *);
static	inline void	mon_reclaim_entry(mon_entry *);


/*
 * init_mon - initialize monitoring global data
 */
void
init_mon(void)
{
	/*
	 * Don't do much of anything here.  We don't allocate memory
	 * until mon_start().
	 */
	mon_enabled = MON_OFF;
	INIT_DLIST(mon_mru_list, mru);
}


/*
 * remove_from_hash - removes an entry from the address hash table and
 *		      decrements mru_entries.
 */
static void
remove_from_hash(
	mon_entry *mon
	)
{
	u_int hash;
	mon_entry *punlinked;

	mru_entries--;
	hash = MON_HASH(&mon->rmtadr);
	UNLINK_SLIST(punlinked, mon_hash[hash], mon, hash_next,
		     mon_entry);
	ENSURE(punlinked == mon);
}


static inline void
mon_free_entry(
	mon_entry *m
	)
{
	ZERO(*m);
	LINK_SLIST(mon_free, m, hash_next);
}


/*
 * mon_reclaim_entry - Remove an entry from the MRU list and from the
 *		       hash array, then zero-initialize it.  Indirectly
 *		       decrements mru_entries.

 * The entry is prepared to be reused.  Before return, in
 * remove_from_hash(), mru_entries is decremented.  It is the caller's
 * responsibility to increment it again.
 */
static inline void
mon_reclaim_entry(
	mon_entry *m
	)
{
	DEBUG_INSIST(NULL != m);

	UNLINK_DLIST(m, mru);
	remove_from_hash(m);
	ZERO(*m);
}


/*
 * mon_getmoremem - get more memory and put it on the free list
 */
static void
mon_getmoremem(void)
{
	mon_entry *chunk;
	u_int entries;

	entries = (0 == mon_mem_increments)
		      ? mru_initalloc
		      : mru_incalloc;

	if (entries) {
		chunk = eallocarray(entries, sizeof(*chunk));
		mru_alloc += entries;
		for (chunk += entries; entries; entries--)
			mon_free_entry(--chunk);

		mon_mem_increments++;
	}
}


/*
 * mon_start - start up the monitoring software
 */
void
mon_start(
	int mode
	)
{
	size_t octets;
	u_int min_hash_slots;

	if (MON_OFF == mode)		/* MON_OFF is 0 */
		return;
	if (mon_enabled) {
		mon_enabled |= mode;
		return;
	}
	if (0 == mon_mem_increments)
		mon_getmoremem();
	/*
	 * Select the MRU hash table size to limit the average count
	 * per bucket at capacity (mru_maxdepth) to 8, if possible
	 * given our hash is limited to 16 bits.
	 */
	min_hash_slots = (mru_maxdepth / 8) + 1;
	mon_hash_bits = 0;
	while (min_hash_slots >>= 1)
		mon_hash_bits++;
	mon_hash_bits = max(4, mon_hash_bits);
	mon_hash_bits = min(16, mon_hash_bits);
	octets = sizeof(*mon_hash) * MON_HASH_SIZE;
	mon_hash = erealloc_zero(mon_hash, octets, 0);

	mon_enabled = mode;
}


/*
 * mon_stop - stop the monitoring software
 */
void
mon_stop(
	int mode
	)
{
	mon_entry *mon;

	if (MON_OFF == mon_enabled)
		return;
	if ((mon_enabled & mode) == 0 || mode == MON_OFF)
		return;

	mon_enabled &= ~mode;
	if (mon_enabled != MON_OFF)
		return;
	
	/*
	 * Move everything on the MRU list to the free list quickly,
	 * without bothering to remove each from either the MRU list or
	 * the hash table.
	 */
	ITER_DLIST_BEGIN(mon_mru_list, mon, mru, mon_entry)
		mon_free_entry(mon);
	ITER_DLIST_END()

	/* empty the MRU list and hash table. */
	mru_entries = 0;
	INIT_DLIST(mon_mru_list, mru);
	zero_mem(mon_hash, sizeof(*mon_hash) * MON_HASH_SIZE);
}


/*
 * mon_clearinterface -- remove mru entries referring to a local address
 *			 which is going away.
 */
void
mon_clearinterface(
	endpt *lcladr
	)
{
	mon_entry *mon;

	/* iterate mon over mon_mru_list */
	ITER_DLIST_BEGIN(mon_mru_list, mon, mru, mon_entry)
		if (mon->lcladr == lcladr) {
			/* remove from mru list */
			UNLINK_DLIST(mon, mru);
			/* remove from hash list, adjust mru_entries */
			remove_from_hash(mon);
			/* put on free list */
			mon_free_entry(mon);
		}
	ITER_DLIST_END()
}


/*
 * ntp_monitor - record stats about this packet
 *
 * Returns supplied restriction flags, with RES_LIMITED and RES_KOD
 * cleared unless the packet should not be responded to normally
 * (RES_LIMITED) and possibly should trigger a KoD response (RES_KOD).
 * The returned flags are saved in the MRU entry, so that it reflects
 * whether the last packet from that source triggered rate limiting,
 * and if so, possible KoD response.  This implies you can not tell
 * whether a given address is eligible for rate limiting/KoD from the
 * monlist restrict bits, only whether or not the last packet triggered
 * such responses.  ntpdc -c reslist lets you see whether RES_LIMITED
 * or RES_KOD is lit for a particular address before ntp_monitor()'s
 * typical dousing.
 */
u_short
ntp_monitor(
	struct recvbuf *rbufp,
	u_short	flags
	)
{
	l_fp		interval_fp;
	struct pkt *	pkt;
	mon_entry *	mon;
	mon_entry *	oldest;
	int		oldest_age;
	u_int		hash;
	u_short		restrict_mask;
	u_char		mode;
	u_char		version;
	int		interval;
	int		head;		/* headway increment */
	int		leak;		/* new headway */
	int		limit;		/* average threshold */

	REQUIRE(rbufp != NULL);

	if (mon_enabled == MON_OFF)
		return ~(RES_LIMITED | RES_KOD) & flags;

	pkt = &rbufp->recv_pkt;
	hash = MON_HASH(&rbufp->recv_srcadr);
	mode = PKT_MODE(pkt->li_vn_mode);
	version = PKT_VERSION(pkt->li_vn_mode);
	mon = mon_hash[hash];

	/*
	 * We keep track of all traffic for a given IP in one entry,
	 * otherwise cron'ed ntpdate or similar evades RES_LIMITED.
	 */

	for (; mon != NULL; mon = mon->hash_next)
		if (SOCK_EQ(&mon->rmtadr, &rbufp->recv_srcadr))
			break;

	if (mon != NULL) {
		interval_fp = rbufp->recv_time;
		L_SUB(&interval_fp, &mon->last);
		/* add one-half second to round up */
		L_ADDUF(&interval_fp, 0x80000000);
		interval = interval_fp.l_i;
		mon->last = rbufp->recv_time;
		NSRCPORT(&mon->rmtadr) = NSRCPORT(&rbufp->recv_srcadr);
		mon->count++;
		restrict_mask = flags;
		mon->vn_mode = VN_MODE(version, mode);

		/* Shuffle to the head of the MRU list. */
		UNLINK_DLIST(mon, mru);
		LINK_DLIST(mon_mru_list, mon, mru);

		/*
		 * At this point the most recent arrival is first in the
		 * MRU list.  Decrease the counter by the headway, but
		 * not less than zero.
		 */
		mon->leak -= interval;
		mon->leak = max(0, mon->leak);
		head = 1 << ntp_minpoll;
		leak = mon->leak + head;
		limit = NTP_SHIFT * head;

		DPRINTF(2, ("MRU: interval %d headway %d limit %d\n",
			    interval, leak, limit));

		/*
		 * If the minimum and average thresholds are not
		 * exceeded, douse the RES_LIMITED and RES_KOD bits and
		 * increase the counter by the headway increment.  Note
		 * that we give a 1-s grace for the minimum threshold
		 * and a 2-s grace for the headway increment.  If one or
		 * both thresholds are exceeded and the old counter is
		 * less than the average threshold, set the counter to
		 * the average threshold plus the increment and leave
		 * the RES_LIMITED and RES_KOD bits lit. Otherwise,
		 * leave the counter alone and douse the RES_KOD bit.
		 * This rate-limits the KoDs to no less than the average
		 * headway.
		 */
		if (interval + 1 >= ntp_minpkt && leak < limit) {
			mon->leak = leak - 2;
			restrict_mask &= ~(RES_LIMITED | RES_KOD);
		} else if (mon->leak < limit)
			mon->leak = limit + head;
		else
			restrict_mask &= ~RES_KOD;

		mon->flags = restrict_mask;

		return mon->flags;
	}

	/*
	 * If we got here, this is the first we've heard of this
	 * guy.  Get him some memory, either from the free list
	 * or from the tail of the MRU list.
	 *
	 * The following ntp.conf "mru" knobs come into play determining
	 * the depth (or count) of the MRU list:
	 * - mru_mindepth ("mru mindepth") is a floor beneath which
	 *   entries are kept without regard to their age.  The
	 *   default is 600 which matches the longtime implementation
	 *   limit on the total number of entries.
	 * - mru_maxage ("mru maxage") is a ceiling on the age in
	 *   seconds of entries.  Entries older than this are
	 *   reclaimed once mon_mindepth is exceeded.  64s default.
	 *   Note that entries older than this can easily survive
	 *   as they are reclaimed only as needed.
	 * - mru_maxdepth ("mru maxdepth") is a hard limit on the
	 *   number of entries.
	 * - "mru maxmem" sets mru_maxdepth to the number of entries
	 *   which fit in the given number of kilobytes.  The default is
	 *   1024, or 1 megabyte.
	 * - mru_initalloc ("mru initalloc" sets the count of the
	 *   initial allocation of MRU entries.
	 * - "mru initmem" sets mru_initalloc in units of kilobytes.
	 *   The default is 4.
	 * - mru_incalloc ("mru incalloc" sets the number of entries to
	 *   allocate on-demand each time the free list is empty.
	 * - "mru incmem" sets mru_incalloc in units of kilobytes.
	 *   The default is 4.
	 * Whichever of "mru maxmem" or "mru maxdepth" occurs last in
	 * ntp.conf controls.  Similarly for "mru initalloc" and "mru
	 * initmem", and for "mru incalloc" and "mru incmem".
	 */
	if (mru_entries < mru_mindepth) {
		if (NULL == mon_free)
			mon_getmoremem();
		UNLINK_HEAD_SLIST(mon, mon_free, hash_next);
	} else {
		oldest = TAIL_DLIST(mon_mru_list, mru);
		oldest_age = 0;		/* silence uninit warning */
		if (oldest != NULL) {
			interval_fp = rbufp->recv_time;
			L_SUB(&interval_fp, &oldest->last);
			/* add one-half second to round up */
			L_ADDUF(&interval_fp, 0x80000000);
			oldest_age = interval_fp.l_i;
		}
		/* note -1 is legal for mru_maxage (disables) */
		if (oldest != NULL && mru_maxage < oldest_age) {
			mon_reclaim_entry(oldest);
			mon = oldest;
		} else if (mon_free != NULL || mru_alloc <
			   mru_maxdepth) {
			if (NULL == mon_free)
				mon_getmoremem();
			UNLINK_HEAD_SLIST(mon, mon_free, hash_next);
		/* Preempt from the MRU list if old enough. */
		} else if (ntp_random() / (2. * FRAC) >
			   (double)oldest_age / mon_age) {
			return ~(RES_LIMITED | RES_KOD) & flags;
		} else {
			mon_reclaim_entry(oldest);
			mon = oldest;
		}
	}

	INSIST(mon != NULL);

	/*
	 * Got one, initialize it
	 */
	mru_entries++;
	mru_peakentries = max(mru_peakentries, mru_entries);
	mon->last = rbufp->recv_time;
	mon->first = mon->last;
	mon->count = 1;
	mon->flags = ~(RES_LIMITED | RES_KOD) & flags;
	mon->leak = 0;
	memcpy(&mon->rmtadr, &rbufp->recv_srcadr, sizeof(mon->rmtadr));
	mon->vn_mode = VN_MODE(version, mode);
	mon->lcladr = rbufp->dstadr;
	mon->cast_flags = (u_char)(((rbufp->dstadr->flags &
	    INT_MCASTOPEN) && rbufp->fd == mon->lcladr->fd) ? MDF_MCAST
	    : rbufp->fd == mon->lcladr->bfd ? MDF_BCAST : MDF_UCAST);

	/*
	 * Drop him into front of the hash table. Also put him on top of
	 * the MRU list.
	 */
	LINK_SLIST(mon_hash[hash], mon, hash_next);
	LINK_DLIST(mon_mru_list, mon, mru);

	return mon->flags;
}


