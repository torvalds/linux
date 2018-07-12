/*
 * Copyright (C) 2017 - Cambridge Greys Ltd
 * Copyright (C) 2011 - 2014 Cisco Systems Inc
 * Copyright (C) 2000 - 2007 Jeff Dike (jdike@{addtoit,linux.intel}.com)
 * Licensed under the GPL
 */

#include <stdlib.h>
#include <errno.h>
#include <sys/epoll.h>
#include <signal.h>
#include <string.h>
#include <irq_user.h>
#include <os.h>
#include <um_malloc.h>

/* Epoll support */

static int epollfd = -1;

#define MAX_EPOLL_EVENTS 64

static struct epoll_event epoll_events[MAX_EPOLL_EVENTS];

/* Helper to return an Epoll data pointer from an epoll event structure.
 * We need to keep this one on the userspace side to keep includes separate
 */

void *os_epoll_get_data_pointer(int index)
{
	return epoll_events[index].data.ptr;
}

/* Helper to compare events versus the events in the epoll structure.
 * Same as above - needs to be on the userspace side
 */


int os_epoll_triggered(int index, int events)
{
	return epoll_events[index].events & events;
}
/* Helper to set the event mask.
 * The event mask is opaque to the kernel side, because it does not have
 * access to the right includes/defines for EPOLL constants.
 */

int os_event_mask(int irq_type)
{
	if (irq_type == IRQ_READ)
		return EPOLLIN | EPOLLPRI;
	if (irq_type == IRQ_WRITE)
		return EPOLLOUT;
	return 0;
}

/*
 * Initial Epoll Setup
 */
int os_setup_epoll(void)
{
	epollfd = epoll_create(MAX_EPOLL_EVENTS);
	return epollfd;
}

/*
 * Helper to run the actual epoll_wait
 */
int os_waiting_for_events_epoll(void)
{
	int n, err;

	n = epoll_wait(epollfd,
		(struct epoll_event *) &epoll_events, MAX_EPOLL_EVENTS, 0);
	if (n < 0) {
		err = -errno;
		if (errno != EINTR)
			printk(
				UM_KERN_ERR "os_waiting_for_events:"
				" epoll returned %d, error = %s\n", n,
				strerror(errno)
			);
		return err;
	}
	return n;
}


/*
 * Helper to add a fd to epoll
 */
int os_add_epoll_fd(int events, int fd, void *data)
{
	struct epoll_event event;
	int result;

	event.data.ptr = data;
	event.events = events | EPOLLET;
	result = epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
	if ((result) && (errno == EEXIST))
		result = os_mod_epoll_fd(events, fd, data);
	if (result)
		printk("epollctl add err fd %d, %s\n", fd, strerror(errno));
	return result;
}

/*
 * Helper to mod the fd event mask and/or data backreference
 */
int os_mod_epoll_fd(int events, int fd, void *data)
{
	struct epoll_event event;
	int result;

	event.data.ptr = data;
	event.events = events;
	result = epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
	if (result)
		printk(UM_KERN_ERR
			"epollctl mod err fd %d, %s\n", fd, strerror(errno));
	return result;
}

/*
 * Helper to delete the epoll fd
 */
int os_del_epoll_fd(int fd)
{
	struct epoll_event event;
	int result;
	/* This is quiet as we use this as IO ON/OFF - so it is often
	 * invoked on a non-existent fd
	 */
	result = epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, &event);
	return result;
}

void os_set_ioignore(void)
{
	signal(SIGIO, SIG_IGN);
}

void os_close_epoll_fd(void)
{
	/* Needed so we do not leak an fd when rebooting */
	os_close_file(epollfd);
}
