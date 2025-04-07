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
 *
 *
 * Clearing a bad blocks range from the bad block table has similar idea as
 * setting does, but much more simpler. The only thing needs to be noticed is
 * when the clearing range hits middle of a bad block range, the existing bad
 * block range will split into two, and one more item should be added into the
 * bad block table. The simplified situations to be considered are, (The already
 * set bad blocks ranges in bad block table are naming with prefix E, and the
 * clearing bad blocks range is naming with prefix C)
 *
 * 1) A clearing range is not overlapped to any already set ranges in bad block
 *    table.
 *    +-----+         |          +-----+         |          +-----+
 *    |  C  |         |          |  C  |         |          |  C  |
 *    +-----+         or         +-----+         or         +-----+
 *            +---+   |   +----+         +----+  |  +---+
 *            | E |   |   | E1 |         | E2 |  |  | E |
 *            +---+   |   +----+         +----+  |  +---+
 *    For the above situations, no bad block to be cleared and no failure
 *    happens, simply returns 0.
 * 2) The clearing range hits middle of an already setting bad blocks range in
 *    the bad block table.
 *            +---+
 *            | C |
 *            +---+
 *     +-----------------+
 *     |         E       |
 *     +-----------------+
 *    In this situation if the bad block table is not full, the range E will be
 *    split into two ranges E1 and E2. The result is,
 *     +------+   +------+
 *     |  E1  |   |  E2  |
 *     +------+   +------+
 * 3) The clearing range starts exactly at same LBA as an already set bad block range
 *    from the bad block table.
 * 3.1) Partially covered at head part
 *         +------------+
 *         |     C      |
 *         +------------+
 *         +-----------------+
 *         |         E       |
 *         +-----------------+
 *    For this situation, the overlapped already set range will update the
 *    start LBA to end of C and shrink the range to BB_LEN(E) - BB_LEN(C). No
 *    item deleted from bad block table. The result is,
 *                      +----+
 *                      | E1 |
 *                      +----+
 * 3.2) Exact fully covered
 *         +-----------------+
 *         |         C       |
 *         +-----------------+
 *         +-----------------+
 *         |         E       |
 *         +-----------------+
 *    For this situation the whole bad blocks range E will be cleared and its
 *    corresponded item is deleted from the bad block table.
 * 4) The clearing range exactly ends at same LBA as an already set bad block
 *    range.
 *                   +-------+
 *                   |   C   |
 *                   +-------+
 *         +-----------------+
 *         |         E       |
 *         +-----------------+
 *    For the above situation, the already set range E is updated to shrink its
 *    end to the start of C, and reduce its length to BB_LEN(E) - BB_LEN(C).
 *    The result is,
 *         +---------+
 *         |    E    |
 *         +---------+
 * 5) The clearing range is partially overlapped with an already set bad block
 *    range from the bad block table.
 * 5.1) The already set bad block range is front overlapped with the clearing
 *    range.
 *         +----------+
 *         |     C    |
 *         +----------+
 *              +------------+
 *              |      E     |
 *              +------------+
 *   For such situation, the clearing range C can be treated as two parts. The
 *   first part ends at the start LBA of range E, and the second part starts at
 *   same LBA of range E.
 *         +----+-----+               +----+   +-----+
 *         | C1 | C2  |               | C1 |   | C2  |
 *         +----+-----+         ===>  +----+   +-----+
 *              +------------+                 +------------+
 *              |      E     |                 |      E     |
 *              +------------+                 +------------+
 *   Now the first part C1 can be handled as condition 1), and the second part C2 can be
 *   handled as condition 3.1) in next loop.
 * 5.2) The already set bad block range is behind overlaopped with the clearing
 *   range.
 *                 +----------+
 *                 |     C    |
 *                 +----------+
 *         +------------+
 *         |      E     |
 *         +------------+
 *   For such situation, the clearing range C can be treated as two parts. The
 *   first part C1 ends at same end LBA of range E, and the second part starts
 *   at end LBA of range E.
 *                 +----+-----+                 +----+    +-----+
 *                 | C1 | C2  |                 | C1 |    | C2  |
 *                 +----+-----+  ===>           +----+    +-----+
 *         +------------+               +------------+
 *         |      E     |               |      E     |
 *         +------------+               +------------+
 *   Now the first part clearing range C1 can be handled as condition 4), and
 *   the second part clearing range C2 can be handled as condition 1) in next
 *   loop.
 *
 *   All bad blocks range clearing can be simplified into the above 5 situations
 *   by only handling the head part of the clearing range in each run of the
 *   while-loop. The idea is similar to bad blocks range setting but much
 *   simpler.
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

	if ((bb->count + (*extra)) > MAX_BADBLOCKS)
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

