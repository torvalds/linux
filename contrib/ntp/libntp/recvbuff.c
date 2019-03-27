#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdio.h>

#include "ntp_assert.h"
#include "ntp_syslog.h"
#include "ntp_stdlib.h"
#include "ntp_lists.h"
#include "recvbuff.h"
#include "iosignal.h"


/*
 * Memory allocation
 */
static u_long volatile full_recvbufs;	/* recvbufs on full_recv_fifo */
static u_long volatile free_recvbufs;	/* recvbufs on free_recv_list */
static u_long volatile total_recvbufs;	/* total recvbufs currently in use */
static u_long volatile lowater_adds;	/* number of times we have added memory */
static u_long volatile buffer_shortfall;/* number of missed free receive buffers
					   between replenishments */

static DECL_FIFO_ANCHOR(recvbuf_t) full_recv_fifo;
static recvbuf_t *		   free_recv_list;
	
#if defined(SYS_WINNT)

/*
 * For Windows we need to set up a lock to manipulate the
 * recv buffers to prevent corruption. We keep it lock for as
 * short a time as possible
 */
static CRITICAL_SECTION RecvLock;
# define LOCK()		EnterCriticalSection(&RecvLock)
# define UNLOCK()	LeaveCriticalSection(&RecvLock)
#else
# define LOCK()		do {} while (FALSE)
# define UNLOCK()	do {} while (FALSE)
#endif

#ifdef DEBUG
static void uninit_recvbuff(void);
#endif


u_long
free_recvbuffs (void)
{
	return free_recvbufs;
}

u_long
full_recvbuffs (void)
{
	return full_recvbufs;
}

u_long
total_recvbuffs (void)
{
	return total_recvbufs;
}

u_long
lowater_additions(void)
{
	return lowater_adds;
}

static inline void 
initialise_buffer(recvbuf_t *buff)
{
	ZERO(*buff);
}

static void
create_buffers(int nbufs)
{
	register recvbuf_t *bufp;
	int i, abuf;

	abuf = nbufs + buffer_shortfall;
	buffer_shortfall = 0;

#ifndef DEBUG
	bufp = eallocarray(abuf, sizeof(*bufp));
#endif

	for (i = 0; i < abuf; i++) {
#ifdef DEBUG
		/*
		 * Allocate each buffer individually so they can be
		 * free()d during ntpd shutdown on DEBUG builds to
		 * keep them out of heap leak reports.
		 */
		bufp = emalloc_zero(sizeof(*bufp));
#endif
		LINK_SLIST(free_recv_list, bufp, link);
		bufp++;
		free_recvbufs++;
		total_recvbufs++;
	}
	lowater_adds++;
}

void
init_recvbuff(int nbufs)
{

	/*
	 * Init buffer free list and stat counters
	 */
	free_recvbufs = total_recvbufs = 0;
	full_recvbufs = lowater_adds = 0;

	create_buffers(nbufs);

#if defined(SYS_WINNT)
	InitializeCriticalSection(&RecvLock);
#endif

#ifdef DEBUG
	atexit(&uninit_recvbuff);
#endif
}


#ifdef DEBUG
static void
uninit_recvbuff(void)
{
	recvbuf_t *rbunlinked;

	for (;;) {
		UNLINK_FIFO(rbunlinked, full_recv_fifo, link);
		if (rbunlinked == NULL)
			break;
		free(rbunlinked);
	}

	for (;;) {
		UNLINK_HEAD_SLIST(rbunlinked, free_recv_list, link);
		if (rbunlinked == NULL)
			break;
		free(rbunlinked);
	}
}
#endif	/* DEBUG */


/*
 * freerecvbuf - make a single recvbuf available for reuse
 */
