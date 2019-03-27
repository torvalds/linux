/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Given several files containing CTF data, merge and uniquify that data into
 * a single CTF section in an output file.
 *
 * Merges can proceed independently.  As such, we perform the merges in parallel
 * using a worker thread model.  A given glob of CTF data (either all of the CTF
 * data from a single input file, or the result of one or more merges) can only
 * be involved in a single merge at any given time, so the process decreases in
 * parallelism, especially towards the end, as more and more files are
 * consolidated, finally resulting in a single merge of two large CTF graphs.
 * Unfortunately, the last merge is also the slowest, as the two graphs being
 * merged are each the product of merges of half of the input files.
 *
 * The algorithm consists of two phases, described in detail below.  The first
 * phase entails the merging of CTF data in groups of eight.  The second phase
 * takes the results of Phase I, and merges them two at a time.  This disparity
 * is due to an observation that the merge time increases at least quadratically
 * with the size of the CTF data being merged.  As such, merges of CTF graphs
 * newly read from input files are much faster than merges of CTF graphs that
 * are themselves the results of prior merges.
 *
 * A further complication is the need to ensure the repeatability of CTF merges.
 * That is, a merge should produce the same output every time, given the same
 * input.  In both phases, this consistency requirement is met by imposing an
 * ordering on the merge process, thus ensuring that a given set of input files
 * are merged in the same order every time.
 *
 *   Phase I
 *
 *   The main thread reads the input files one by one, transforming the CTF
 *   data they contain into tdata structures.  When a given file has been read
 *   and parsed, it is placed on the work queue for retrieval by worker threads.
 *
 *   Central to Phase I is the Work In Progress (wip) array, which is used to
 *   merge batches of files in a predictable order.  Files are read by the main
 *   thread, and are merged into wip array elements in round-robin order.  When
 *   the number of files merged into a given array slot equals the batch size,
 *   the merged CTF graph in that array is added to the done slot in order by
 *   array slot.
 *
 *   For example, consider a case where we have five input files, a batch size
 *   of two, a wip array size of two, and two worker threads (T1 and T2).
 *
 *    1. The wip array elements are assigned initial batch numbers 0 and 1.
 *    2. T1 reads an input file from the input queue (wq_queue).  This is the
 *       first input file, so it is placed into wip[0].  The second file is
 *       similarly read and placed into wip[1].  The wip array slots now contain
 *       one file each (wip_nmerged == 1).
 *    3. T1 reads the third input file, which it merges into wip[0].  The
 *       number of files in wip[0] is equal to the batch size.
 *    4. T2 reads the fourth input file, which it merges into wip[1].  wip[1]
 *       is now full too.
 *    5. T2 attempts to place the contents of wip[1] on the done queue
 *       (wq_done_queue), but it can't, since the batch ID for wip[1] is 1.
 *       Batch 0 needs to be on the done queue before batch 1 can be added, so
 *       T2 blocks on wip[1]'s cv.
 *    6. T1 attempts to place the contents of wip[0] on the done queue, and
 *       succeeds, updating wq_lastdonebatch to 0.  It clears wip[0], and sets
 *       its batch ID to 2.  T1 then signals wip[1]'s cv to awaken T2.
 *    7. T2 wakes up, notices that wq_lastdonebatch is 0, which means that
 *       batch 1 can now be added.  It adds wip[1] to the done queue, clears
 *       wip[1], and sets its batch ID to 3.  It signals wip[0]'s cv, and
 *       restarts.
 *
 *   The above process continues until all input files have been consumed.  At
 *   this point, a pair of barriers are used to allow a single thread to move
 *   any partial batches from the wip array to the done array in batch ID order.
 *   When this is complete, wq_done_queue is moved to wq_queue, and Phase II
 *   begins.
 *
 *	Locking Semantics (Phase I)
 *
 *	The input queue (wq_queue) and the done queue (wq_done_queue) are
 *	protected by separate mutexes - wq_queue_lock and wq_done_queue.  wip
 *	array slots are protected by their own mutexes, which must be grabbed
 *	before releasing the input queue lock.  The wip array lock is dropped
 *	when the thread restarts the loop.  If the array slot was full, the
 *	array lock will be held while the slot contents are added to the done
 *	queue.  The done queue lock is used to protect the wip slot cv's.
 *
 *	The pow number is protected by the queue lock.  The master batch ID
 *	and last completed batch (wq_lastdonebatch) counters are protected *in
 *	Phase I* by the done queue lock.
 *
 *   Phase II
 *
 *   When Phase II begins, the queue consists of the merged batches from the
 *   first phase.  Assume we have five batches:
 *
 *	Q:	a b c d e
 *
 *   Using the same batch ID mechanism we used in Phase I, but without the wip
 *   array, worker threads remove two entries at a time from the beginning of
 *   the queue.  These two entries are merged, and are added back to the tail
 *   of the queue, as follows:
 *
 *	Q:	a b c d e	# start
 *	Q:	c d e ab	# a, b removed, merged, added to end
 *	Q:	e ab cd		# c, d removed, merged, added to end
 *	Q:	cd eab		# e, ab removed, merged, added to end
 *	Q:	cdeab		# cd, eab removed, merged, added to end
 *
 *   When one entry remains on the queue, with no merges outstanding, Phase II
 *   finishes.  We pre-determine the stopping point by pre-calculating the
 *   number of nodes that will appear on the list.  In the example above, the
 *   number (wq_ninqueue) is 9.  When ninqueue is 1, we conclude Phase II by
 *   signaling the main thread via wq_done_cv.
 *
 *	Locking Semantics (Phase II)
 *
 *	The queue (wq_queue), ninqueue, and the master batch ID and last
 *	completed batch counters are protected by wq_queue_lock.  The done
 *	queue and corresponding lock are unused in Phase II as is the wip array.
 *
 *   Uniquification
 *
 *   We want the CTF data that goes into a given module to be as small as
 *   possible.  For example, we don't want it to contain any type data that may
 *   be present in another common module.  As such, after creating the master
 *   tdata_t for a given module, we can, if requested by the user, uniquify it
 *   against the tdata_t from another module (genunix in the case of the SunOS
 *   kernel).  We perform a merge between the tdata_t for this module and the
 *   tdata_t from genunix.  Nodes found in this module that are not present in
 *   genunix are added to a third tdata_t - the uniquified tdata_t.
 *
 *   Additive Merges
 *
 *   In some cases, for example if we are issuing a new version of a common
 *   module in a patch, we need to make sure that the CTF data already present
 *   in that module does not change.  Changes to this data would void the CTF
 *   data in any module that uniquified against the common module.  To preserve
 *   the existing data, we can perform what is known as an additive merge.  In
 *   this case, a final uniquification is performed against the CTF data in the
 *   previous version of the module.  The result will be the placement of new
 *   and changed data after the existing data, thus preserving the existing type
 *   ID space.
 *
 *   Saving the result
 *
 *   When the merges are complete, the resulting tdata_t is placed into the
 *   output file, replacing the .SUNW_ctf section (if any) already in that file.
 *
 * The person who changes the merging thread code in this file without updating
 * this comment will not live to see the stock hit five.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#ifdef illumos
