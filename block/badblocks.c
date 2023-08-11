// SPDX-License-Identifier: GPL-2.0
/*
 * Bad block management
 *
 * - Heavily based on MD badblocks code from Neil Brown
 *
 * Copyright (c) 2015, Intel Corporation.
 */

#include <linux/badblocks.h>
#include <linux/seqlock.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/slab.h>

/*
 * The purpose of badblocks set/clear is to manage bad blocks ranges which are
 * identified by LBA addresses.
 *
 * When the caller of badblocks_set() wants to set a range of bad blocks, the
 * setting range can be acked or unacked. And the setting range may merge,
 * overwrite, skip the overlapped already set range, depends on who they are
 * overlapped or adjacent, and the acknowledgment type of the ranges. It can be
 * more complicated when the setting range covers multiple already set bad block
 * ranges, with restrictions of maximum length of each bad range and the bad
 * table space limitation.
 *
 * It is difficult and unnecessary to take care of all the possible situations,
 * for setting a large range of bad blocks, we can handle it by dividing the
 * large range into smaller ones when encounter overlap, max range length or
 * bad table full conditions. Every time only a smaller piece of the bad range
 * is handled with a limited number of conditions how it is interacted with
 * possible overlapped or adjacent already set bad block ranges. Then the hard
 * complicated problem can be much simpler to handle in proper way.
 *
 * When setting a range of bad blocks to the bad table, the simplified situations
 * to be considered are, (The already set bad blocks ranges are naming with
 *  prefix E, and the setting bad blocks range is naming with prefix S)
 *
 * 1) A setting range is not overlapped or adjacent to any other already set bad
 *    block range.
 *                         +--------+
 *                         |    S   |
 *                         +--------+
 *        +-------------+               +-------------+
 *        |      E1     |               |      E2     |
 *        +-------------+               +-------------+
 *    For this situation if the bad blocks table is not full, just allocate a
 *    free slot from the bad blocks table to mark the setting range S. The
 *    result is,
 *        +-------------+  +--------+   +-------------+
 *        |      E1     |  |    S   |   |      E2     |
 *        +-------------+  +--------+   +-------------+
 * 2) A setting range starts exactly at a start LBA of an already set bad blocks
 *    range.
 * 2.1) The setting range size < already set range size
 *        +--------+
 *        |    S   |
 *        +--------+
 *        +-------------+
 *        |      E      |
 *        +-------------+
 * 2.1.1) If S and E are both acked or unacked range, the setting range S can
 *    be merged into existing bad range E. The result is,
 *        +-------------+
 *        |      S      |
 *        +-------------+
 * 2.1.2) If S is unacked setting and E is acked, the setting will be denied, and
 *    the result is,
 *        +-------------+
 *        |      E      |
 *        +-------------+
 * 2.1.3) If S is acked setting and E is unacked, range S can overwrite on E.
 *    An extra slot from the bad blocks table will be allocated for S, and head
 *    of E will move to end of the inserted range S. The result is,
 *        +--------+----+
 *        |    S   | E  |
 *        +--------+----+
 * 2.2) The setting range size == already set range size
 * 2.2.1) If S and E are both acked or unacked range, the setting range S can
 *    be merged into existing bad range E. The result is,
 *        +-------------+
 *        |      S      |
 *        +-------------+
 * 2.2.2) If S is unacked setting and E is acked, the setting will be denied, and
 *    the result is,
 *        +-------------+
 *        |      E      |
 *        +-------------+
 * 2.2.3) If S is acked setting and E is unacked, range S can overwrite all of
      bad blocks range E. The result is,
 *        +-------------+
 *        |      S      |
 *        +-------------+
 * 2.3) The setting range size > already set range size
 *        +-------------------+
 *        |          S        |
 *        +-------------------+
 *        +-------------+
 *        |      E      |
 *        +-------------+
 *    For such situation, the setting range S can be treated as two parts, the
 *    first part (S1) is as same size as the already set range E, the second
 *    part (S2) is the rest of setting range.
 *        +-------------+-----+        +-------------+       +-----+
 *        |    S1       | S2  |        |     S1      |       | S2  |
 *        +-------------+-----+  ===>  +-------------+       +-----+
 *        +-------------+              +-------------+
 *        |      E      |              |      E      |
 *        +-------------+              +-------------+
 *    Now we only focus on how to handle the setting range S1 and already set
 *    range E, which are already explained in 2.2), for the rest S2 it will be
 *    handled later in next loop.
 * 3) A setting range starts before the start LBA of an already set bad blocks
 *    range.
 *        +-------------+
 *        |      S      |
 *        +-------------+
 *             +-------------+
 *             |      E      |
 *             +-------------+
 *    For this situation, the setting range S can be divided into two parts, the
 *    first (S1) ends at the start LBA of already set range E, the second part
 *    (S2) starts exactly at a start LBA of the already set range E.
 *        +----+---------+             +----+      +---------+
 *        | S1 |    S2   |             | S1 |      |    S2   |
 *        +----+---------+      ===>   +----+      +---------+
 *             +-------------+                     +-------------+
 *             |      E      |                     |      E      |
 *             +-------------+                     +-------------+
 *    Now only the first part S1 should be handled in this loop, which is in
 *    similar condition as 1). The rest part S2 has exact same start LBA address
 *    of the already set range E, they will be handled in next loop in one of
 *    situations in 2).
 * 4) A setting range starts after the start LBA of an already set bad blocks
 *    range.
 * 4.1) If the setting range S exactly matches the tail part of already set bad
 *    blocks range E, like the following chart shows,
 *            +---------+
 *            |   S     |
 *            +---------+
 *        +-------------+
 *        |      E      |
 *        +-------------+
 * 4.1.1) If range S and E have same acknowledge value (both acked or unacked),
 *    they will be merged into one, the result is,
 *        +-------------+
 *        |      S      |
 *        +-------------+
 * 4.1.2) If range E is acked and the setting range S is unacked, the setting
 *    request of S will be rejected, the result is,
 *        +-------------+
 *        |      E      |
 *        +-------------+
 * 4.1.3) If range E is unacked, and the setting range S is acked, then S may
 *    overwrite the overlapped range of E, the result is,
 *        +---+---------+
 *        | E |    S    |
 *        +---+---------+
 * 4.2) If the setting range S stays in middle of an already set range E, like
 *    the following chart shows,
 *             +----+
 *             | S  |
 *             +----+
 *        +--------------+
 *        |       E      |
 *        +--------------+
 * 4.2.1) If range S and E have same acknowledge value (both acked or unacked),
 *    they will be merged into one, the result is,
 *        +--------------+
 *        |       S      |
 *        +--------------+
 * 4.2.2) If range E is acked and the setting range S is unacked, the setting
 *    request of S will be rejected, the result is also,
 *        +--------------+
 *        |       E      |
 *        +--------------+
 * 4.2.3) If range E is unacked, and the setting range S is acked, then S will
 *    inserted into middle of E and split previous range E into two parts (E1
 *    and E2), the result is,
 *        +----+----+----+
 *        | E1 |  S | E2 |
 *        +----+----+----+
 * 4.3) If the setting bad blocks range S is overlapped with an already set bad
 *    blocks range E. The range S starts after the start LBA of range E, and
 *    ends after the end LBA of range E, as the following chart shows,
 *            +-------------------+
 *            |          S        |
 *            +-------------------+
 *        +-------------+
 *        |      E      |
 *        +-------------+
 *    For this situation the range S can be divided into two parts, the first
 *    part (S1) ends at end range E, and the second part (S2) has rest range of
 *    origin S.
 *            +---------+---------+            +---------+      +---------+
 *            |    S1   |    S2   |            |    S1   |      |    S2   |
 *            +---------+---------+  ===>      +---------+      +---------+
 *        +-------------+                  +-------------+
 *        |      E      |                  |      E      |
 *        +-------------+                  +-------------+
 *     Now in this loop the setting range S1 and already set range E can be
 *     handled as the situations 4.1), the rest range S2 will be handled in next
 *     loop and ignored in this loop.
 * 5) A setting bad blocks range S is adjacent to one or more already set bad
 *    blocks range(s), and they are all acked or unacked range.
 * 5.1) Front merge: If the already set bad blocks range E is before setting
 *    range S and they are adjacent,
 *                +------+
 *                |  S   |
 *                +------+
 *        +-------+
 *        |   E   |
 *        +-------+
 * 5.1.1) When total size of range S and E <= BB_MAX_LEN, and their acknowledge
 *    values are same, the setting range S can front merges into range E. The
 *    result is,
 *        +--------------+
 *        |       S      |
 *        +--------------+
 * 5.1.2) Otherwise these two ranges cannot merge, just insert the setting
 *    range S right after already set range E into the bad blocks table. The
 *    result is,
 *        +--------+------+
 *        |   E    |   S  |
 *        +--------+------+
 * 6) Special cases which above conditions cannot handle
 * 6.1) Multiple already set ranges may merge into less ones in a full bad table
 *        +-------------------------------------------------------+
 *        |                           S                           |
 *        +-------------------------------------------------------+
 *        |<----- BB_MAX_LEN ----->|
 *                                 +-----+     +-----+   +-----+
 *                                 | E1  |     | E2  |   | E3  |
 *                                 +-----+     +-----+   +-----+
 *     In the above example, when the bad blocks table is full, inserting the
 *     first part of setting range S will fail because no more available slot
 *     can be allocated from bad blocks table. In this situation a proper
 *     setting method should be go though all the setting bad blocks range and
 *     look for chance to merge already set ranges into less ones. When there
 *     is available slot from bad blocks table, re-try again to handle more
 *     setting bad blocks ranges as many as possible.
 *        +------------------------+
 *        |          S3            |
 *        +------------------------+
 *        |<----- BB_MAX_LEN ----->|
 *                                 +-----+-----+-----+---+-----+--+
 *                                 |       S1        |     S2     |
 *                                 +-----+-----+-----+---+-----+--+
 *     The above chart shows although the first part (S3) cannot be inserted due
 *     to no-space in bad blocks table, but the following E1, E2 and E3 ranges
 *     can be merged with rest part of S into less range S1 and S2. Now there is
 *     1 free slot in bad blocks table.
 *        +------------------------+-----+-----+-----+---+-----+--+
 *        |           S3           |       S1        |     S2     |
 *        +------------------------+-----+-----+-----+---+-----+--+
 *     Since the bad blocks table is not full anymore, re-try again for the
 *     origin setting range S. Now the setting range S3 can be inserted into the
 *     bad blocks table with previous freed slot from multiple ranges merge.
 * 6.2) Front merge after overwrite
 *    In the following example, in bad blocks table, E1 is an acked bad blocks
 *    range and E2 is an unacked bad blocks range, therefore they are not able
 *    to merge into a larger range. The setting bad blocks range S is acked,
 *    therefore part of E2 can be overwritten by S.
 *                      +--------+
 *                      |    S   |                             acknowledged
 *                      +--------+                         S:       1
 *              +-------+-------------+                   E1:       1
 *              |   E1  |    E2       |                   E2:       0
 *              +-------+-------------+
 *     With previous simplified routines, after overwriting part of E2 with S,
 *     the bad blocks table should be (E3 is remaining part of E2 which is not
 *     overwritten by S),
 *                                                             acknowledged
 *              +-------+--------+----+                    S:       1
 *              |   E1  |    S   | E3 |                   E1:       1
 *              +-------+--------+----+                   E3:       0
 *     The above result is correct but not perfect. Range E1 and S in the bad
 *     blocks table are all acked, merging them into a larger one range may
 *     occupy less bad blocks table space and make badblocks_check() faster.
 *     Therefore in such situation, after overwriting range S, the previous range
 *     E1 should be checked for possible front combination. Then the ideal
 *     result can be,
 *              +----------------+----+                        acknowledged
 *              |       E1       | E3 |                   E1:       1
 *              +----------------+----+                   E3:       0
 * 6.3) Behind merge: If the already set bad blocks range E is behind the setting
 *    range S and they are adjacent. Normally we don't need to care about this
 *    because front merge handles this while going though range S from head to
 *    tail, except for the tail part of range S. When the setting range S are
 *    fully handled, all the above simplified routine doesn't check whether the
 *    tail LBA of range S is adjacent to the next already set range and not
 *    merge them even it is possible.
 *        +------+
 *        |  S   |
 *        +------+
 *               +-------+
 *               |   E   |
 *               +-------+
 *    For the above special situation, when the setting range S are all handled
 *    and the loop ends, an extra check is necessary for whether next already
 *    set range E is right after S and mergeable.
 * 6.3.1) When total size of range E and S <= BB_MAX_LEN, and their acknowledge
 *    values are same, the setting range S can behind merges into range E. The
 *    result is,
 *        +--------------+
 *        |       S      |
 *        +--------------+
 * 6.3.2) Otherwise these two ranges cannot merge, just insert the setting range
 *     S in front of the already set range E in the bad blocks table. The result
 *     is,
 *        +------+-------+
 *        |  S   |   E   |
 *        +------+-------+
 *
 * All the above 5 simplified situations and 3 special cases may cover 99%+ of
 * the bad block range setting conditions. Maybe there is some rare corner case
 * is not considered and optimized, it won't hurt if badblocks_set() fails due
 * to no space, or some ranges are not merged to save bad blocks table space.
 *
 * Inside badblocks_set() each loop starts by jumping to re_insert label, every
 * time for the new loop prev_badblocks() is called to find an already set range
 * which starts before or at current setting range. Since the setting bad blocks
 * range is handled from head to tail, most of the cases it is unnecessary to do
 * the binary search inside prev_badblocks(), it is possible to provide a hint
 * to prev_badblocks() for a fast path, then the expensive binary search can be
 * avoided. In my test with the hint to prev_badblocks(), except for the first
 * loop, all rested calls to prev_badblocks() can go into the fast path and
 * return correct bad blocks table index immediately.
 */