/*
 * Return 'true' if the range indicated by 'bad' is exactly backward
 * overlapped with the bad range (from bad table) indexed by 'behind'.
 */
static bool try_adjacent_combine(struct badblocks *bb, int prev)
{
	u64 *p = bb->page;

	if (prev >= 0 && (prev + 1) < bb->count &&
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
		return true;
	}
	return false;
}

/* Do exact work to set bad block range into the bad block table */
static bool _badblocks_set(struct badblocks *bb, sector_t s, sector_t sectors,
			   int acknowledged)
{
	int len = 0, added = 0;
	struct badblocks_context bad;
	int prev = -1, hint = -1;
	unsigned long flags;
	u64 *p;

	if (bb->shift < 0)
		/* badblocks are disabled */
		return false;

	if (sectors == 0)
		/* Invalid sectors number */
		return false;

	if (bb->shift) {
		/* round the start down, and the end up */
		sector_t next = s + sectors;

		rounddown(s, 1 << bb->shift);
		roundup(next, 1 << bb->shift);
		sectors = next - s;
	}

	write_seqlock_irqsave(&bb->lock, flags);

	bad.ack = acknowledged;
	p = bb->page;

re_insert:
	bad.start = s;
	bad.len = sectors;
	len = 0;

	if (badblocks_full(bb))
		goto out;

	if (badblocks_empty(bb)) {
		len = insert_at(bb, 0, &bad);
		bb->count++;
		added++;
		goto update_sectors;
	}

	prev = prev_badblocks(bb, &bad, hint);

	/* start before all badblocks */
	if (prev < 0) {
		/* insert on the first */
		if (bad.len > (BB_OFFSET(p[0]) - bad.start))
			bad.len = BB_OFFSET(p[0]) - bad.start;
		len = insert_at(bb, 0, &bad);
		bb->count++;
		added++;
		hint = ++prev;
		goto update_sectors;
	}

	/* in case p[prev-1] can be merged with p[prev] */
	if (can_combine_front(bb, prev, &bad)) {
		front_combine(bb, prev);
		bb->count--;
		added++;
		hint = prev;
		goto update_sectors;
	}

	if (can_merge_front(bb, prev, &bad)) {
		len = front_merge(bb, prev, &bad);
		added++;
		hint = prev;
		goto update_sectors;
	}

	if (overlap_front(bb, prev, &bad)) {
		int extra = 0;

		if (!can_front_overwrite(bb, prev, &bad, &extra)) {
			if (extra > 0)
				goto out;

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

		hint = prev;
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
	hint = ++prev;

update_sectors:
	s += len;
	sectors -= len;

	if (sectors > 0)
		goto re_insert;

	/*
	 * Check whether the following already set range can be
	 * merged. (prev < 0) condition is not handled here,
	 * because it's already complicated enough.
	 */
	try_adjacent_combine(bb, prev);

out:
	if (added) {
		set_changed(bb);

		if (!acknowledged)
			bb->unacked_exist = 1;
		else
			badblocks_update_acked(bb);
	}

	write_sequnlock_irqrestore(&bb->lock, flags);

	return sectors == 0;
}

/*
 * Clear the bad block range from bad block table which is front overlapped
 * with the clearing range. The return value is how many sectors from an
 * already set bad block range are cleared. If the whole bad block range is
 * covered by the clearing range and fully cleared, 'delete' is set as 1 for
 * the caller to reduce bb->count.
 */
static int front_clear(struct badblocks *bb, int prev,
		       struct badblocks_context *bad, int *deleted)
{
	sector_t sectors = bad->len;
	sector_t s = bad->start;
	u64 *p = bb->page;
	int cleared = 0;

	*deleted = 0;
	if (s == BB_OFFSET(p[prev])) {
		if (BB_LEN(p[prev]) > sectors) {
			p[prev] = BB_MAKE(BB_OFFSET(p[prev]) + sectors,
					  BB_LEN(p[prev]) - sectors,
					  BB_ACK(p[prev]));
			cleared = sectors;
		} else {
			/* BB_LEN(p[prev]) <= sectors */
			cleared = BB_LEN(p[prev]);
			if ((prev + 1) < bb->count)
				memmove(p + prev, p + prev + 1,
				       (bb->count - prev - 1) * 8);
			*deleted = 1;
		}
	} else if (s > BB_OFFSET(p[prev])) {
		if (BB_END(p[prev]) <= (s + sectors)) {
			cleared = BB_END(p[prev]) - s;
			p[prev] = BB_MAKE(BB_OFFSET(p[prev]),
					  s - BB_OFFSET(p[prev]),
					  BB_ACK(p[prev]));
		} else {
			/* Splitting is handled in front_splitting_clear() */
			BUG();
		}
	}

	return cleared;
}

/*
 * Handle the condition that the clearing range hits middle of an already set
 * bad block range from bad block table. In this condition the existing bad
 * block range is split into two after the middle part is cleared.
 */
static int front_splitting_clear(struct badblocks *bb, int prev,
				  struct badblocks_context *bad)
{
	u64 *p = bb->page;
	u64 end = BB_END(p[prev]);
	int ack = BB_ACK(p[prev]);
	sector_t sectors = bad->len;
	sector_t s = bad->start;

	p[prev] = BB_MAKE(BB_OFFSET(p[prev]),
			  s - BB_OFFSET(p[prev]),
			  ack);
	memmove(p + prev + 2, p + prev + 1, (bb->count - prev - 1) * 8);
	p[prev + 1] = BB_MAKE(s + sectors, end - s - sectors, ack);
	return sectors;
}

/* Do the exact work to clear bad block range from the bad block table */
static bool _badblocks_clear(struct badblocks *bb, sector_t s, sector_t sectors)
{
	struct badblocks_context bad;
	int prev = -1, hint = -1;
	int len = 0, cleared = 0;
	u64 *p;

	if (bb->shift < 0)
		/* badblocks are disabled */
		return false;

	if (sectors == 0)
		/* Invalid sectors number */
		return false;

	if (bb->shift) {
		sector_t target;

		/* When clearing we round the start up and the end down.
		 * This should not matter as the shift should align with
		 * the block size and no rounding should ever be needed.
		 * However it is better the think a block is bad when it
		 * isn't than to think a block is not bad when it is.
		 */
		target = s + sectors;
		roundup(s, 1 << bb->shift);
		rounddown(target, 1 << bb->shift);
		sectors = target - s;
	}

	write_seqlock_irq(&bb->lock);

	bad.ack = true;
	p = bb->page;

re_clear:
	bad.start = s;
	bad.len = sectors;

	if (badblocks_empty(bb)) {
		len = sectors;
		cleared++;
		goto update_sectors;
	}


	prev = prev_badblocks(bb, &bad, hint);

	/* Start before all badblocks */
	if (prev < 0) {
		if (overlap_behind(bb, &bad, 0)) {
			len = BB_OFFSET(p[0]) - s;
			hint = 0;
		} else {
			len = sectors;
		}
		/*
		 * Both situations are to clear non-bad range,
		 * should be treated as successful
		 */
		cleared++;
		goto update_sectors;
	}

	/* Start after all badblocks */
	if ((prev + 1) >= bb->count && !overlap_front(bb, prev, &bad)) {
		len = sectors;
		cleared++;
		goto update_sectors;
	}

	/* Clear will split a bad record but the table is full */
	if (badblocks_full(bb) && (BB_OFFSET(p[prev]) < bad.start) &&
	    (BB_END(p[prev]) > (bad.start + sectors))) {
		len = sectors;
		goto update_sectors;
	}

	if (overlap_front(bb, prev, &bad)) {
		if ((BB_OFFSET(p[prev]) < bad.start) &&
		    (BB_END(p[prev]) > (bad.start + bad.len))) {
			/* Splitting */
			if ((bb->count + 1) <= MAX_BADBLOCKS) {
				len = front_splitting_clear(bb, prev, &bad);
				bb->count += 1;
				cleared++;
			} else {
				/* No space to split, give up */
				len = sectors;
			}
		} else {
			int deleted = 0;

			len = front_clear(bb, prev, &bad, &deleted);
			bb->count -= deleted;
			cleared++;
			hint = prev;
		}

		goto update_sectors;
	}

	/* Not front overlap, but behind overlap */
	if ((prev + 1) < bb->count && overlap_behind(bb, &bad, prev + 1)) {
		len = BB_OFFSET(p[prev + 1]) - bad.start;
		hint = prev + 1;
		/* Clear non-bad range should be treated as successful */
		cleared++;
		goto update_sectors;
	}

	/* Not cover any badblocks range in the table */
	len = sectors;
	/* Clear non-bad range should be treated as successful */
	cleared++;

update_sectors:
	s += len;
	sectors -= len;

	if (sectors > 0)
		goto re_clear;

	if (cleared) {
		badblocks_update_acked(bb);
		set_changed(bb);
	}

	write_sequnlock_irq(&bb->lock);

	if (!cleared)
		return false;

	return true;
}

/* Do the exact work to check bad blocks range from the bad block table */
static int _badblocks_check(struct badblocks *bb, sector_t s, sector_t sectors,
			    sector_t *first_bad, sector_t *bad_sectors)
{
	int prev = -1, hint = -1, set = 0;
	struct badblocks_context bad;
	int unacked_badblocks = 0;
	int acked_badblocks = 0;
	u64 *p = bb->page;
	int len, rv;

re_check:
	bad.start = s;
	bad.len = sectors;

	if (badblocks_empty(bb)) {
		len = sectors;
		goto update_sectors;
	}

	prev = prev_badblocks(bb, &bad, hint);

	/* start after all badblocks */
	if ((prev >= 0) &&
	    ((prev + 1) >= bb->count) && !overlap_front(bb, prev, &bad)) {
		len = sectors;
		goto update_sectors;
	}

	/* Overlapped with front badblocks record */
	if ((prev >= 0) && overlap_front(bb, prev, &bad)) {
		if (BB_ACK(p[prev]))
			acked_badblocks++;
		else
			unacked_badblocks++;

		if (BB_END(p[prev]) >= (s + sectors))
			len = sectors;
		else
			len = BB_END(p[prev]) - s;

		if (set == 0) {
			*first_bad = BB_OFFSET(p[prev]);
			*bad_sectors = BB_LEN(p[prev]);
			set = 1;
		}
		goto update_sectors;
	}

	/* Not front overlap, but behind overlap */
	if ((prev + 1) < bb->count && overlap_behind(bb, &bad, prev + 1)) {
		len = BB_OFFSET(p[prev + 1]) - bad.start;
		hint = prev + 1;
		goto update_sectors;
	}

	/* not cover any badblocks range in the table */
	len = sectors;

update_sectors:
	/* This situation should never happen */
	WARN_ON(sectors < len);

	s += len;
	sectors -= len;

	if (sectors > 0)
		goto re_check;

	if (unacked_badblocks > 0)
		rv = -1;
	else if (acked_badblocks > 0)
		rv = 1;
	else
		rv = 0;

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
int badblocks_check(struct badblocks *bb, sector_t s, sector_t sectors,
			sector_t *first_bad, sector_t *bad_sectors)
{
	unsigned int seq;
	int rv;

	WARN_ON(bb->shift < 0 || sectors == 0);

	if (bb->shift > 0) {
		/* round the start down, and the end up */
		sector_t target = s + sectors;

		rounddown(s, 1 << bb->shift);
		roundup(target, 1 << bb->shift);
		sectors = target - s;
	}

retry:
	seq = read_seqbegin(&bb->lock);
	rv = _badblocks_check(bb, s, sectors, first_bad, bad_sectors);
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
 *  true: success
 *  false: failed to set badblocks (out of space). Parital setting will be
 *  treated as failure.
 */
bool badblocks_set(struct badblocks *bb, sector_t s, sector_t sectors,
		   int acknowledged)
{
	return _badblocks_set(bb, s, sectors, acknowledged);
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
 *  true: success
 *  false: failed to clear badblocks
 */
bool badblocks_clear(struct badblocks *bb, sector_t s, sector_t sectors)
{
	return _badblocks_clear(bb, s, sectors);
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

		for (i = 0; i < bb->count ; i++)
			while (try_adjacent_combine(bb, i))
				;

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

	if (!badblocks_set(bb, sector, length, !unack))
		return -ENOSPC;

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
