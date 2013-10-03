/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * libcfs/libcfs/lwt.c
 *
 * Author: Eric Barton <eeb@clusterfs.com>
 */

#define DEBUG_SUBSYSTEM S_LNET

#include <linux/libcfs/libcfs.h>

#if LWT_SUPPORT

#if !KLWT_SUPPORT
int	 lwt_enabled;
lwt_cpu_t   lwt_cpus[NR_CPUS];
#endif

int	 lwt_pages_per_cpu;

/* NB only root is allowed to retrieve LWT info; it's an open door into the
 * kernel... */

int
lwt_lookup_string (int *size, char *knl_ptr,
		   char *user_ptr, int user_size)
{
	int   maxsize = 128;

	/* knl_ptr was retrieved from an LWT snapshot and the caller wants to
	 * turn it into a string.  NB we can crash with an access violation
	 * trying to determine the string length, so we're trusting our
	 * caller... */

	if (!cfs_capable(CFS_CAP_SYS_ADMIN))
		return (-EPERM);

	if (user_size > 0 &&
	    maxsize > user_size)
		maxsize = user_size;

	*size = strnlen (knl_ptr, maxsize - 1) + 1;

	if (user_ptr != NULL) {
		if (user_size < 4)
			return (-EINVAL);

		if (copy_to_user (user_ptr, knl_ptr, *size))
			return (-EFAULT);

		/* Did I truncate the string?  */
		if (knl_ptr[*size - 1] != 0)
			copy_to_user (user_ptr + *size - 4, "...", 4);
	}

	return (0);
}

int
lwt_control (int enable, int clear)
{
	lwt_page_t  *p;
	int	  i;
	int	  j;

	if (!cfs_capable(CFS_CAP_SYS_ADMIN))
		return (-EPERM);

	if (!enable) {
		LWT_EVENT(0,0,0,0);
		lwt_enabled = 0;
		mb();
		/* give people some time to stop adding traces */
		schedule_timeout(10);
	}

	for (i = 0; i < num_online_cpus(); i++) {
		p = lwt_cpus[i].lwtc_current_page;

		if (p == NULL)
			return (-ENODATA);

		if (!clear)
			continue;

		for (j = 0; j < lwt_pages_per_cpu; j++) {
			memset (p->lwtp_events, 0, PAGE_CACHE_SIZE);

			p = list_entry (p->lwtp_list.next,
					    lwt_page_t, lwtp_list);
		}
	}

	if (enable) {
		lwt_enabled = 1;
		mb();
		LWT_EVENT(0,0,0,0);
	}

	return (0);
}

int
lwt_snapshot (cfs_cycles_t *now, int *ncpu, int *total_size,
	      void *user_ptr, int user_size)
{
	const int    events_per_page = PAGE_CACHE_SIZE / sizeof(lwt_event_t);
	const int    bytes_per_page = events_per_page * sizeof(lwt_event_t);
	lwt_page_t  *p;
	int	  i;
	int	  j;

	if (!cfs_capable(CFS_CAP_SYS_ADMIN))
		return (-EPERM);

	*ncpu = num_online_cpus();
	*total_size = num_online_cpus() * lwt_pages_per_cpu *
		bytes_per_page;
	*now = get_cycles();

	if (user_ptr == NULL)
		return (0);

	for (i = 0; i < num_online_cpus(); i++) {
		p = lwt_cpus[i].lwtc_current_page;

		if (p == NULL)
			return (-ENODATA);

		for (j = 0; j < lwt_pages_per_cpu; j++) {
			if (copy_to_user(user_ptr, p->lwtp_events,
					     bytes_per_page))
				return (-EFAULT);

			user_ptr = ((char *)user_ptr) + bytes_per_page;
			p = list_entry(p->lwtp_list.next,
					   lwt_page_t, lwtp_list);
		}
	}

	return (0);
}

int
lwt_init ()
{
	int     i;
	int     j;

	for (i = 0; i < num_online_cpus(); i++)
		if (lwt_cpus[i].lwtc_current_page != NULL)
			return (-EALREADY);

	LASSERT (!lwt_enabled);

	/* NULL pointers, zero scalars */
	memset (lwt_cpus, 0, sizeof (lwt_cpus));
	lwt_pages_per_cpu =
		LWT_MEMORY / (num_online_cpus() * PAGE_CACHE_SIZE);

	for (i = 0; i < num_online_cpus(); i++)
		for (j = 0; j < lwt_pages_per_cpu; j++) {
			struct page *page = alloc_page (GFP_KERNEL);
			lwt_page_t  *lwtp;

			if (page == NULL) {
				CERROR ("Can't allocate page\n");
				lwt_fini ();
				return (-ENOMEM);
			}

			LIBCFS_ALLOC(lwtp, sizeof (*lwtp));
			if (lwtp == NULL) {
				CERROR ("Can't allocate lwtp\n");
				__free_page(page);
				lwt_fini ();
				return (-ENOMEM);
			}

			lwtp->lwtp_page = page;
			lwtp->lwtp_events = page_address(page);
			memset (lwtp->lwtp_events, 0, PAGE_CACHE_SIZE);

			if (j == 0) {
				INIT_LIST_HEAD (&lwtp->lwtp_list);
				lwt_cpus[i].lwtc_current_page = lwtp;
			} else {
				list_add (&lwtp->lwtp_list,
				    &lwt_cpus[i].lwtc_current_page->lwtp_list);
			}
		}

	lwt_enabled = 1;
	mb();

	LWT_EVENT(0,0,0,0);

	return (0);
}

void
lwt_fini ()
{
	int    i;

	lwt_control(0, 0);

	for (i = 0; i < num_online_cpus(); i++)
		while (lwt_cpus[i].lwtc_current_page != NULL) {
			lwt_page_t *lwtp = lwt_cpus[i].lwtc_current_page;

			if (list_empty (&lwtp->lwtp_list)) {
				lwt_cpus[i].lwtc_current_page = NULL;
			} else {
				lwt_cpus[i].lwtc_current_page =
					list_entry (lwtp->lwtp_list.next,
							lwt_page_t, lwtp_list);

				list_del (&lwtp->lwtp_list);
			}

			__free_page (lwtp->lwtp_page);
			LIBCFS_FREE (lwtp, sizeof (*lwtp));
		}
}

EXPORT_SYMBOL(lwt_enabled);
EXPORT_SYMBOL(lwt_cpus);

EXPORT_SYMBOL(lwt_init);
EXPORT_SYMBOL(lwt_fini);
EXPORT_SYMBOL(lwt_lookup_string);
EXPORT_SYMBOL(lwt_control);
EXPORT_SYMBOL(lwt_snapshot);
#endif