/*
 * Find the range starts at-or-before 's' from bad table. The search
 * starts from index 'hint' and stops at index 'hint_end' from the bad
 * table.
 */
static int prev_by_hint(struct badblocks *bb, sector_t s, int hint)
{
	int hint_end = hint + 2;
	u64 *p = bb->page;
	int ret = -1;

	while ((hint < hint_end) && ((hint + 1) <= bb->count) &&
	       (BB_OFFSET(p[hint]) <= s)) {
		if ((hint + 1) == bb->count || BB_OFFSET(p[hint + 1]) > s) {
			ret = hint;
			break;
		}
		hint++;
	}

	return ret;
}

/*
 * Find the range starts at-or-before bad->start. If 'hint' is provided
 * (hint >= 0) then search in the bad table from hint firstly. It is
 * very probably the wanted bad range can be found from the hint index,
 * then the unnecessary while-loop iteration can be avoided.
 */
static int prev_badblocks(struct badblocks *bb, struct badblocks_context *bad,
			  int hint)
{
	sector_t s = bad->start;
	int ret = -1;
	int lo, hi;
	u64 *p;

	if (!bb->count)
		goto out;

	if (hint >= 0) {
		ret = prev_by_hint(bb, s, hint);
		if (ret >= 0)
			goto out;
	}

	lo = 0;
	hi = bb->count;
	p = bb->page;

	/* The following bisect search might be unnecessary */
	if (BB_OFFSET(p[lo]) > s)
		return -1;
	if (BB_OFFSET(p[hi - 1]) <= s)
		return hi - 1;

	/* Do bisect search in bad table */
	while (hi - lo > 1) {
		int mid = (lo + hi)/2;
		sector_t a = BB_OFFSET(p[mid]);

		if (a == s) {
			ret = mid;
			goto out;
		}

		if (a < s)
			lo = mid;
		else
			hi = mid;
	}

	if (BB_OFFSET(p[lo]) <= s)
		ret = lo;
out:
	return ret;
}