#include <synch.h>
#endif
#include <signal.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#ifdef illumos
#include <alloca.h>
#endif
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mman.h>
#ifdef illumos
#include <sys/sysconf.h>
#endif

#include "ctf_headers.h"
#include "ctftools.h"
#include "ctfmerge.h"
#include "traverse.h"
#include "memory.h"
#include "fifo.h"
#include "barrier.h"

#pragma init(bigheap)

#define	MERGE_PHASE1_BATCH_SIZE		8
#define	MERGE_PHASE1_MAX_SLOTS		5
#define	MERGE_INPUT_THROTTLE_LEN	10

const char *progname;
static char *outfile = NULL;
static char *tmpname = NULL;
static int dynsym;
int debug_level = DEBUG_LEVEL;
static size_t maxpgsize = 0x400000;


void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: %s [-fgstv] -l label | -L labelenv -o outfile file ...\n"
	    "       %s [-fgstv] -l label | -L labelenv -o outfile -d uniqfile\n"
	    "       %*s [-g] [-D uniqlabel] file ...\n"
	    "       %s [-fgstv] -l label | -L labelenv -o outfile -w withfile "
	    "file ...\n"
	    "       %s [-g] -c srcfile destfile\n"
	    "\n"
	    "  Note: if -L labelenv is specified and labelenv is not set in\n"
	    "  the environment, a default value is used.\n",
	    progname, progname, (int)strlen(progname), " ",
	    progname, progname);
}

