/*
 * Copyright (C) 2004, 2005, 2007, 2009, 2011, 2012  Internet Systems Consortium, Inc. ("ISC")
 * Copyright (C) 1998-2001, 2003  Internet Software Consortium.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

/* $Id$ */

/*! \file */

#include <config.h>

#include <stddef.h>

#include <isc/atomic.h>
#include <isc/magic.h>
#include <isc/msgs.h>
#include <isc/platform.h>
#include <isc/rwlock.h>
#include <isc/util.h>

#define RWLOCK_MAGIC		ISC_MAGIC('R', 'W', 'L', 'k')
#define VALID_RWLOCK(rwl)	ISC_MAGIC_VALID(rwl, RWLOCK_MAGIC)

#ifdef ISC_PLATFORM_USETHREADS

#ifndef RWLOCK_DEFAULT_READ_QUOTA
#define RWLOCK_DEFAULT_READ_QUOTA 4
#endif

#ifndef RWLOCK_DEFAULT_WRITE_QUOTA
#define RWLOCK_DEFAULT_WRITE_QUOTA 4
#endif

#ifdef ISC_RWLOCK_TRACE
#include <stdio.h>		/* Required for fprintf/stderr. */
#include <isc/thread.h>		/* Required for isc_thread_self(). */

static void
print_lock(const char *operation, isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	fprintf(stderr,
		isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
			       ISC_MSG_PRINTLOCK,
			       "rwlock %p thread %lu %s(%s): %s, %u active, "
			       "%u granted, %u rwaiting, %u wwaiting\n"),
		rwl, isc_thread_self(), operation,
		(type == isc_rwlocktype_read ?
		 isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				ISC_MSG_READ, "read") :
		 isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				ISC_MSG_WRITE, "write")),
		(rwl->type == isc_rwlocktype_read ?
		 isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				ISC_MSG_READING, "reading") :
		 isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				ISC_MSG_WRITING, "writing")),
		rwl->active, rwl->granted, rwl->readers_waiting,
		rwl->writers_waiting);
}
#endif

isc_result_t
isc_rwlock_init(isc_rwlock_t *rwl, unsigned int read_quota,
		unsigned int write_quota)
{
	isc_result_t result;

	REQUIRE(rwl != NULL);

	/*
	 * In case there's trouble initializing, we zero magic now.  If all
	 * goes well, we'll set it to RWLOCK_MAGIC.
	 */
	rwl->magic = 0;

#if defined(ISC_PLATFORM_HAVEXADD) && defined(ISC_PLATFORM_HAVECMPXCHG)
	rwl->write_requests = 0;
	rwl->write_completions = 0;
	rwl->cnt_and_flag = 0;
	rwl->readers_waiting = 0;
	rwl->write_granted = 0;
	if (read_quota != 0) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "read quota is not supported");
	}
	if (write_quota == 0)
		write_quota = RWLOCK_DEFAULT_WRITE_QUOTA;
	rwl->write_quota = write_quota;
#else
	rwl->type = isc_rwlocktype_read;
	rwl->original = isc_rwlocktype_none;
	rwl->active = 0;
	rwl->granted = 0;
	rwl->readers_waiting = 0;
	rwl->writers_waiting = 0;
	if (read_quota == 0)
		read_quota = RWLOCK_DEFAULT_READ_QUOTA;
	rwl->read_quota = read_quota;
	if (write_quota == 0)
		write_quota = RWLOCK_DEFAULT_WRITE_QUOTA;
	rwl->write_quota = write_quota;
#endif

	result = isc_mutex_init(&rwl->lock);
	if (result != ISC_R_SUCCESS)
		return (result);

	result = isc_condition_init(&rwl->readable);
	if (result != ISC_R_SUCCESS) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_condition_init(readable) %s: %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"),
				 isc_result_totext(result));
		result = ISC_R_UNEXPECTED;
		goto destroy_lock;
	}
	result = isc_condition_init(&rwl->writeable);
	if (result != ISC_R_SUCCESS) {
		UNEXPECTED_ERROR(__FILE__, __LINE__,
				 "isc_condition_init(writeable) %s: %s",
				 isc_msgcat_get(isc_msgcat, ISC_MSGSET_GENERAL,
						ISC_MSG_FAILED, "failed"),
				 isc_result_totext(result));
		result = ISC_R_UNEXPECTED;
		goto destroy_rcond;
	}

	rwl->magic = RWLOCK_MAGIC;

	return (ISC_R_SUCCESS);

  destroy_rcond:
	(void)isc_condition_destroy(&rwl->readable);
  destroy_lock:
	DESTROYLOCK(&rwl->lock);

	return (result);
}