/*
 * Return 'true' if the range indicated by 'bad' can be backward merged
 * with the bad range (from the bad table) index by 'behind'.
 */
static bool can_merge_behind(struct badblocks *bb,
			     struct badblocks_context *bad, int behind)
{
	sector_t sectors = bad->len;
	sector_t s = bad->start;
	u64 *p = bb->page;

	if ((s < BB_OFFSET(p[behind])) &&
	    ((s + sectors) >= BB_OFFSET(p[behind])) &&
	    ((BB_END(p[behind]) - s) <= BB_MAX_LEN) &&
	    BB_ACK(p[behind]) == bad->ack)
		return true;
	return false;
}

/*
 * Do backward merge for range indicated by 'bad' and the bad range
 * (from the bad table) indexed by 'behind'. The return value is merged
 * sectors from bad->len.
 */
static int behind_merge(struct badblocks *bb, struct badblocks_context *bad,
			int behind)
{
	sector_t sectors = bad->len;
	sector_t s = bad->start;
	u64 *p = bb->page;
	int merged = 0;

	WARN_ON(s >= BB_OFFSET(p[behind]));
	WARN_ON((s + sectors) < BB_OFFSET(p[behind]));

	if (s < BB_OFFSET(p[behind])) {
		merged = BB_OFFSET(p[behind]) - s;
		p[behind] =  BB_MAKE(s, BB_LEN(p[behind]) + merged, bad->ack);

		WARN_ON((BB_LEN(p[behind]) + merged) >= BB_MAX_LEN);
	}

	return merged;
}

/*
 * Return 'true' if the range indicated by 'bad' can be forward
 * merged with the bad range (from the bad table) indexed by 'prev'.
 */
static bool can_merge_front(struct badblocks *bb, int prev,
			    struct badblocks_context *bad)
{
	sector_t s = bad->start;
	u64 *p = bb->page;

	if (BB_ACK(p[prev]) == bad->ack &&
	    (s < BB_END(p[prev]) ||
	     (s == BB_END(p[prev]) && (BB_LEN(p[prev]) < BB_MAX_LEN))))
		return true;
	return false;
}

/*
 * Do forward merge for range indicated by 'bad' and the bad range
 * (from bad table) indexed by 'prev'. The return value is sectors
 * merged from bad->len.
 */