#ifdef illumos
static void
bigheap(void)
{
	size_t big, *size;
	int sizes;
	struct memcntl_mha mha;

	/*
	 * First, get the available pagesizes.
	 */
	if ((sizes = getpagesizes(NULL, 0)) == -1)
		return;

	if (sizes == 1 || (size = alloca(sizeof (size_t) * sizes)) == NULL)
		return;

	if (getpagesizes(size, sizes) == -1)
		return;

	while (size[sizes - 1] > maxpgsize)
		sizes--;

	/* set big to the largest allowed page size */
	big = size[sizes - 1];
	if (big & (big - 1)) {
		/*
		 * The largest page size is not a power of two for some
		 * inexplicable reason; return.
		 */
		return;
	}

	/*
	 * Now, align our break to the largest page size.
	 */
	if (brk((void *)((((uintptr_t)sbrk(0) - 1) & ~(big - 1)) + big)) != 0)
		return;

	/*
	 * set the preferred page size for the heap
	 */
	mha.mha_cmd = MHA_MAPSIZE_BSSBRK;
	mha.mha_flags = 0;
	mha.mha_pagesize = big;

	(void) memcntl(NULL, 0, MC_HAT_ADVISE, (caddr_t)&mha, 0, 0);
}
#endif	/* illumos */

static void
finalize_phase_one(workqueue_t *wq)
{
	int startslot, i;

	/*
	 * wip slots are cleared out only when maxbatchsz td's have been merged
	 * into them.  We're not guaranteed that the number of files we're
	 * merging is a multiple of maxbatchsz, so there will be some partial
	 * groups in the wip array.  Move them to the done queue in batch ID
	 * order, starting with the slot containing the next batch that would
	 * have been placed on the done queue, followed by the others.
	 * One thread will be doing this while the others wait at the barrier
	 * back in worker_thread(), so we don't need to worry about pesky things
	 * like locks.
	 */

	for (startslot = -1, i = 0; i < wq->wq_nwipslots; i++) {
		if (wq->wq_wip[i].wip_batchid == wq->wq_lastdonebatch + 1) {
			startslot = i;
			break;
		}
	}

	assert(startslot != -1);

	for (i = startslot; i < startslot + wq->wq_nwipslots; i++) {
		int slotnum = i % wq->wq_nwipslots;
		wip_t *wipslot = &wq->wq_wip[slotnum];

		if (wipslot->wip_td != NULL) {
			debug(2, "clearing slot %d (%d) (saving %d)\n",
			    slotnum, i, wipslot->wip_nmerged);
		} else
			debug(2, "clearing slot %d (%d)\n", slotnum, i);

		if (wipslot->wip_td != NULL) {
			fifo_add(wq->wq_donequeue, wipslot->wip_td);
			wq->wq_wip[slotnum].wip_td = NULL;
		}
	}

	wq->wq_lastdonebatch = wq->wq_next_batchid++;

	debug(2, "phase one done: donequeue has %d items\n",
	    fifo_len(wq->wq_donequeue));
}

static void
init_phase_two(workqueue_t *wq)
{
	int num;

	/*
	 * We're going to continually merge the first two entries on the queue,
	 * placing the result on the end, until there's nothing left to merge.
	 * At that point, everything will have been merged into one.  The
	 * initial value of ninqueue needs to be equal to the total number of
	 * entries that will show up on the queue, both at the start of the
	 * phase and as generated by merges during the phase.
	 */
	wq->wq_ninqueue = num = fifo_len(wq->wq_donequeue);
	while (num != 1) {
		wq->wq_ninqueue += num / 2;
		num = num / 2 + num % 2;
	}

	/*
	 * Move the done queue to the work queue.  We won't be using the done
	 * queue in phase 2.
	 */
	assert(fifo_len(wq->wq_queue) == 0);
	fifo_free(wq->wq_queue, NULL);
	wq->wq_queue = wq->wq_donequeue;
}

static void
wip_save_work(workqueue_t *wq, wip_t *slot, int slotnum)
{
	pthread_mutex_lock(&wq->wq_donequeue_lock);

	while (wq->wq_lastdonebatch + 1 < slot->wip_batchid)
		pthread_cond_wait(&slot->wip_cv, &wq->wq_donequeue_lock);
	assert(wq->wq_lastdonebatch + 1 == slot->wip_batchid);

	fifo_add(wq->wq_donequeue, slot->wip_td);
	wq->wq_lastdonebatch++;
	pthread_cond_signal(&wq->wq_wip[(slotnum + 1) %
	    wq->wq_nwipslots].wip_cv);

	/* reset the slot for next use */
	slot->wip_td = NULL;
	slot->wip_batchid = wq->wq_next_batchid++;

	pthread_mutex_unlock(&wq->wq_donequeue_lock);
}