void
isc_rwlock_destroy(isc_rwlock_t *rwl) {
	REQUIRE(VALID_RWLOCK(rwl));

#if defined(ISC_PLATFORM_HAVEXADD) && defined(ISC_PLATFORM_HAVECMPXCHG)
	REQUIRE(rwl->write_requests == rwl->write_completions &&
		rwl->cnt_and_flag == 0 && rwl->readers_waiting == 0);
#else
	LOCK(&rwl->lock);
	REQUIRE(rwl->active == 0 &&
		rwl->readers_waiting == 0 &&
		rwl->writers_waiting == 0);
	UNLOCK(&rwl->lock);
#endif

	rwl->magic = 0;
	(void)isc_condition_destroy(&rwl->readable);
	(void)isc_condition_destroy(&rwl->writeable);
	DESTROYLOCK(&rwl->lock);
}

#if defined(ISC_PLATFORM_HAVEXADD) && defined(ISC_PLATFORM_HAVECMPXCHG)

/*
 * When some architecture-dependent atomic operations are available,
 * rwlock can be more efficient than the generic algorithm defined below.
 * The basic algorithm is described in the following URL:
 *   http://www.cs.rochester.edu/u/scott/synchronization/pseudocode/rw.html
 *
 * The key is to use the following integer variables modified atomically:
 *   write_requests, write_completions, and cnt_and_flag.
 *
 * write_requests and write_completions act as a waiting queue for writers
 * in order to ensure the FIFO order.  Both variables begin with the initial
 * value of 0.  When a new writer tries to get a write lock, it increments
 * write_requests and gets the previous value of the variable as a "ticket".
 * When write_completions reaches the ticket number, the new writer can start
 * writing.  When the writer completes its work, it increments
 * write_completions so that another new writer can start working.  If the
 * write_requests is not equal to write_completions, it means a writer is now
 * working or waiting.  In this case, a new readers cannot start reading, or
 * in other words, this algorithm basically prefers writers.
 *
 * cnt_and_flag is a "lock" shared by all readers and writers.  This integer
 * variable is a kind of structure with two members: writer_flag (1 bit) and
 * reader_count (31 bits).  The writer_flag shows whether a writer is working,
 * and the reader_count shows the number of readers currently working or almost
 * ready for working.  A writer who has the current "ticket" tries to get the
 * lock by exclusively setting the writer_flag to 1, provided that the whole
 * 32-bit is 0 (meaning no readers or writers working).  On the other hand,
 * a new reader tries to increment the "reader_count" field provided that
 * the writer_flag is 0 (meaning there is no writer working).
 *
 * If some of the above operations fail, the reader or the writer sleeps
 * until the related condition changes.  When a working reader or writer
 * completes its work, some readers or writers are sleeping, and the condition
 * that suspended the reader or writer has changed, it wakes up the sleeping
 * readers or writers.
 *
 * As already noted, this algorithm basically prefers writers.  In order to
 * prevent readers from starving, however, the algorithm also introduces the
 * "writer quota" (Q).  When Q consecutive writers have completed their work,
 * suspending readers, the last writer will wake up the readers, even if a new
 * writer is waiting.
 *
 * Implementation specific note: due to the combination of atomic operations
 * and a mutex lock, ordering between the atomic operation and locks can be
 * very sensitive in some cases.  In particular, it is generally very important
 * to check the atomic variable that requires a reader or writer to sleep after
 * locking the mutex and before actually sleeping; otherwise, it could be very
 * likely to cause a deadlock.  For example, assume "var" is a variable
 * atomically modified, then the corresponding code would be:
 *	if (var == need_sleep) {
 *		LOCK(lock);
 *		if (var == need_sleep)
 *			WAIT(cond, lock);
 *		UNLOCK(lock);
 *	}
 * The second check is important, since "var" is protected by the atomic
 * operation, not by the mutex, and can be changed just before sleeping.
 * (The first "if" could be omitted, but this is also important in order to
 * make the code efficient by avoiding the use of the mutex unless it is
 * really necessary.)
 */

#define WRITER_ACTIVE	0x1
#define READER_INCR	0x2