static int front_merge(struct badblocks *bb, int prev, struct badblocks_context *bad)
{
	sector_t sectors = bad->len;
	sector_t s = bad->start;
	u64 *p = bb->page;
	int merged = 0;

	WARN_ON(s > BB_END(p[prev]));

	if (s < BB_END(p[prev])) {
		merged = min_t(sector_t, sectors, BB_END(p[prev]) - s);
	} else {
		merged = min_t(sector_t, sectors, BB_MAX_LEN - BB_LEN(p[prev]));
		if ((prev + 1) < bb->count &&
		    merged > (BB_OFFSET(p[prev + 1]) - BB_END(p[prev]))) {
			merged = BB_OFFSET(p[prev + 1]) - BB_END(p[prev]);
		}

		p[prev] = BB_MAKE(BB_OFFSET(p[prev]),
				  BB_LEN(p[prev]) + merged, bad->ack);
	}

	return merged;
}

/*
 * 'Combine' is a special case which can_merge_front() is not able to
 * handle: If a bad range (indexed by 'prev' from bad table) exactly
 * starts as bad->start, and the bad range ahead of 'prev' (indexed by
 * 'prev - 1' from bad table) exactly ends at where 'prev' starts, and
 * the sum of their lengths does not exceed BB_MAX_LEN limitation, then
 * these two bad range (from bad table) can be combined.
 *
 * Return 'true' if bad ranges indexed by 'prev' and 'prev - 1' from bad
 * table can be combined.
 */
static bool can_combine_front(struct badblocks *bb, int prev,
			      struct badblocks_context *bad)
{
	u64 *p = bb->page;

	if ((prev > 0) &&
	    (BB_OFFSET(p[prev]) == bad->start) &&
	    (BB_END(p[prev - 1]) == BB_OFFSET(p[prev])) &&
	    (BB_LEN(p[prev - 1]) + BB_LEN(p[prev]) <= BB_MAX_LEN) &&
	    (BB_ACK(p[prev - 1]) == BB_ACK(p[prev])))
		return true;
	return false;
}

/*
 * Combine the bad ranges indexed by 'prev' and 'prev - 1' (from bad
 * table) into one larger bad range, and the new range is indexed by
 * 'prev - 1'.
 * The caller of front_combine() will decrease bb->count, therefore
 * it is unnecessary to clear p[perv] after front merge.
 */
static void front_combine(struct badblocks *bb, int prev)
{
	u64 *p = bb->page;

	p[prev - 1] = BB_MAKE(BB_OFFSET(p[prev - 1]),
			      BB_LEN(p[prev - 1]) + BB_LEN(p[prev]),
			      BB_ACK(p[prev]));
	if ((prev + 1) < bb->count)
		memmove(p + prev, p + prev + 1, (bb->count - prev - 1) * 8);
}

/*
 * Return 'true' if the range indicated by 'bad' is exactly forward
 * overlapped with the bad range (from bad table) indexed by 'front'.
 * Exactly forward overlap means the bad range (from bad table) indexed
 * by 'prev' does not cover the whole range indicated by 'bad'.
 */
static bool overlap_front(struct badblocks *bb, int front,
			  struct badblocks_context *bad)
{
	u64 *p = bb->page;

	if (bad->start >= BB_OFFSET(p[front]) &&
	    bad->start < BB_END(p[front]))
		return true;
	return false;
}

/*
 * Return 'true' if the range indicated by 'bad' is exactly backward
 * overlapped with the bad range (from bad table) indexed by 'behind'.
 */
static bool overlap_behind(struct badblocks *bb, struct badblocks_context *bad,
			   int behind)
{
	u64 *p = bb->page;

	if (bad->start < BB_OFFSET(p[behind]) &&
	    (bad->start + bad->len) > BB_OFFSET(p[behind]))
		return true;
	return false;
}

/*
 * Return 'true' if the range indicated by 'bad' can overwrite the bad
 * range (from bad table) indexed by 'prev'.
 *
 * The range indicated by 'bad' can overwrite the bad range indexed by
 * 'prev' when,
 * 1) The whole range indicated by 'bad' can cover partial or whole bad
 *    range (from bad table) indexed by 'prev'.
 * 2) The ack value of 'bad' is larger or equal to the ack value of bad
 *    range 'prev'.
 *
 * If the overwriting doesn't cover the whole bad range (from bad table)
 * indexed by 'prev', new range might be split from existing bad range,
 * 1) The overwrite covers head or tail part of existing bad range, 1
 *    extra bad range will be split and added into the bad table.
 * 2) The overwrite covers middle of existing bad range, 2 extra bad
 *    ranges will be split (ahead and after the overwritten range) and
 *    added into the bad table.
 * The number of extra split ranges of the overwriting is stored in
 * 'extra' and returned for the caller.
 */
static bool can_front_overwrite(struct badblocks *bb, int prev,
				struct badblocks_context *bad, int *extra)
{
	u64 *p = bb->page;
	int len;

	WARN_ON(!overlap_front(bb, prev, bad));

	if (BB_ACK(p[prev]) >= bad->ack)
		return false;

	if (BB_END(p[prev]) <= (bad->start + bad->len)) {
		len = BB_END(p[prev]) - bad->start;
		if (BB_OFFSET(p[prev]) == bad->start)
			*extra = 0;
		else
			*extra = 1;

		bad->len = len;
	} else {
		if (BB_OFFSET(p[prev]) == bad->start)
			*extra = 1;
		else
		/*
		 * prev range will be split into two, beside the overwritten
		 * one, an extra slot needed from bad table.
		 */
			*extra = 2;
	}

	if ((bb->count + (*extra)) >= MAX_BADBLOCKS)
		return false;

	return true;
}

/*
 * Do the overwrite from the range indicated by 'bad' to the bad range
 * (from bad table) indexed by 'prev'.
 * The previously called can_front_overwrite() will provide how many
 * extra bad range(s) might be split and added into the bad table. All
 * the splitting cases in the bad table will be handled here.
 */
static int front_overwrite(struct badblocks *bb, int prev,
			   struct badblocks_context *bad, int extra)
{
	u64 *p = bb->page;
	sector_t orig_end = BB_END(p[prev]);
	int orig_ack = BB_ACK(p[prev]);