static void
wip_add_work(wip_t *slot, tdata_t *pow)
{
	if (slot->wip_td == NULL) {
		slot->wip_td = pow;
		slot->wip_nmerged = 1;
	} else {
		debug(2, "%d: merging %p into %p\n", pthread_self(),
		    (void *)pow, (void *)slot->wip_td);

		merge_into_master(pow, slot->wip_td, NULL, 0);
		tdata_free(pow);

		slot->wip_nmerged++;
	}
}

static void
worker_runphase1(workqueue_t *wq)
{
	wip_t *wipslot;
	tdata_t *pow;
	int wipslotnum, pownum;

	for (;;) {
		pthread_mutex_lock(&wq->wq_queue_lock);

		while (fifo_empty(wq->wq_queue)) {
			if (wq->wq_nomorefiles == 1) {
				pthread_cond_broadcast(&wq->wq_work_avail);
				pthread_mutex_unlock(&wq->wq_queue_lock);

				/* on to phase 2 ... */
				return;
			}

			pthread_cond_wait(&wq->wq_work_avail,
			    &wq->wq_queue_lock);
		}

		/* there's work to be done! */
		pow = fifo_remove(wq->wq_queue);
		pownum = wq->wq_nextpownum++;
		pthread_cond_broadcast(&wq->wq_work_removed);

		assert(pow != NULL);

		/* merge it into the right slot */
		wipslotnum = pownum % wq->wq_nwipslots;
		wipslot = &wq->wq_wip[wipslotnum];

		pthread_mutex_lock(&wipslot->wip_lock);

		pthread_mutex_unlock(&wq->wq_queue_lock);

		wip_add_work(wipslot, pow);

		if (wipslot->wip_nmerged == wq->wq_maxbatchsz)
			wip_save_work(wq, wipslot, wipslotnum);

		pthread_mutex_unlock(&wipslot->wip_lock);
	}
}

static void
worker_runphase2(workqueue_t *wq)
{
	tdata_t *pow1, *pow2;
	int batchid;

	for (;;) {
		pthread_mutex_lock(&wq->wq_queue_lock);

		if (wq->wq_ninqueue == 1) {
			pthread_cond_broadcast(&wq->wq_work_avail);
			pthread_mutex_unlock(&wq->wq_queue_lock);

			debug(2, "%d: entering p2 completion barrier\n",
			    pthread_self());
			if (barrier_wait(&wq->wq_bar1)) {
				pthread_mutex_lock(&wq->wq_queue_lock);
				wq->wq_alldone = 1;
				pthread_cond_signal(&wq->wq_alldone_cv);
				pthread_mutex_unlock(&wq->wq_queue_lock);
			}

			return;
		}

		if (fifo_len(wq->wq_queue) < 2) {
			pthread_cond_wait(&wq->wq_work_avail,
			    &wq->wq_queue_lock);
			pthread_mutex_unlock(&wq->wq_queue_lock);
			continue;
		}

		/* there's work to be done! */
		pow1 = fifo_remove(wq->wq_queue);
		pow2 = fifo_remove(wq->wq_queue);
		wq->wq_ninqueue -= 2;

		batchid = wq->wq_next_batchid++;

		pthread_mutex_unlock(&wq->wq_queue_lock);

		debug(2, "%d: merging %p into %p\n", pthread_self(),
		    (void *)pow1, (void *)pow2);
		merge_into_master(pow1, pow2, NULL, 0);
		tdata_free(pow1);

		/*
		 * merging is complete.  place at the tail of the queue in
		 * proper order.
		 */
		pthread_mutex_lock(&wq->wq_queue_lock);
		while (wq->wq_lastdonebatch + 1 != batchid) {
			pthread_cond_wait(&wq->wq_done_cv,
			    &wq->wq_queue_lock);
		}

		wq->wq_lastdonebatch = batchid;

		fifo_add(wq->wq_queue, pow2);
		debug(2, "%d: added %p to queue, len now %d, ninqueue %d\n",
		    pthread_self(), (void *)pow2, fifo_len(wq->wq_queue),
		    wq->wq_ninqueue);
		pthread_cond_broadcast(&wq->wq_done_cv);
		pthread_cond_signal(&wq->wq_work_avail);
		pthread_mutex_unlock(&wq->wq_queue_lock);
	}
}