isc_result_t
isc_rwlock_lock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	isc_int32_t cntflag;

	REQUIRE(VALID_RWLOCK(rwl));

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_PRELOCK, "prelock"), rwl, type);
#endif

	if (type == isc_rwlocktype_read) {
		if (rwl->write_requests != rwl->write_completions) {
			/* there is a waiting or active writer */
			LOCK(&rwl->lock);
			if (rwl->write_requests != rwl->write_completions) {
				rwl->readers_waiting++;
				WAIT(&rwl->readable, &rwl->lock);
				rwl->readers_waiting--;
			}
			UNLOCK(&rwl->lock);
		}

		cntflag = isc_atomic_xadd(&rwl->cnt_and_flag, READER_INCR);
		POST(cntflag);
		while (1) {
			if ((rwl->cnt_and_flag & WRITER_ACTIVE) == 0)
				break;

			/* A writer is still working */
			LOCK(&rwl->lock);
			rwl->readers_waiting++;
			if ((rwl->cnt_and_flag & WRITER_ACTIVE) != 0)
				WAIT(&rwl->readable, &rwl->lock);
			rwl->readers_waiting--;
			UNLOCK(&rwl->lock);

			/*
			 * Typically, the reader should be able to get a lock
			 * at this stage:
			 *   (1) there should have been no pending writer when
			 *       the reader was trying to increment the
			 *       counter; otherwise, the writer should be in
			 *       the waiting queue, preventing the reader from
			 *       proceeding to this point.
			 *   (2) once the reader increments the counter, no
			 *       more writer can get a lock.
			 * Still, it is possible another writer can work at
			 * this point, e.g. in the following scenario:
			 *   A previous writer unlocks the writer lock.
			 *   This reader proceeds to point (1).
			 *   A new writer appears, and gets a new lock before
			 *   the reader increments the counter.
			 *   The reader then increments the counter.
			 *   The previous writer notices there is a waiting
			 *   reader who is almost ready, and wakes it up.
			 * So, the reader needs to confirm whether it can now
			 * read explicitly (thus we loop).  Note that this is
			 * not an infinite process, since the reader has
			 * incremented the counter at this point.
			 */
		}

		/*
		 * If we are temporarily preferred to writers due to the writer
		 * quota, reset the condition (race among readers doesn't
		 * matter).
		 */
		rwl->write_granted = 0;
	} else {
		isc_int32_t prev_writer;

		/* enter the waiting queue, and wait for our turn */
		prev_writer = isc_atomic_xadd(&rwl->write_requests, 1);
		while (rwl->write_completions != prev_writer) {
			LOCK(&rwl->lock);
			if (rwl->write_completions != prev_writer) {
				WAIT(&rwl->writeable, &rwl->lock);
				UNLOCK(&rwl->lock);
				continue;
			}
			UNLOCK(&rwl->lock);
			break;
		}

		while (1) {
			cntflag = isc_atomic_cmpxchg(&rwl->cnt_and_flag, 0,
						     WRITER_ACTIVE);
			if (cntflag == 0)
				break;

			/* Another active reader or writer is working. */
			LOCK(&rwl->lock);
			if (rwl->cnt_and_flag != 0)
				WAIT(&rwl->writeable, &rwl->lock);
			UNLOCK(&rwl->lock);
		}

		INSIST((rwl->cnt_and_flag & WRITER_ACTIVE) != 0);
		rwl->write_granted++;
	}

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_POSTLOCK, "postlock"), rwl, type);
#endif

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_rwlock_trylock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	isc_int32_t cntflag;

	REQUIRE(VALID_RWLOCK(rwl));

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_PRELOCK, "prelock"), rwl, type);
#endif

	if (type == isc_rwlocktype_read) {
		/* If a writer is waiting or working, we fail. */
		if (rwl->write_requests != rwl->write_completions)
			return (ISC_R_LOCKBUSY);

		/* Otherwise, be ready for reading. */
		cntflag = isc_atomic_xadd(&rwl->cnt_and_flag, READER_INCR);
		if ((cntflag & WRITER_ACTIVE) != 0) {
			/*
			 * A writer is working.  We lose, and cancel the read
			 * request.
			 */
			cntflag = isc_atomic_xadd(&rwl->cnt_and_flag,
						  -READER_INCR);
			/*
			 * If no other readers are waiting and we've suspended
			 * new writers in this short period, wake them up.
			 */
			if (cntflag == READER_INCR &&
			    rwl->write_completions != rwl->write_requests) {
				LOCK(&rwl->lock);
				BROADCAST(&rwl->writeable);
				UNLOCK(&rwl->lock);
			}

			return (ISC_R_LOCKBUSY);
		}
	} else {
		/* Try locking without entering the waiting queue. */
		cntflag = isc_atomic_cmpxchg(&rwl->cnt_and_flag, 0,
					     WRITER_ACTIVE);
		if (cntflag != 0)
			return (ISC_R_LOCKBUSY);

		/*
		 * XXXJT: jump into the queue, possibly breaking the writer
		 * order.
		 */
		(void)isc_atomic_xadd(&rwl->write_completions, -1);

		rwl->write_granted++;
	}

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_POSTLOCK, "postlock"), rwl, type);
#endif

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_rwlock_tryupgrade(isc_rwlock_t *rwl) {
	isc_int32_t prevcnt;

	REQUIRE(VALID_RWLOCK(rwl));

	/* Try to acquire write access. */
	prevcnt = isc_atomic_cmpxchg(&rwl->cnt_and_flag,
				     READER_INCR, WRITER_ACTIVE);
	/*
	 * There must have been no writer, and there must have been at least
	 * one reader.
	 */
	INSIST((prevcnt & WRITER_ACTIVE) == 0 &&
	       (prevcnt & ~WRITER_ACTIVE) != 0);

	if (prevcnt == READER_INCR) {
		/*
		 * We are the only reader and have been upgraded.
		 * Now jump into the head of the writer waiting queue.
		 */
		(void)isc_atomic_xadd(&rwl->write_completions, -1);
	} else
		return (ISC_R_LOCKBUSY);

	return (ISC_R_SUCCESS);

}