	switch (extra) {
	case 0:
		p[prev] = BB_MAKE(BB_OFFSET(p[prev]), BB_LEN(p[prev]),
				  bad->ack);
		break;
	case 1:
		if (BB_OFFSET(p[prev]) == bad->start) {
			p[prev] = BB_MAKE(BB_OFFSET(p[prev]),
					  bad->len, bad->ack);
			memmove(p + prev + 2, p + prev + 1,
				(bb->count - prev - 1) * 8);
			p[prev + 1] = BB_MAKE(bad->start + bad->len,
					      orig_end - BB_END(p[prev]),
					      orig_ack);
		} else {
			p[prev] = BB_MAKE(BB_OFFSET(p[prev]),
					  bad->start - BB_OFFSET(p[prev]),
					  orig_ack);
			/*
			 * prev +2 -> prev + 1 + 1, which is for,
			 * 1) prev + 1: the slot index of the previous one
			 * 2) + 1: one more slot for extra being 1.
			 */
			memmove(p + prev + 2, p + prev + 1,
				(bb->count - prev - 1) * 8);
			p[prev + 1] = BB_MAKE(bad->start, bad->len, bad->ack);
		}
		break;
	case 2:
		p[prev] = BB_MAKE(BB_OFFSET(p[prev]),
				  bad->start - BB_OFFSET(p[prev]),
				  orig_ack);
		/*
		 * prev + 3 -> prev + 1 + 2, which is for,
		 * 1) prev + 1: the slot index of the previous one
		 * 2) + 2: two more slots for extra being 2.
		 */
		memmove(p + prev + 3, p + prev + 1,
			(bb->count - prev - 1) * 8);
		p[prev + 1] = BB_MAKE(bad->start, bad->len, bad->ack);
		p[prev + 2] = BB_MAKE(BB_END(p[prev + 1]),
				      orig_end - BB_END(p[prev + 1]),
				      orig_ack);
		break;
	default:
		break;
	}

	return bad->len;
}

/*
 * Explicitly insert a range indicated by 'bad' to the bad table, where
 * the location is indexed by 'at'.
 */
static int insert_at(struct badblocks *bb, int at, struct badblocks_context *bad)
{
	u64 *p = bb->page;
	int len;

	WARN_ON(badblocks_full(bb));

	len = min_t(sector_t, bad->len, BB_MAX_LEN);
	if (at < bb->count)
		memmove(p + at + 1, p + at, (bb->count - at) * 8);
	p[at] = BB_MAKE(bad->start, len, bad->ack);

	return len;
}

static void badblocks_update_acked(struct badblocks *bb)
{
	bool unacked = false;
	u64 *p = bb->page;
	int i;

	if (!bb->unacked_exist)
		return;

	for (i = 0; i < bb->count ; i++) {
		if (!BB_ACK(p[i])) {
			unacked = true;
			break;
		}
	}

	if (!unacked)
		bb->unacked_exist = 0;
}

/* Do exact work to set bad block range into the bad block table */
static int _badblocks_set(struct badblocks *bb, sector_t s, int sectors,
			  int acknowledged)
{
	int retried = 0, space_desired = 0;
	int orig_len, len = 0, added = 0;
	struct badblocks_context bad;
	int prev = -1, hint = -1;
	sector_t orig_start;
	unsigned long flags;
	int rv = 0;
	u64 *p;

	if (bb->shift < 0)
		/* badblocks are disabled */
		return 1;

	if (sectors == 0)
		/* Invalid sectors number */
		return 1;

	if (bb->shift) {
		/* round the start down, and the end up */
		sector_t next = s + sectors;

		rounddown(s, bb->shift);
		roundup(next, bb->shift);
		sectors = next - s;
	}

	write_seqlock_irqsave(&bb->lock, flags);

	orig_start = s;
	orig_len = sectors;
	bad.ack = acknowledged;
	p = bb->page;

re_insert:
	bad.start = s;
	bad.len = sectors;
	len = 0;

	if (badblocks_empty(bb)) {
		len = insert_at(bb, 0, &bad);
		bb->count++;
		added++;
		goto update_sectors;
	}

	prev = prev_badblocks(bb, &bad, hint);

	/* start before all badblocks */
	if (prev < 0) {
		if (!badblocks_full(bb)) {
			/* insert on the first */
			if (bad.len > (BB_OFFSET(p[0]) - bad.start))
				bad.len = BB_OFFSET(p[0]) - bad.start;
			len = insert_at(bb, 0, &bad);
			bb->count++;
			added++;
			hint = 0;
			goto update_sectors;
		}

		/* No sapce, try to merge */
		if (overlap_behind(bb, &bad, 0)) {
			if (can_merge_behind(bb, &bad, 0)) {
				len = behind_merge(bb, &bad, 0);
				added++;
			} else {
				len = BB_OFFSET(p[0]) - s;
				space_desired = 1;
			}
			hint = 0;
			goto update_sectors;
		}

		/* no table space and give up */
		goto out;
	}

	/* in case p[prev-1] can be merged with p[prev] */
	if (can_combine_front(bb, prev, &bad)) {
		front_combine(bb, prev);
		bb->count--;
		added++;
		hint = prev;
		goto update_sectors;
	}

	if (overlap_front(bb, prev, &bad)) {
		if (can_merge_front(bb, prev, &bad)) {
			len = front_merge(bb, prev, &bad);
			added++;
		} else {
			int extra = 0;

			if (!can_front_overwrite(bb, prev, &bad, &extra)) {
				len = min_t(sector_t,
					    BB_END(p[prev]) - s, sectors);
				hint = prev;
				goto update_sectors;
			}

			len = front_overwrite(bb, prev, &bad, extra);
			added++;
			bb->count += extra;

			if (can_combine_front(bb, prev, &bad)) {
				front_combine(bb, prev);
				bb->count--;
			}
		}
		hint = prev;
		goto update_sectors;
	}

	if (can_merge_front(bb, prev, &bad)) {
		len = front_merge(bb, prev, &bad);
		added++;
		hint = prev;
		goto update_sectors;
	}

	/* if no space in table, still try to merge in the covered range */
	if (badblocks_full(bb)) {
		/* skip the cannot-merge range */
		if (((prev + 1) < bb->count) &&
		    overlap_behind(bb, &bad, prev + 1) &&
		    ((s + sectors) >= BB_END(p[prev + 1]))) {
			len = BB_END(p[prev + 1]) - s;
			hint = prev + 1;
			goto update_sectors;
		}

		/* no retry any more */
		len = sectors;
		space_desired = 1;
		hint = -1;
		goto update_sectors;
	}

	/* cannot merge and there is space in bad table */
	if ((prev + 1) < bb->count &&
	    overlap_behind(bb, &bad, prev + 1))
		bad.len = min_t(sector_t,
				bad.len, BB_OFFSET(p[prev + 1]) - bad.start);

	len = insert_at(bb, prev + 1, &bad);
	bb->count++;
	added++;
	hint = prev + 1;

update_sectors:
	s += len;
	sectors -= len;

	if (sectors > 0)
		goto re_insert;

	WARN_ON(sectors < 0);

	/*
	 * Check whether the following already set range can be
	 * merged. (prev < 0) condition is not handled here,
	 * because it's already complicated enough.
	 */
	if (prev >= 0 &&
	    (prev + 1) < bb->count &&
	    BB_END(p[prev]) == BB_OFFSET(p[prev + 1]) &&
	    (BB_LEN(p[prev]) + BB_LEN(p[prev + 1])) <= BB_MAX_LEN &&
	    BB_ACK(p[prev]) == BB_ACK(p[prev + 1])) {
		p[prev] = BB_MAKE(BB_OFFSET(p[prev]),
				  BB_LEN(p[prev]) + BB_LEN(p[prev + 1]),
				  BB_ACK(p[prev]));

		if ((prev + 2) < bb->count)
			memmove(p + prev + 1, p + prev + 2,
				(bb->count -  (prev + 2)) * 8);
		bb->count--;
	}

	if (space_desired && !badblocks_full(bb)) {
		s = orig_start;
		sectors = orig_len;
		space_desired = 0;
		if (retried++ < 3)
			goto re_insert;
	}

out:
	if (added) {
		set_changed(bb);

		if (!acknowledged)
			bb->unacked_exist = 1;
		else
			badblocks_update_acked(bb);
	}

	write_sequnlock_irqrestore(&bb->lock, flags);

	if (!added)
		rv = 1;

	return rv;
}