/*
 * Main loop for worker threads.
 */
static void
worker_thread(workqueue_t *wq)
{
	worker_runphase1(wq);

	debug(2, "%d: entering first barrier\n", pthread_self());

	if (barrier_wait(&wq->wq_bar1)) {

		debug(2, "%d: doing work in first barrier\n", pthread_self());

		finalize_phase_one(wq);

		init_phase_two(wq);

		debug(2, "%d: ninqueue is %d, %d on queue\n", pthread_self(),
		    wq->wq_ninqueue, fifo_len(wq->wq_queue));
	}

	debug(2, "%d: entering second barrier\n", pthread_self());

	(void) barrier_wait(&wq->wq_bar2);

	debug(2, "%d: phase 1 complete\n", pthread_self());

	worker_runphase2(wq);
}

/*
 * Pass a tdata_t tree, built from an input file, off to the work queue for
 * consumption by worker threads.
 */
static int
merge_ctf_cb(tdata_t *td, char *name, void *arg)
{
	workqueue_t *wq = arg;

	debug(3, "Adding tdata %p for processing\n", (void *)td);

	pthread_mutex_lock(&wq->wq_queue_lock);
	while (fifo_len(wq->wq_queue) > wq->wq_ithrottle) {
		debug(2, "Throttling input (len = %d, throttle = %d)\n",
		    fifo_len(wq->wq_queue), wq->wq_ithrottle);
		pthread_cond_wait(&wq->wq_work_removed, &wq->wq_queue_lock);
	}

	fifo_add(wq->wq_queue, td);
	debug(1, "Thread %d announcing %s\n", pthread_self(), name);
	pthread_cond_broadcast(&wq->wq_work_avail);
	pthread_mutex_unlock(&wq->wq_queue_lock);

	return (1);
}

/*
 * This program is intended to be invoked from a Makefile, as part of the build.
 * As such, in the event of a failure or user-initiated interrupt (^C), we need
 * to ensure that a subsequent re-make will cause ctfmerge to be executed again.
 * Unfortunately, ctfmerge will usually be invoked directly after (and as part
 * of the same Makefile rule as) a link, and will operate on the linked file
 * in place.  If we merely exit upon receipt of a SIGINT, a subsequent make
 * will notice that the *linked* file is newer than the object files, and thus
 * will not reinvoke ctfmerge.  The only way to ensure that a subsequent make
 * reinvokes ctfmerge, is to remove the file to which we are adding CTF
 * data (confusingly named the output file).  This means that the link will need
 * to happen again, but links are generally fast, and we can't allow the merge
 * to be skipped.
 *
 * Another possibility would be to block SIGINT entirely - to always run to
 * completion.  The run time of ctfmerge can, however, be measured in minutes
 * in some cases, so this is not a valid option.
 */
static void
handle_sig(int sig)
{
	terminate("Caught signal %d - exiting\n", sig);
}

static void
terminate_cleanup(void)
{
	int dounlink = getenv("CTFMERGE_TERMINATE_NO_UNLINK") ? 0 : 1;

	if (tmpname != NULL && dounlink)
		unlink(tmpname);

	if (outfile == NULL)
		return;

#if !defined(__FreeBSD__)
	if (dounlink) {
		fprintf(stderr, "Removing %s\n", outfile);
		unlink(outfile);
	}
#endif
}

static void
copy_ctf_data(char *srcfile, char *destfile, int keep_stabs)
{
	tdata_t *srctd;

	if (read_ctf(&srcfile, 1, NULL, read_ctf_save_cb, &srctd, 1) == 0)
		terminate("No CTF data found in source file %s\n", srcfile);

	tmpname = mktmpname(destfile, ".ctf");
	write_ctf(srctd, destfile, tmpname, CTF_COMPRESS | CTF_SWAP_BYTES | keep_stabs);
	if (rename(tmpname, destfile) != 0) {
		terminate("Couldn't rename temp file %s to %s", tmpname,
		    destfile);
	}
	free(tmpname);
	tdata_free(srctd);
}