void
isc_rwlock_downgrade(isc_rwlock_t *rwl) {
	isc_int32_t prev_readers;

	REQUIRE(VALID_RWLOCK(rwl));

	/* Become an active reader. */
	prev_readers = isc_atomic_xadd(&rwl->cnt_and_flag, READER_INCR);
	/* We must have been a writer. */
	INSIST((prev_readers & WRITER_ACTIVE) != 0);

	/* Complete write */
	(void)isc_atomic_xadd(&rwl->cnt_and_flag, -WRITER_ACTIVE);
	(void)isc_atomic_xadd(&rwl->write_completions, 1);

	/* Resume other readers */
	LOCK(&rwl->lock);
	if (rwl->readers_waiting > 0)
		BROADCAST(&rwl->readable);
	UNLOCK(&rwl->lock);
}

isc_result_t
isc_rwlock_unlock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	isc_int32_t prev_cnt;

	REQUIRE(VALID_RWLOCK(rwl));

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_PREUNLOCK, "preunlock"), rwl, type);
#endif

	if (type == isc_rwlocktype_read) {
		prev_cnt = isc_atomic_xadd(&rwl->cnt_and_flag, -READER_INCR);

		/*
		 * If we're the last reader and any writers are waiting, wake
		 * them up.  We need to wake up all of them to ensure the
		 * FIFO order.
		 */
		if (prev_cnt == READER_INCR &&
		    rwl->write_completions != rwl->write_requests) {
			LOCK(&rwl->lock);
			BROADCAST(&rwl->writeable);
			UNLOCK(&rwl->lock);
		}
	} else {
		isc_boolean_t wakeup_writers = ISC_TRUE;

		/*
		 * Reset the flag, and (implicitly) tell other writers
		 * we are done.
		 */
		(void)isc_atomic_xadd(&rwl->cnt_and_flag, -WRITER_ACTIVE);
		(void)isc_atomic_xadd(&rwl->write_completions, 1);

		if (rwl->write_granted >= rwl->write_quota ||
		    rwl->write_requests == rwl->write_completions ||
		    (rwl->cnt_and_flag & ~WRITER_ACTIVE) != 0) {
			/*
			 * We have passed the write quota, no writer is
			 * waiting, or some readers are almost ready, pending
			 * possible writers.  Note that the last case can
			 * happen even if write_requests != write_completions
			 * (which means a new writer in the queue), so we need
			 * to catch the case explicitly.
			 */
			LOCK(&rwl->lock);
			if (rwl->readers_waiting > 0) {
				wakeup_writers = ISC_FALSE;
				BROADCAST(&rwl->readable);
			}
			UNLOCK(&rwl->lock);
		}

		if (rwl->write_requests != rwl->write_completions &&
		    wakeup_writers) {
			LOCK(&rwl->lock);
			BROADCAST(&rwl->writeable);
			UNLOCK(&rwl->lock);
		}
	}

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_POSTUNLOCK, "postunlock"),
		   rwl, type);
