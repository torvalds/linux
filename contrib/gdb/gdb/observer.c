/* GDB Notifications to Observers.
   Copyright 2003 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.  */

/* An observer is an entity who is interested in being notified when GDB
   reaches certain states, or certain events occur in GDB. The entity being
   observed is called the Subject. To receive notifications, the observer
   attaches a callback to the subject. One subject can have several
   observers.

   This file implements an internal generic low-level event notification
   mechanism based on the Observer paradigm described in the book "Design
   Patterns".  This generic event notification mechansim is then re-used
   to implement the exported high-level notification management routines
   for all possible notifications.

   The current implementation of the generic observer provides support
   for contextual data. This contextual data is given to the subject
   when attaching the callback. In return, the subject will provide
   this contextual data back to the observer as a parameter of the
   callback.

   FIXME: The current support for the contextual data is only partial,
   as it lacks a mechanism that would deallocate this data when the
   callback is detached. This is not a problem so far, as this contextual
   data is only used internally to hold a function pointer. Later on,
   if a certain observer needs to provide support for user-level
   contextual data, then the generic notification mechanism will need
   need to be enhanced to allow the observer to provide a routine to
   deallocate the data when attaching the callback.

   This file is currently maintained by hand, but the long term plan
   if the number of different notifications starts growing is to create
   a new script (observer.sh) that would generate this file, and the
   associated documentation.  */

#include "defs.h"
#include "observer.h"

/* The internal generic observer.  */

typedef void (generic_observer_notification_ftype) (const void *data,
						    const void *args);

struct observer
{
  generic_observer_notification_ftype *notify;
  /* No memory management needed for the following field for now.  */
  void *data;
};

/* A list of observers, maintained by the subject.  A subject is
   actually represented by its list of observers.  */

struct observer_list
{
  struct observer_list *next;
  struct observer *observer;
};

/* Allocate a struct observer_list, intended to be used as a node
   in the list of observers maintained by a subject.  */

static struct observer_list *
xalloc_observer_list_node (void)
{
  struct observer_list *node = XMALLOC (struct observer_list);
  node->observer = XMALLOC (struct observer);
  return node;
}

/* The opposite of xalloc_observer_list_node, frees the memory for
   the given node.  */

static void
xfree_observer_list_node (struct observer_list *node)
{
  xfree (node->observer);
  xfree (node);
}

/* Attach the callback NOTIFY to a SUBJECT.  The DATA is also stored,
   in order for the subject to provide it back to the observer during
   a notification.  */

static struct observer *
generic_observer_attach (struct observer_list **subject,
			 generic_observer_notification_ftype * notify,
			 void *data)
{
  struct observer_list *observer_list = xalloc_observer_list_node ();

  observer_list->next = *subject;
  observer_list->observer->notify = notify;
  observer_list->observer->data = data;
  *subject = observer_list;

  return observer_list->observer;
}

/* Remove the given OBSERVER from the SUBJECT.  Once detached, OBSERVER
   should no longer be used, as it is no longer valid.  */

static void
generic_observer_detach (struct observer_list **subject,
			 const struct observer *observer)
{
  struct observer_list *previous_node = NULL;
  struct observer_list *current_node = *subject;

  while (current_node != NULL)
    {
      if (current_node->observer == observer)
	{
	  if (previous_node != NULL)
	    previous_node->next = current_node->next;
	  else
	    *subject = current_node->next;
	  xfree_observer_list_node (current_node);
	  return;
	}
      previous_node = current_node;
      current_node = current_node->next;
    }

  /* We should never reach this point.  However, this should not be
     a very serious error, so simply report a warning to the user.  */
  warning ("Failed to detach observer");
}

/* Send a notification to all the observers of SUBJECT.  ARGS is passed to
   all observers as an argument to the notification callback.  */

static void
generic_observer_notify (struct observer_list *subject, const void *args)
{
  struct observer_list *current_node = subject;

  while (current_node != NULL)
    {
      (*current_node->observer->notify) (current_node->observer->data, args);
      current_node = current_node->next;
    }
}

/* normal_stop notifications.  */

static struct observer_list *normal_stop_subject = NULL;

static void
observer_normal_stop_notification_stub (const void *data,
					const void *unused_args)
{
  observer_normal_stop_ftype *notify = (observer_normal_stop_ftype *) data;
  (*notify) ();
}

struct observer *
observer_attach_normal_stop (observer_normal_stop_ftype *f)
{
  return generic_observer_attach (&normal_stop_subject,
				  &observer_normal_stop_notification_stub,
				  (void *) f);
}

void
observer_detach_normal_stop (struct observer *observer)
{
  generic_observer_detach (&normal_stop_subject, observer);
}

void
observer_notify_normal_stop (void)
{
  generic_observer_notify (normal_stop_subject, NULL);
}

/* The following code is only used to unit-test the observers from our
   testsuite.  DO NOT USE IT within observer.c (or anywhere else for
   that matter)!  */

/* If we define these variables and functions as `static', the
   compiler will optimize them out.  */
 
int observer_test_first_observer = 0;
int observer_test_second_observer = 0;
int observer_test_third_observer = 0;

void
observer_test_first_notification_function (void)
{
  observer_test_first_observer++;
}

void
observer_test_second_notification_function (void)
{
  observer_test_second_observer++;
}

void
observer_test_third_notification_function (void)
{
  observer_test_third_observer++;
}