/**
 * badblocks_check() - check a given range for bad sectors
 * @bb:		the badblocks structure that holds all badblock information
 * @s:		sector (start) at which to check for badblocks
 * @sectors:	number of sectors to check for badblocks
 * @first_bad:	pointer to store location of the first badblock
 * @bad_sectors: pointer to store number of badblocks after @first_bad
 *
 * We can record which blocks on each device are 'bad' and so just
 * fail those blocks, or that stripe, rather than the whole device.
 * Entries in the bad-block table are 64bits wide.  This comprises:
 * Length of bad-range, in sectors: 0-511 for lengths 1-512
 * Start of bad-range, sector offset, 54 bits (allows 8 exbibytes)
 *  A 'shift' can be set so that larger blocks are tracked and
 *  consequently larger devices can be covered.
 * 'Acknowledged' flag - 1 bit. - the most significant bit.
 *
 * Locking of the bad-block table uses a seqlock so badblocks_check
 * might need to retry if it is very unlucky.
 * We will sometimes want to check for bad blocks in a bi_end_io function,
 * so we use the write_seqlock_irq variant.
 *
 * When looking for a bad block we specify a range and want to
 * know if any block in the range is bad.  So we binary-search
 * to the last range that starts at-or-before the given endpoint,
 * (or "before the sector after the target range")
 * then see if it ends after the given start.
 *
 * Return:
 *  0: there are no known bad blocks in the range
 *  1: there are known bad block which are all acknowledged
 * -1: there are bad blocks which have not yet been acknowledged in metadata.
 * plus the start/length of the first bad section we overlap.
 */
int badblocks_check(struct badblocks *bb, sector_t s, int sectors,
			sector_t *first_bad, int *bad_sectors)
{
	int hi;
	int lo;
	u64 *p = bb->page;
	int rv;
	sector_t target = s + sectors;
	unsigned seq;

	if (bb->shift > 0) {
		/* round the start down, and the end up */
		s >>= bb->shift;
		target += (1<<bb->shift) - 1;
		target >>= bb->shift;
	}
	/* 'target' is now the first block after the bad range */

retry:
	seq = read_seqbegin(&bb->lock);
	lo = 0;
	rv = 0;
	hi = bb->count;

	/* Binary search between lo and hi for 'target'
	 * i.e. for the last range that starts before 'target'
	 */
	/* INVARIANT: ranges before 'lo' and at-or-after 'hi'
	 * are known not to be the last range before target.
	 * VARIANT: hi-lo is the number of possible
	 * ranges, and decreases until it reaches 1
	 */
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		sector_t a = BB_OFFSET(p[mid]);

		if (a < target)
			/* This could still be the one, earlier ranges
			 * could not.
			 */
			lo = mid;
		else
			/* This and later ranges are definitely out. */
			hi = mid;
	}
	/* 'lo' might be the last that started before target, but 'hi' isn't */
	if (hi > lo) {
		/* need to check all range that end after 's' to see if
		 * any are unacknowledged.
		 */
		while (lo >= 0 &&
		       BB_OFFSET(p[lo]) + BB_LEN(p[lo]) > s) {
			if (BB_OFFSET(p[lo]) < target) {
				/* starts before the end, and finishes after
				 * the start, so they must overlap
				 */
				if (rv != -1 && BB_ACK(p[lo]))
					rv = 1;
				else
					rv = -1;
				*first_bad = BB_OFFSET(p[lo]);
				*bad_sectors = BB_LEN(p[lo]);
			}
			lo--;
		}
	}

	if (read_seqretry(&bb->lock, seq))
		goto retry;

	return rv;
}
EXPORT_SYMBOL_GPL(badblocks_check);

/**
 * badblocks_set() - Add a range of bad blocks to the table.
 * @bb:		the badblocks structure that holds all badblock information
 * @s:		first sector to mark as bad
 * @sectors:	number of sectors to mark as bad
 * @acknowledged: weather to mark the bad sectors as acknowledged
 *
 * This might extend the table, or might contract it if two adjacent ranges
 * can be merged. We binary-search to find the 'insertion' point, then
 * decide how best to handle it.
 *
 * Return:
 *  0: success
 *  1: failed to set badblocks (out of space)
 */
