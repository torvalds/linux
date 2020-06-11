.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/userspace-api/media/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _VIDIOC_SUBSCRIBE_EVENT:
.. _VIDIOC_UNSUBSCRIBE_EVENT:

******************************************************
ioctl VIDIOC_SUBSCRIBE_EVENT, VIDIOC_UNSUBSCRIBE_EVENT
******************************************************

Name
====

VIDIOC_SUBSCRIBE_EVENT - VIDIOC_UNSUBSCRIBE_EVENT - Subscribe or unsubscribe event


Synopsis
========

.. c:function:: int ioctl( int fd, VIDIOC_SUBSCRIBE_EVENT, struct v4l2_event_subscription *argp )
    :name: VIDIOC_SUBSCRIBE_EVENT

.. c:function:: int ioctl( int fd, VIDIOC_UNSUBSCRIBE_EVENT, struct v4l2_event_subscription *argp )
    :name: VIDIOC_UNSUBSCRIBE_EVENT


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``argp``
    Pointer to struct :c:type:`v4l2_event_subscription`.


Description
===========

Subscribe or unsubscribe V4L2 event. Subscribed events are dequeued by
using the :ref:`VIDIOC_DQEVENT` ioctl.


.. tabularcolumns:: |p{4.6cm}|p{4.4cm}|p{8.7cm}|

.. c:type:: v4l2_event_subscription

.. flat-table:: struct v4l2_event_subscription
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2

    * - __u32
      - ``type``
      - Type of the event, see :ref:`event-type`.

	.. note::

	   ``V4L2_EVENT_ALL`` can be used with
	   :ref:`VIDIOC_UNSUBSCRIBE_EVENT <VIDIOC_SUBSCRIBE_EVENT>` for
	   unsubscribing all events at once.
    * - __u32
      - ``id``
      - ID of the event source. If there is no ID associated with the
	event source, then set this to 0. Whether or not an event needs an
	ID depends on the event type.
    * - __u32
      - ``flags``
      - Event flags, see :ref:`event-flags`.
    * - __u32
      - ``reserved``\ [5]
      - Reserved for future extensions. Drivers and applications must set
	the array to zero.



.. tabularcolumns:: |p{6.8cm}|p{2.2cm}|p{8.5cm}|

.. _event-flags:

.. flat-table:: Event Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4

    * - ``V4L2_EVENT_SUB_FL_SEND_INITIAL``
      - 0x0001
      - When this event is subscribed an initial event will be sent
	containing the current status. This only makes sense for events
	that are triggered by a status change such as ``V4L2_EVENT_CTRL``.
	Other events will ignore this flag.
    * - ``V4L2_EVENT_SUB_FL_ALLOW_FEEDBACK``
      - 0x0002
      - If set, then events directly caused by an ioctl will also be sent
	to the filehandle that called that ioctl. For example, changing a
	control using :ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` will cause
	a V4L2_EVENT_CTRL to be sent back to that same filehandle.
	Normally such events are suppressed to prevent feedback loops
	where an application changes a control to a one value and then
	another, and then receives an event telling it that that control
	has changed to the first value.

	Since it can't tell whether that event was caused by another
	application or by the :ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>`
	call it is hard to decide whether to set the control to the value
	in the event, or ignore it.

	Think carefully when you set this flag so you won't get into
	situations like that.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