void
freerecvbuf(recvbuf_t *rb)
{
	if (rb) {
		LOCK();
		rb->used--;
		if (rb->used != 0)
			msyslog(LOG_ERR, "******** freerecvbuff non-zero usage: %d *******", rb->used);
		LINK_SLIST(free_recv_list, rb, link);
		free_recvbufs++;
		UNLOCK();
	}
}

	
void
add_full_recv_buffer(recvbuf_t *rb)
{
	if (rb == NULL) {
		msyslog(LOG_ERR, "add_full_recv_buffer received NULL buffer");
		return;
	}
	LOCK();
	LINK_FIFO(full_recv_fifo, rb, link);
	full_recvbufs++;
	UNLOCK();
}


recvbuf_t *
get_free_recv_buffer(void)
{
	recvbuf_t *buffer;

	LOCK();
	UNLINK_HEAD_SLIST(buffer, free_recv_list, link);
	if (buffer != NULL) {
		free_recvbufs--;
		initialise_buffer(buffer);
		buffer->used++;
	} else {
		buffer_shortfall++;
	}
	UNLOCK();

	return buffer;
}


#ifdef HAVE_IO_COMPLETION_PORT
recvbuf_t *
get_free_recv_buffer_alloc(void)
{
	recvbuf_t *buffer;
	
	buffer = get_free_recv_buffer();
	if (NULL == buffer) {
		create_buffers(RECV_INC);
		buffer = get_free_recv_buffer();
	}
	ENSURE(buffer != NULL);
	return (buffer);
}
#endif


recvbuf_t *
get_full_recv_buffer(void)
{
	recvbuf_t *	rbuf;

	LOCK();
	
#ifdef HAVE_SIGNALED_IO
	/*
	 * make sure there are free buffers when we
	 * wander off to do lengthy packet processing with
	 * any buffer we grab from the full list.
	 * 
	 * fixes malloc() interrupted by SIGIO risk
	 * (Bug 889)
	 */
	if (NULL == free_recv_list || buffer_shortfall > 0) {
		/*
		 * try to get us some more buffers
		 */
		create_buffers(RECV_INC);
	}
#endif

	/*
	 * try to grab a full buffer
	 */
	UNLINK_FIFO(rbuf, full_recv_fifo, link);
	if (rbuf != NULL)
		full_recvbufs--;
	UNLOCK();

	return rbuf;
}


/*
 * purge_recv_buffers_for_fd() - purges any previously-received input
 *				 from a given file descriptor.
 */
void
purge_recv_buffers_for_fd(
	int	fd
	)
{
	recvbuf_t *rbufp;
	recvbuf_t *next;
	recvbuf_t *punlinked;

	LOCK();

	for (rbufp = HEAD_FIFO(full_recv_fifo);
	     rbufp != NULL;
	     rbufp = next) {
		next = rbufp->link;
#	    ifdef HAVE_IO_COMPLETION_PORT
		if (rbufp->dstadr == NULL && rbufp->fd == fd)
#	    else
		if (rbufp->fd == fd)
#	    endif
		{
			UNLINK_MID_FIFO(punlinked, full_recv_fifo,
					rbufp, link, recvbuf_t);
			INSIST(punlinked == rbufp);
			full_recvbufs--;
			freerecvbuf(rbufp);
		}
	}

	UNLOCK();
}


/*
 * Checks to see if there are buffers to process
 */
isc_boolean_t has_full_recv_buffer(void)
{
	if (HEAD_FIFO(full_recv_fifo) != NULL)
		return (ISC_TRUE);
	else
		return (ISC_FALSE);
}


#ifdef NTP_DEBUG_LISTS_H
void
check_gen_fifo_consistency(void *fifo)
{
	gen_fifo *pf;
	gen_node *pthis;
	gen_node **pptail;

	pf = fifo;
	REQUIRE((NULL == pf->phead && NULL == pf->pptail) ||
		(NULL != pf->phead && NULL != pf->pptail));

	pptail = &pf->phead;
	for (pthis = pf->phead;
	     pthis != NULL;
	     pthis = pthis->link)
		if (NULL != pthis->link)
			pptail = &pthis->link;

	REQUIRE(NULL == pf->pptail || pptail == pf->pptail);
}
#endif	/* NTP_DEBUG_LISTS_H */
