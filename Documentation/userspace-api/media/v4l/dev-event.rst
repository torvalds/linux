.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _event:

***************
Event Interface
***************

The V4L2 event interface provides a means for a user to get immediately
notified on certain conditions taking place on a device. This might
include start of frame or loss of signal events, for example. Changes in
the value or state of a V4L2 control can also be reported through
events.

To receive events, the events the user is interested in first must be
subscribed using the
:ref:`VIDIOC_SUBSCRIBE_EVENT` ioctl. Once
an event is subscribed, the events of subscribed types are dequeueable
using the :ref:`VIDIOC_DQEVENT` ioctl. Events may be
unsubscribed using VIDIOC_UNSUBSCRIBE_EVENT ioctl. The special event
type V4L2_EVENT_ALL may be used to unsubscribe all the events the
driver supports.

The event subscriptions and event queues are specific to file handles.
Subscribing an event on one file handle does not affect other file
handles.

The information on dequeueable events is obtained by using select or
poll system calls on video devices. The V4L2 events use POLLPRI events
on poll system call and exceptions on select system call.

Starting with kernel 3.1 certain guarantees can be given with regards to
events:

1. Each subscribed event has its own internal dedicated event queue.
   This means that flooding of one event type will not interfere with
   other event types.

2. If the internal event queue for a particular subscribed event becomes
   full, then the oldest event in that queue will be dropped.

3. Where applicable, certain event types can ensure that the payload of
   the oldest event that is about to be dropped will be merged with the
   payload of the next oldest event. Thus ensuring that no information
   is lost, but only an intermediate step leading up to that
   information. See the documentation for the event you want to
   subscribe to whether this is applicable for that event or not.