static void
wq_init(workqueue_t *wq, int nfiles)
{
	int throttle, nslots, i;

	if (getenv("CTFMERGE_MAX_SLOTS"))
		nslots = atoi(getenv("CTFMERGE_MAX_SLOTS"));
	else
		nslots = MERGE_PHASE1_MAX_SLOTS;

	if (getenv("CTFMERGE_PHASE1_BATCH_SIZE"))
		wq->wq_maxbatchsz = atoi(getenv("CTFMERGE_PHASE1_BATCH_SIZE"));
	else
		wq->wq_maxbatchsz = MERGE_PHASE1_BATCH_SIZE;

	nslots = MIN(nslots, (nfiles + wq->wq_maxbatchsz - 1) /
	    wq->wq_maxbatchsz);

	wq->wq_wip = xcalloc(sizeof (wip_t) * nslots);
	wq->wq_nwipslots = nslots;
	wq->wq_nthreads = MIN(sysconf(_SC_NPROCESSORS_ONLN) * 3 / 2, nslots);
	wq->wq_thread = xmalloc(sizeof (pthread_t) * wq->wq_nthreads);

	if (getenv("CTFMERGE_INPUT_THROTTLE"))
		throttle = atoi(getenv("CTFMERGE_INPUT_THROTTLE"));
	else
		throttle = MERGE_INPUT_THROTTLE_LEN;
	wq->wq_ithrottle = throttle * wq->wq_nthreads;

	debug(1, "Using %d slots, %d threads\n", wq->wq_nwipslots,
	    wq->wq_nthreads);

	wq->wq_next_batchid = 0;

	for (i = 0; i < nslots; i++) {
		pthread_mutex_init(&wq->wq_wip[i].wip_lock, NULL);
		wq->wq_wip[i].wip_batchid = wq->wq_next_batchid++;
	}

	pthread_mutex_init(&wq->wq_queue_lock, NULL);
	wq->wq_queue = fifo_new();
	pthread_cond_init(&wq->wq_work_avail, NULL);
	pthread_cond_init(&wq->wq_work_removed, NULL);
	wq->wq_ninqueue = nfiles;
	wq->wq_nextpownum = 0;

	pthread_mutex_init(&wq->wq_donequeue_lock, NULL);
	wq->wq_donequeue = fifo_new();
	wq->wq_lastdonebatch = -1;

	pthread_cond_init(&wq->wq_done_cv, NULL);

	pthread_cond_init(&wq->wq_alldone_cv, NULL);
	wq->wq_alldone = 0;

	barrier_init(&wq->wq_bar1, wq->wq_nthreads);
	barrier_init(&wq->wq_bar2, wq->wq_nthreads);

	wq->wq_nomorefiles = 0;
}

static void
start_threads(workqueue_t *wq)
{
	sigset_t sets;
	int i;

	sigemptyset(&sets);
	sigaddset(&sets, SIGINT);
	sigaddset(&sets, SIGQUIT);
	sigaddset(&sets, SIGTERM);
	pthread_sigmask(SIG_BLOCK, &sets, NULL);

	for (i = 0; i < wq->wq_nthreads; i++) {
		pthread_create(&wq->wq_thread[i], NULL,
		    (void *(*)(void *))worker_thread, wq);
	}

#ifdef illumos
	sigset(SIGINT, handle_sig);
	sigset(SIGQUIT, handle_sig);
	sigset(SIGTERM, handle_sig);
#else
	signal(SIGINT, handle_sig);
	signal(SIGQUIT, handle_sig);
	signal(SIGTERM, handle_sig);
#endif
	pthread_sigmask(SIG_UNBLOCK, &sets, NULL);
}

static void
join_threads(workqueue_t *wq)
{
	int i;

	for (i = 0; i < wq->wq_nthreads; i++) {
		pthread_join(wq->wq_thread[i], NULL);
	}
}

static int
strcompare(const void *p1, const void *p2)
{
	char *s1 = *((char **)p1);
	char *s2 = *((char **)p2);

	return (strcmp(s1, s2));
}

/*
 * Core work queue structure; passed to worker threads on thread creation
 * as the main point of coordination.  Allocate as a static structure; we
 * could have put this into a local variable in main, but passing a pointer
 * into your stack to another thread is fragile at best and leads to some
 * hard-to-debug failure modes.
 */