int badblocks_set(struct badblocks *bb, sector_t s, int sectors,
			int acknowledged)
{
	u64 *p;
	int lo, hi;
	int rv = 0;
	unsigned long flags;

	if (bb->shift < 0)
		/* badblocks are disabled */
		return 1;

	if (bb->shift) {
		/* round the start down, and the end up */
		sector_t next = s + sectors;

		s >>= bb->shift;
		next += (1<<bb->shift) - 1;
		next >>= bb->shift;
		sectors = next - s;
	}

	write_seqlock_irqsave(&bb->lock, flags);

	p = bb->page;
	lo = 0;
	hi = bb->count;
	/* Find the last range that starts at-or-before 's' */
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		sector_t a = BB_OFFSET(p[mid]);

		if (a <= s)
			lo = mid;
		else
			hi = mid;
	}
	if (hi > lo && BB_OFFSET(p[lo]) > s)
		hi = lo;

	if (hi > lo) {
		/* we found a range that might merge with the start
		 * of our new range
		 */
		sector_t a = BB_OFFSET(p[lo]);
		sector_t e = a + BB_LEN(p[lo]);
		int ack = BB_ACK(p[lo]);

		if (e >= s) {
			/* Yes, we can merge with a previous range */
			if (s == a && s + sectors >= e)
				/* new range covers old */
				ack = acknowledged;
			else
				ack = ack && acknowledged;

			if (e < s + sectors)
				e = s + sectors;
			if (e - a <= BB_MAX_LEN) {
				p[lo] = BB_MAKE(a, e-a, ack);
				s = e;
			} else {
				/* does not all fit in one range,
				 * make p[lo] maximal
				 */
				if (BB_LEN(p[lo]) != BB_MAX_LEN)
					p[lo] = BB_MAKE(a, BB_MAX_LEN, ack);
				s = a + BB_MAX_LEN;
			}
			sectors = e - s;
		}
	}
	if (sectors && hi < bb->count) {
		/* 'hi' points to the first range that starts after 's'.
		 * Maybe we can merge with the start of that range
		 */
		sector_t a = BB_OFFSET(p[hi]);
		sector_t e = a + BB_LEN(p[hi]);
		int ack = BB_ACK(p[hi]);

		if (a <= s + sectors) {
			/* merging is possible */
			if (e <= s + sectors) {
				/* full overlap */
				e = s + sectors;
				ack = acknowledged;
			} else
				ack = ack && acknowledged;

			a = s;
			if (e - a <= BB_MAX_LEN) {
				p[hi] = BB_MAKE(a, e-a, ack);
				s = e;
			} else {
				p[hi] = BB_MAKE(a, BB_MAX_LEN, ack);
				s = a + BB_MAX_LEN;
			}
			sectors = e - s;
			lo = hi;
			hi++;
		}
	}
	if (sectors == 0 && hi < bb->count) {
		/* we might be able to combine lo and hi */
		/* Note: 's' is at the end of 'lo' */
		sector_t a = BB_OFFSET(p[hi]);
		int lolen = BB_LEN(p[lo]);
		int hilen = BB_LEN(p[hi]);
		int newlen = lolen + hilen - (s - a);

		if (s >= a && newlen < BB_MAX_LEN) {
			/* yes, we can combine them */
			int ack = BB_ACK(p[lo]) && BB_ACK(p[hi]);

			p[lo] = BB_MAKE(BB_OFFSET(p[lo]), newlen, ack);
			memmove(p + hi, p + hi + 1,
				(bb->count - hi - 1) * 8);
			bb->count--;
		}
	}
	while (sectors) {
		/* didn't merge (it all).
		 * Need to add a range just before 'hi'
		 */
		if (bb->count >= MAX_BADBLOCKS) {
			/* No room for more */
			rv = 1;
			break;
		} else {
			int this_sectors = sectors;

			memmove(p + hi + 1, p + hi,
				(bb->count - hi) * 8);
			bb->count++;

			if (this_sectors > BB_MAX_LEN)
				this_sectors = BB_MAX_LEN;
			p[hi] = BB_MAKE(s, this_sectors, acknowledged);
			sectors -= this_sectors;
			s += this_sectors;
		}
	}

	bb->changed = 1;
	if (!acknowledged)
		bb->unacked_exist = 1;
	else
		badblocks_update_acked(bb);
	write_sequnlock_irqrestore(&bb->lock, flags);

	return rv;
}
EXPORT_SYMBOL_GPL(badblocks_set);

/**
 * badblocks_clear() - Remove a range of bad blocks to the table.
 * @bb:		the badblocks structure that holds all badblock information
 * @s:		first sector to mark as bad
 * @sectors:	number of sectors to mark as bad
 *
 * This may involve extending the table if we spilt a region,
 * but it must not fail.  So if the table becomes full, we just
 * drop the remove request.
 *
 * Return:
 *  0: success
 *  1: failed to clear badblocks
 */
int badblocks_clear(struct badblocks *bb, sector_t s, int sectors)
{
	u64 *p;
	int lo, hi;
	sector_t target = s + sectors;
	int rv = 0;

	if (bb->shift > 0) {
		/* When clearing we round the start up and the end down.
		 * This should not matter as the shift should align with
		 * the block size and no rounding should ever be needed.
		 * However it is better the think a block is bad when it
		 * isn't than to think a block is not bad when it is.
		 */
		s += (1<<bb->shift) - 1;
		s >>= bb->shift;
		target >>= bb->shift;
	}

	write_seqlock_irq(&bb->lock);

	p = bb->page;
	lo = 0;
	hi = bb->count;
	/* Find the last range that starts before 'target' */
	while (hi - lo > 1) {
		int mid = (lo + hi) / 2;
		sector_t a = BB_OFFSET(p[mid]);

		if (a < target)
			lo = mid;
		else
			hi = mid;
	}
	if (hi > lo) {
		/* p[lo] is the last range that could overlap the
		 * current range.  Earlier ranges could also overlap,
		 * but only this one can overlap the end of the range.
		 */
		if ((BB_OFFSET(p[lo]) + BB_LEN(p[lo]) > target) &&
		    (BB_OFFSET(p[lo]) < target)) {
			/* Partial overlap, leave the tail of this range */
			int ack = BB_ACK(p[lo]);
			sector_t a = BB_OFFSET(p[lo]);
			sector_t end = a + BB_LEN(p[lo]);

			if (a < s) {
				/* we need to split this range */
				if (bb->count >= MAX_BADBLOCKS) {
					rv = -ENOSPC;
					goto out;
				}
				memmove(p+lo+1, p+lo, (bb->count - lo) * 8);
				bb->count++;
				p[lo] = BB_MAKE(a, s-a, ack);
				lo++;
			}
			p[lo] = BB_MAKE(target, end - target, ack);
			/* there is no longer an overlap */
			hi = lo;
			lo--;
		}
		while (lo >= 0 &&
		       (BB_OFFSET(p[lo]) + BB_LEN(p[lo]) > s) &&
		       (BB_OFFSET(p[lo]) < target)) {
			/* This range does overlap */
			if (BB_OFFSET(p[lo]) < s) {
				/* Keep the early parts of this range. */
				int ack = BB_ACK(p[lo]);
				sector_t start = BB_OFFSET(p[lo]);

				p[lo] = BB_MAKE(start, s - start, ack);
				/* now low doesn't overlap, so.. */
				break;
			}
			lo--;
		}
		/* 'lo' is strictly before, 'hi' is strictly after,
		 * anything between needs to be discarded
		 */
		if (hi - lo > 1) {
			memmove(p+lo+1, p+hi, (bb->count - hi) * 8);
			bb->count -= (hi - lo - 1);
		}
	}

	badblocks_update_acked(bb);
	bb->changed = 1;
out:
	write_sequnlock_irq(&bb->lock);
	return rv;
}
EXPORT_SYMBOL_GPL(badblocks_clear);

