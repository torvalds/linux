#ifndef RECVBUFF_H
#define RECVBUFF_H

#include "ntp.h"
#include "ntp_net.h"
#include "ntp_lists.h"

#include <isc/result.h>

/*
 * recvbuf memory management
 */
#define RECV_INIT	10	/* 10 buffers initially */
#define RECV_LOWAT	3	/* when we're down to three buffers get more */
#define RECV_INC	5	/* get 5 more at a time */
#define RECV_TOOMANY	40	/* this is way too many buffers */

#if defined HAVE_IO_COMPLETION_PORT
# include "ntp_iocompletionport.h"
# include "ntp_timer.h"

# define RECV_BLOCK_IO()	EnterCriticalSection(&RecvCritSection)
# define RECV_UNBLOCK_IO()	LeaveCriticalSection(&RecvCritSection)

/*  Return the event which is set when items are added to the full list
 */
extern HANDLE	get_recv_buff_event(void);
#else
# define RECV_BLOCK_IO()	
# define RECV_UNBLOCK_IO()	
#endif


/*
 * Format of a recvbuf.  These are used by the asynchronous receive
 * routine to store incoming packets and related information.
 */

/*
 *  the maximum length NTP packet contains the NTP header, one Autokey
 *  request, one Autokey response and the MAC. Assuming certificates don't
 *  get too big, the maximum packet length is set arbitrarily at 1200.
 *  (was 1000, but that bumps on 2048 RSA keys)
 */   
#define	RX_BUFF_SIZE	1200		/* hail Mary */


typedef struct recvbuf recvbuf_t;

struct recvbuf {
	recvbuf_t *	link;	/* next in list */
	union {
		sockaddr_u	X_recv_srcadr;
		caddr_t		X_recv_srcclock;
		struct peer *	X_recv_peer;
	} X_from_where;
#define recv_srcadr		X_from_where.X_recv_srcadr
#define	recv_srcclock		X_from_where.X_recv_srcclock
#define recv_peer		X_from_where.X_recv_peer
#ifndef HAVE_IO_COMPLETION_PORT
	sockaddr_u	srcadr;		/* where packet came from */
#else
	int		recv_srcadr_len;/* filled in on completion */
#endif
	endpt *		dstadr;		/* address pkt arrived on */
	SOCKET		fd;		/* fd on which it was received */
	int		msg_flags;	/* Flags received about the packet */
	l_fp		recv_time;	/* time of arrival */
	void		(*receiver)(struct recvbuf *); /* callback */
	int		recv_length;	/* number of octets received */
	union {
		struct pkt	X_recv_pkt;
		u_char		X_recv_buffer[RX_BUFF_SIZE];
	} recv_space;
#define	recv_pkt		recv_space.X_recv_pkt
#define	recv_buffer		recv_space.X_recv_buffer
	int used;		/* reference count */
};

extern	void	init_recvbuff(int);

/* freerecvbuf - make a single recvbuf available for reuse
 */
extern	void	freerecvbuf(struct recvbuf *);

/*  Get a free buffer (typically used so an async
 *  read can directly place data into the buffer
 *
 *  The buffer is removed from the free list. Make sure
 *  you put it back with freerecvbuf() or 
 */

/* signal safe - no malloc */
extern	struct recvbuf *get_free_recv_buffer(void);
/* signal unsafe - may malloc, never returs NULL */
extern	struct recvbuf *get_free_recv_buffer_alloc(void);

/*   Add a buffer to the full list
 */
extern	void	add_full_recv_buffer(struct recvbuf *);

/* number of recvbufs on freelist */
extern u_long free_recvbuffs(void);		
extern u_long full_recvbuffs(void);		
extern u_long total_recvbuffs(void);
extern u_long lowater_additions(void);
		
/*  Returns the next buffer in the full list.
 *
 */
extern	struct recvbuf *get_full_recv_buffer(void);

/*
 * purge_recv_buffers_for_fd() - purges any previously-received input
 *				 from a given file descriptor.
 */
extern	void purge_recv_buffers_for_fd(int);

/*
 * Checks to see if there are buffers to process
 */
extern isc_boolean_t has_full_recv_buffer(void);

#endif	/* RECVBUFF_H */