static workqueue_t wq;

int
main(int argc, char **argv)
{
	tdata_t *mstrtd, *savetd;
	char *uniqfile = NULL, *uniqlabel = NULL;
	char *withfile = NULL;
	char *label = NULL;
	char **ifiles, **tifiles;
	int verbose = 0, docopy = 0;
	int write_fuzzy_match = 0;
	int keep_stabs = 0;
	int require_ctf = 0;
	int nifiles, nielems;
	int c, i, idx, tidx, err;

	progname = basename(argv[0]);

	if (getenv("CTFMERGE_DEBUG_LEVEL"))
		debug_level = atoi(getenv("CTFMERGE_DEBUG_LEVEL"));

	err = 0;
	while ((c = getopt(argc, argv, ":cd:D:fgl:L:o:tvw:s")) != EOF) {
		switch (c) {
		case 'c':
			docopy = 1;
			break;
		case 'd':
			/* Uniquify against `uniqfile' */
			uniqfile = optarg;
			break;
		case 'D':
			/* Uniquify against label `uniqlabel' in `uniqfile' */
			uniqlabel = optarg;
			break;
		case 'f':
			write_fuzzy_match = CTF_FUZZY_MATCH;
			break;
		case 'g':
			keep_stabs = CTF_KEEP_STABS;
			break;
		case 'l':
			/* Label merged types with `label' */
			label = optarg;
			break;
		case 'L':
			/* Label merged types with getenv(`label`) */
			if ((label = getenv(optarg)) == NULL)
				label = CTF_DEFAULT_LABEL;
			break;
		case 'o':
			/* Place merged types in CTF section in `outfile' */
			outfile = optarg;
			break;
		case 't':
			/* Insist *all* object files built from C have CTF */
			require_ctf = 1;
			break;
		case 'v':
			/* More debugging information */
			verbose = 1;
			break;
		case 'w':
			/* Additive merge with data from `withfile' */
			withfile = optarg;
			break;
		case 's':
			/* use the dynsym rather than the symtab */
			dynsym = CTF_USE_DYNSYM;
			break;
		default:
			usage();
			exit(2);
		}
	}

	/* Validate arguments */
	if (docopy) {
		if (uniqfile != NULL || uniqlabel != NULL || label != NULL ||
		    outfile != NULL || withfile != NULL || dynsym != 0)
			err++;

		if (argc - optind != 2)
			err++;
	} else {
		if (uniqfile != NULL && withfile != NULL)
			err++;

		if (uniqlabel != NULL && uniqfile == NULL)
			err++;

		if (outfile == NULL || label == NULL)
			err++;

		if (argc - optind == 0)
			err++;
	}

	if (err) {
		usage();
		exit(2);
	}

	if (getenv("STRIPSTABS_KEEP_STABS") != NULL)
		keep_stabs = CTF_KEEP_STABS;

	if (uniqfile && access(uniqfile, R_OK) != 0) {
		warning("Uniquification file %s couldn't be opened and "
		    "will be ignored.\n", uniqfile);
		uniqfile = NULL;
	}
	if (withfile && access(withfile, R_OK) != 0) {
		warning("With file %s couldn't be opened and will be "
		    "ignored.\n", withfile);
		withfile = NULL;
	}
	if (outfile && access(outfile, R_OK|W_OK) != 0)
		terminate("Cannot open output file %s for r/w", outfile);

	/*
	 * This is ugly, but we don't want to have to have a separate tool
	 * (yet) just for copying an ELF section with our specific requirements,
	 * so we shoe-horn a copier into ctfmerge.
	 */
	if (docopy) {
		copy_ctf_data(argv[optind], argv[optind + 1], keep_stabs);

		exit(0);
	}

	set_terminate_cleanup(terminate_cleanup);

	/* Sort the input files and strip out duplicates */
	nifiles = argc - optind;
	ifiles = xmalloc(sizeof (char *) * nifiles);
	tifiles = xmalloc(sizeof (char *) * nifiles);

	for (i = 0; i < nifiles; i++)
		tifiles[i] = argv[optind + i];
	qsort(tifiles, nifiles, sizeof (char *), (int (*)())strcompare);

	ifiles[0] = tifiles[0];
	for (idx = 0, tidx = 1; tidx < nifiles; tidx++) {
		if (strcmp(ifiles[idx], tifiles[tidx]) != 0)
			ifiles[++idx] = tifiles[tidx];
	}
	nifiles = idx + 1;

	/* Make sure they all exist */
	if ((nielems = count_files(ifiles, nifiles)) < 0)
		terminate("Some input files were inaccessible\n");

	/* Prepare for the merge */
	wq_init(&wq, nielems);

	start_threads(&wq);

	/*
	 * Start the merge
	 *
	 * We're reading everything from each of the object files, so we
	 * don't need to specify labels.
	 */
	if (read_ctf(ifiles, nifiles, NULL, merge_ctf_cb,
	    &wq, require_ctf) == 0) {
		/*
		 * If we're verifying that C files have CTF, it's safe to
		 * assume that in this case, we're building only from assembly
		 * inputs.
		 */
		if (require_ctf)
			exit(0);
		terminate("No ctf sections found to merge\n");
	}

	pthread_mutex_lock(&wq.wq_queue_lock);
	wq.wq_nomorefiles = 1;
	pthread_cond_broadcast(&wq.wq_work_avail);
	pthread_mutex_unlock(&wq.wq_queue_lock);

	pthread_mutex_lock(&wq.wq_queue_lock);
	while (wq.wq_alldone == 0)
		pthread_cond_wait(&wq.wq_alldone_cv, &wq.wq_queue_lock);
	pthread_mutex_unlock(&wq.wq_queue_lock);

	join_threads(&wq);

	/*
	 * All requested files have been merged, with the resulting tree in
	 * mstrtd.  savetd is the tree that will be placed into the output file.
	 *
	 * Regardless of whether we're doing a normal uniquification or an
	 * additive merge, we need a type tree that has been uniquified
	 * against uniqfile or withfile, as appropriate.
	 *
	 * If we're doing a uniquification, we stuff the resulting tree into
	 * outfile.  Otherwise, we add the tree to the tree already in withfile.
	 */
	assert(fifo_len(wq.wq_queue) == 1);
	mstrtd = fifo_remove(wq.wq_queue);

	if (verbose || debug_level) {
		debug(2, "Statistics for td %p\n", (void *)mstrtd);

		iidesc_stats(mstrtd->td_iihash);
	}

	if (uniqfile != NULL || withfile != NULL) {
		char *reffile, *reflabel = NULL;
		tdata_t *reftd;

		if (uniqfile != NULL) {
			reffile = uniqfile;
			reflabel = uniqlabel;
		} else
			reffile = withfile;

		if (read_ctf(&reffile, 1, reflabel, read_ctf_save_cb,
		    &reftd, require_ctf) == 0) {
			terminate("No CTF data found in reference file %s\n",
			    reffile);
		}

		savetd = tdata_new();

		if (CTF_TYPE_ISCHILD(reftd->td_nextid))
			terminate("No room for additional types in master\n");

		savetd->td_nextid = withfile ? reftd->td_nextid :
		    CTF_INDEX_TO_TYPE(1, TRUE);
		merge_into_master(mstrtd, reftd, savetd, 0);

		tdata_label_add(savetd, label, CTF_LABEL_LASTIDX);

		if (withfile) {
			/*
			 * savetd holds the new data to be added to the withfile
			 */
			tdata_t *withtd = reftd;

			tdata_merge(withtd, savetd);

			savetd = withtd;
		} else {
			char uniqname[MAXPATHLEN];
			labelent_t *parle;

			parle = tdata_label_top(reftd);

			savetd->td_parlabel = xstrdup(parle->le_name);

			strncpy(uniqname, reffile, sizeof (uniqname));
			uniqname[MAXPATHLEN - 1] = '\0';
			savetd->td_parname = xstrdup(basename(uniqname));
		}

	} else {
		/*
		 * No post processing.  Write the merged tree as-is into the
		 * output file.
		 */
		tdata_label_free(mstrtd);
		tdata_label_add(mstrtd, label, CTF_LABEL_LASTIDX);

		savetd = mstrtd;
	}

	tmpname = mktmpname(outfile, ".ctf");
	write_ctf(savetd, outfile, tmpname,
	    CTF_COMPRESS | CTF_SWAP_BYTES | write_fuzzy_match | dynsym | keep_stabs);
	if (rename(tmpname, outfile) != 0)
		terminate("Couldn't rename output temp file %s", tmpname);
	free(tmpname);

	return (0);
}