/**
 * ack_all_badblocks() - Acknowledge all bad blocks in a list.
 * @bb:		the badblocks structure that holds all badblock information
 *
 * This only succeeds if ->changed is clear.  It is used by
 * in-kernel metadata updates
 */
void ack_all_badblocks(struct badblocks *bb)
{
	if (bb->page == NULL || bb->changed)
		/* no point even trying */
		return;
	write_seqlock_irq(&bb->lock);

	if (bb->changed == 0 && bb->unacked_exist) {
		u64 *p = bb->page;
		int i;

		for (i = 0; i < bb->count ; i++) {
			if (!BB_ACK(p[i])) {
				sector_t start = BB_OFFSET(p[i]);
				int len = BB_LEN(p[i]);

				p[i] = BB_MAKE(start, len, 1);
			}
		}
		bb->unacked_exist = 0;
	}
	write_sequnlock_irq(&bb->lock);
}
EXPORT_SYMBOL_GPL(ack_all_badblocks);

/**
 * badblocks_show() - sysfs access to bad-blocks list
 * @bb:		the badblocks structure that holds all badblock information
 * @page:	buffer received from sysfs
 * @unack:	weather to show unacknowledged badblocks
 *
 * Return:
 *  Length of returned data
 */
ssize_t badblocks_show(struct badblocks *bb, char *page, int unack)
{
	size_t len;
	int i;
	u64 *p = bb->page;
	unsigned seq;

	if (bb->shift < 0)
		return 0;

retry:
	seq = read_seqbegin(&bb->lock);

	len = 0;
	i = 0;

	while (len < PAGE_SIZE && i < bb->count) {
		sector_t s = BB_OFFSET(p[i]);
		unsigned int length = BB_LEN(p[i]);
		int ack = BB_ACK(p[i]);

		i++;

		if (unack && ack)
			continue;

		len += snprintf(page+len, PAGE_SIZE-len, "%llu %u\n",
				(unsigned long long)s << bb->shift,
				length << bb->shift);
	}
	if (unack && len == 0)
		bb->unacked_exist = 0;

	if (read_seqretry(&bb->lock, seq))
		goto retry;

	return len;
}
EXPORT_SYMBOL_GPL(badblocks_show);

/**
 * badblocks_store() - sysfs access to bad-blocks list
 * @bb:		the badblocks structure that holds all badblock information
 * @page:	buffer received from sysfs
 * @len:	length of data received from sysfs
 * @unack:	weather to show unacknowledged badblocks
 *
 * Return:
 *  Length of the buffer processed or -ve error.
 */
ssize_t badblocks_store(struct badblocks *bb, const char *page, size_t len,
			int unack)
{
	unsigned long long sector;
	int length;
	char newline;

	switch (sscanf(page, "%llu %d%c", &sector, &length, &newline)) {
	case 3:
		if (newline != '\n')
			return -EINVAL;
		fallthrough;
	case 2:
		if (length <= 0)
			return -EINVAL;
		break;
	default:
		return -EINVAL;
	}

	if (badblocks_set(bb, sector, length, !unack))
		return -ENOSPC;
	else
		return len;
}
EXPORT_SYMBOL_GPL(badblocks_store);

static int __badblocks_init(struct device *dev, struct badblocks *bb,
		int enable)
{
	bb->dev = dev;
	bb->count = 0;
	if (enable)
		bb->shift = 0;
	else
		bb->shift = -1;
	if (dev)
		bb->page = devm_kzalloc(dev, PAGE_SIZE, GFP_KERNEL);
	else
		bb->page = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!bb->page) {
		bb->shift = -1;
		return -ENOMEM;
	}
	seqlock_init(&bb->lock);

	return 0;
}

/**
 * badblocks_init() - initialize the badblocks structure
 * @bb:		the badblocks structure that holds all badblock information
 * @enable:	weather to enable badblocks accounting
 *
 * Return:
 *  0: success
 *  -ve errno: on error
 */
int badblocks_init(struct badblocks *bb, int enable)
{
	return __badblocks_init(NULL, bb, enable);
}
EXPORT_SYMBOL_GPL(badblocks_init);

int devm_init_badblocks(struct device *dev, struct badblocks *bb)
{
	if (!bb)
		return -EINVAL;
	return __badblocks_init(dev, bb, 1);
}
EXPORT_SYMBOL_GPL(devm_init_badblocks);

/**
 * badblocks_exit() - free the badblocks structure
 * @bb:		the badblocks structure that holds all badblock information
 */
void badblocks_exit(struct badblocks *bb)
{
	if (!bb)
		return;
	if (bb->dev)
		devm_kfree(bb->dev, bb->page);
	else
		kfree(bb->page);
	bb->page = NULL;
}
EXPORT_SYMBOL_GPL(badblocks_exit);