#endif

	return (ISC_R_SUCCESS);
}

#else /* ISC_PLATFORM_HAVEXADD && ISC_PLATFORM_HAVECMPXCHG */

static isc_result_t
doit(isc_rwlock_t *rwl, isc_rwlocktype_t type, isc_boolean_t nonblock) {
	isc_boolean_t skip = ISC_FALSE;
	isc_boolean_t done = ISC_FALSE;
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(VALID_RWLOCK(rwl));

	LOCK(&rwl->lock);

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_PRELOCK, "prelock"), rwl, type);
#endif

	if (type == isc_rwlocktype_read) {
		if (rwl->readers_waiting != 0)
			skip = ISC_TRUE;
		while (!done) {
			if (!skip &&
			    ((rwl->active == 0 ||
			      (rwl->type == isc_rwlocktype_read &&
			       (rwl->writers_waiting == 0 ||
				rwl->granted < rwl->read_quota)))))
			{
				rwl->type = isc_rwlocktype_read;
				rwl->active++;
				rwl->granted++;
				done = ISC_TRUE;
			} else if (nonblock) {
				result = ISC_R_LOCKBUSY;
				done = ISC_TRUE;
			} else {
				skip = ISC_FALSE;
				rwl->readers_waiting++;
				WAIT(&rwl->readable, &rwl->lock);
				rwl->readers_waiting--;
			}
		}
	} else {
		if (rwl->writers_waiting != 0)
			skip = ISC_TRUE;
		while (!done) {
			if (!skip && rwl->active == 0) {
				rwl->type = isc_rwlocktype_write;
				rwl->active = 1;
				rwl->granted++;
				done = ISC_TRUE;
			} else if (nonblock) {
				result = ISC_R_LOCKBUSY;
				done = ISC_TRUE;
			} else {
				skip = ISC_FALSE;
				rwl->writers_waiting++;
				WAIT(&rwl->writeable, &rwl->lock);
				rwl->writers_waiting--;
			}
		}
	}

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_POSTLOCK, "postlock"), rwl, type);
#endif

	UNLOCK(&rwl->lock);

	return (result);
}

isc_result_t
isc_rwlock_lock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	return (doit(rwl, type, ISC_FALSE));
}

isc_result_t
isc_rwlock_trylock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	return (doit(rwl, type, ISC_TRUE));
}

isc_result_t
isc_rwlock_tryupgrade(isc_rwlock_t *rwl) {
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(VALID_RWLOCK(rwl));
	LOCK(&rwl->lock);
	REQUIRE(rwl->type == isc_rwlocktype_read);
	REQUIRE(rwl->active != 0);

	/* If we are the only reader then succeed. */
	if (rwl->active == 1) {
		rwl->original = (rwl->original == isc_rwlocktype_none) ?
				isc_rwlocktype_read : isc_rwlocktype_none;
		rwl->type = isc_rwlocktype_write;
	} else
		result = ISC_R_LOCKBUSY;

	UNLOCK(&rwl->lock);
	return (result);
}

void
isc_rwlock_downgrade(isc_rwlock_t *rwl) {

	REQUIRE(VALID_RWLOCK(rwl));
	LOCK(&rwl->lock);
	REQUIRE(rwl->type == isc_rwlocktype_write);
	REQUIRE(rwl->active == 1);

	rwl->type = isc_rwlocktype_read;
	rwl->original = (rwl->original == isc_rwlocktype_none) ?
			isc_rwlocktype_write : isc_rwlocktype_none;
	/*
	 * Resume processing any read request that were blocked when
	 * we upgraded.
	 */
	if (rwl->original == isc_rwlocktype_none &&
	    (rwl->writers_waiting == 0 || rwl->granted < rwl->read_quota) &&
	    rwl->readers_waiting > 0)
		BROADCAST(&rwl->readable);

	UNLOCK(&rwl->lock);
}

