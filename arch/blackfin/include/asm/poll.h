#ifndef __BFIN_POLL_H
#define __BFIN_POLL_H

#define POLLIN		  1
#define POLLPRI		  2
#define POLLOUT		  4
#define POLLERR		  8
#define POLLHUP		 16
#define POLLNVAL	 32
#define POLLRDNORM	 64
#define POLLWRNORM	POLLOUT
#define POLLRDBAND	128
#define POLLWRBAND	256
#define POLLMSG		0x0400
#define POLLREMOVE	0x1000
#define POLLRDHUP       0x2000

struct pollfd {
	int fd;
	short events;
	short revents;
};

#endif				/* __BFIN_POLL_H */
