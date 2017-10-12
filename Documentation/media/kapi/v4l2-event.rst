
V4L2 events
-----------

The V4L2 events provide a generic way to pass events to user space.
The driver must use :c:type:`v4l2_fh` to be able to support V4L2 events.

Events are defined by a type and an optional ID. The ID may refer to a V4L2
object such as a control ID. If unused, then the ID is 0.

When the user subscribes to an event the driver will allocate a number of
kevent structs for that event. So every (type, ID) event tuple will have
its own set of kevent structs. This guarantees that if a driver is generating
lots of events of one type in a short time, then that will not overwrite
events of another type.

But if you get more events of one type than the number of kevents that were
reserved, then the oldest event will be dropped and the new one added.

Furthermore, the internal struct :c:type:`v4l2_subscribed_event` has
``merge()`` and ``replace()`` callbacks which drivers can set. These
callbacks are called when a new event is raised and there is no more room.
The ``replace()`` callback allows you to replace the payload of the old event
with that of the new event, merging any relevant data from the old payload
into the new payload that replaces it. It is called when this event type has
only one kevent struct allocated. The ``merge()`` callback allows you to merge
the oldest event payload into that of the second-oldest event payload. It is
called when there are two or more kevent structs allocated.

This way no status information is lost, just the intermediate steps leading
up to that state.

A good example of these ``replace``/``merge`` callbacks is in v4l2-event.c:
``ctrls_replace()`` and ``ctrls_merge()`` callbacks for the control event.

.. note::
	these callbacks can be called from interrupt context, so they must
	be fast.

In order to queue events to video device, drivers should call:

	:c:func:`v4l2_event_queue <v4l2_event_queue>`
	(:c:type:`vdev <video_device>`, :c:type:`ev <v4l2_event>`)

The driver's only responsibility is to fill in the type and the data fields.
The other fields will be filled in by V4L2.

Event subscription
~~~~~~~~~~~~~~~~~~

Subscribing to an event is via:

	:c:func:`v4l2_event_subscribe <v4l2_event_subscribe>`
	(:c:type:`fh <v4l2_fh>`, :c:type:`sub <v4l2_event_subscription>` ,
	elems, :c:type:`ops <v4l2_subscribed_event_ops>`)


This function is used to implement :c:type:`video_device`->
:c:type:`ioctl_ops <v4l2_ioctl_ops>`-> ``vidioc_subscribe_event``,
but the driver must check first if the driver is able to produce events
with specified event id, and then should call
:c:func:`v4l2_event_subscribe` to subscribe the event.

The elems argument is the size of the event queue for this event. If it is 0,
then the framework will fill in a default value (this depends on the event
type).

The ops argument allows the driver to specify a number of callbacks:

.. tabularcolumns:: |p{1.5cm}|p{16.0cm}|

======== ==============================================================
Callback Description
======== ==============================================================
add      called when a new listener gets added (subscribing to the same
         event twice will only cause this callback to get called once)
del      called when a listener stops listening
replace  replace event 'old' with event 'new'.
merge    merge event 'old' into event 'new'.
======== ==============================================================

All 4 callbacks are optional, if you don't want to specify any callbacks
the ops argument itself maybe ``NULL``.

Unsubscribing an event
~~~~~~~~~~~~~~~~~~~~~~

Unsubscribing to an event is via:

	:c:func:`v4l2_event_unsubscribe <v4l2_event_unsubscribe>`
	(:c:type:`fh <v4l2_fh>`, :c:type:`sub <v4l2_event_subscription>`)

This function is used to implement :c:type:`video_device`->
:c:type:`ioctl_ops <v4l2_ioctl_ops>`-> ``vidioc_unsubscribe_event``.
A driver may call :c:func:`v4l2_event_unsubscribe` directly unless it
wants to be involved in unsubscription process.

The special type ``V4L2_EVENT_ALL`` may be used to unsubscribe all events. The
drivers may want to handle this in a special way.

Check if there's a pending event
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Checking if there's a pending event is via:

	:c:func:`v4l2_event_pending <v4l2_event_pending>`
	(:c:type:`fh <v4l2_fh>`)


This function returns the number of pending events. Useful when implementing
poll.

How events work
~~~~~~~~~~~~~~~

Events are delivered to user space through the poll system call. The driver
can use :c:type:`v4l2_fh`->wait (a wait_queue_head_t) as the argument for
``poll_wait()``.

There are standard and private events. New standard events must use the
smallest available event type. The drivers must allocate their events from
their own class starting from class base. Class base is
``V4L2_EVENT_PRIVATE_START`` + n * 1000 where n is the lowest available number.
The first event type in the class is reserved for future use, so the first
available event type is 'class base + 1'.

An example on how the V4L2 events may be used can be found in the OMAP
3 ISP driver (``drivers/media/platform/omap3isp``).

A subdev can directly send an event to the :c:type:`v4l2_device` notify
function with ``V4L2_DEVICE_NOTIFY_EVENT``. This allows the bridge to map
the subdev that sends the event to the video node(s) associated with the
subdev that need to be informed about such an event.

V4L2 event functions and data structures
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

.. kernel-doc:: include/media/v4l2-event.h