isc_result_t
isc_rwlock_unlock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {

	REQUIRE(VALID_RWLOCK(rwl));
	LOCK(&rwl->lock);
	REQUIRE(rwl->type == type);

	UNUSED(type);

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_PREUNLOCK, "preunlock"), rwl, type);
#endif

	INSIST(rwl->active > 0);
	rwl->active--;
	if (rwl->active == 0) {
		if (rwl->original != isc_rwlocktype_none) {
			rwl->type = rwl->original;
			rwl->original = isc_rwlocktype_none;
		}
		if (rwl->type == isc_rwlocktype_read) {
			rwl->granted = 0;
			if (rwl->writers_waiting > 0) {
				rwl->type = isc_rwlocktype_write;
				SIGNAL(&rwl->writeable);
			} else if (rwl->readers_waiting > 0) {
				/* Does this case ever happen? */
				BROADCAST(&rwl->readable);
			}
		} else {
			if (rwl->readers_waiting > 0) {
				if (rwl->writers_waiting > 0 &&
				    rwl->granted < rwl->write_quota) {
					SIGNAL(&rwl->writeable);
				} else {
					rwl->granted = 0;
					rwl->type = isc_rwlocktype_read;
					BROADCAST(&rwl->readable);
				}
			} else if (rwl->writers_waiting > 0) {
				rwl->granted = 0;
				SIGNAL(&rwl->writeable);
			} else {
				rwl->granted = 0;
			}
		}
	}
	INSIST(rwl->original == isc_rwlocktype_none);

#ifdef ISC_RWLOCK_TRACE
	print_lock(isc_msgcat_get(isc_msgcat, ISC_MSGSET_RWLOCK,
				  ISC_MSG_POSTUNLOCK, "postunlock"),
		   rwl, type);
#endif

	UNLOCK(&rwl->lock);

	return (ISC_R_SUCCESS);
}

#endif /* ISC_PLATFORM_HAVEXADD && ISC_PLATFORM_HAVECMPXCHG */
#else /* ISC_PLATFORM_USETHREADS */

isc_result_t
isc_rwlock_init(isc_rwlock_t *rwl, unsigned int read_quota,
		unsigned int write_quota)
{
	REQUIRE(rwl != NULL);

	UNUSED(read_quota);
	UNUSED(write_quota);

	rwl->type = isc_rwlocktype_read;
	rwl->active = 0;
	rwl->magic = RWLOCK_MAGIC;

	return (ISC_R_SUCCESS);
}

isc_result_t
isc_rwlock_lock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	REQUIRE(VALID_RWLOCK(rwl));

	if (type == isc_rwlocktype_read) {
		if (rwl->type != isc_rwlocktype_read && rwl->active != 0)
			return (ISC_R_LOCKBUSY);
		rwl->type = isc_rwlocktype_read;
		rwl->active++;
	} else {
		if (rwl->active != 0)
			return (ISC_R_LOCKBUSY);
		rwl->type = isc_rwlocktype_write;
		rwl->active = 1;
	}
	return (ISC_R_SUCCESS);
}

isc_result_t
isc_rwlock_trylock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	return (isc_rwlock_lock(rwl, type));
}

isc_result_t
isc_rwlock_tryupgrade(isc_rwlock_t *rwl) {
	isc_result_t result = ISC_R_SUCCESS;

	REQUIRE(VALID_RWLOCK(rwl));
	REQUIRE(rwl->type == isc_rwlocktype_read);
	REQUIRE(rwl->active != 0);

	/* If we are the only reader then succeed. */
	if (rwl->active == 1)
		rwl->type = isc_rwlocktype_write;
	else
		result = ISC_R_LOCKBUSY;
	return (result);
}

void
isc_rwlock_downgrade(isc_rwlock_t *rwl) {

	REQUIRE(VALID_RWLOCK(rwl));
	REQUIRE(rwl->type == isc_rwlocktype_write);
	REQUIRE(rwl->active == 1);

	rwl->type = isc_rwlocktype_read;
}

isc_result_t
isc_rwlock_unlock(isc_rwlock_t *rwl, isc_rwlocktype_t type) {
	REQUIRE(VALID_RWLOCK(rwl));
	REQUIRE(rwl->type == type);

	UNUSED(type);

	INSIST(rwl->active > 0);
	rwl->active--;

	return (ISC_R_SUCCESS);
}

void
isc_rwlock_destroy(isc_rwlock_t *rwl) {
	REQUIRE(rwl != NULL);
	REQUIRE(rwl->active == 0);
	rwl->magic = 0;
}

#endif /* ISC_PLATFORM_USETHREADS */
