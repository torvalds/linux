.. -*- coding: utf-8; mode: rst -*-

.. _CEC_DQEVENT:

*****************
ioctl CEC_DQEVENT
*****************

Name
====

CEC_DQEVENT - Dequeue a CEC event


Synopsis
========

.. c:function:: int ioctl( int fd, CEC_DQEVENT, struct cec_event *argp )
    :name: CEC_DQEVENT

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <cec-open>`.

``argp``


Description
===========

CEC devices can send asynchronous events. These can be retrieved by
calling :c:func:`CEC_DQEVENT`. If the file descriptor is in
non-blocking mode and no event is pending, then it will return -1 and
set errno to the ``EAGAIN`` error code.

The internal event queues are per-filehandle and per-event type. If
there is no more room in a queue then the last event is overwritten with
the new one. This means that intermediate results can be thrown away but
that the latest event is always available. This also means that is it
possible to read two successive events that have the same value (e.g.
two :ref:`CEC_EVENT_STATE_CHANGE <CEC-EVENT-STATE-CHANGE>` events with
the same state). In that case the intermediate state changes were lost but
it is guaranteed that the state did change in between the two events.

.. tabularcolumns:: |p{1.2cm}|p{2.9cm}|p{13.4cm}|

.. c:type:: cec_event_state_change

.. flat-table:: struct cec_event_state_change
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 8

    * - __u16
      - ``phys_addr``
      - The current physical address. This is ``CEC_PHYS_ADDR_INVALID`` if no
        valid physical address is set.
    * - __u16
      - ``log_addr_mask``
      - The current set of claimed logical addresses. This is 0 if no logical
        addresses are claimed or if ``phys_addr`` is ``CEC_PHYS_ADDR_INVALID``.
	If bit 15 is set (``1 << CEC_LOG_ADDR_UNREGISTERED``) then this device
	has the unregistered logical address. In that case all other bits are 0.


.. c:type:: cec_event_lost_msgs

.. tabularcolumns:: |p{1.0cm}|p{2.0cm}|p{14.5cm}|

.. flat-table:: struct cec_event_lost_msgs
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 16

    * - __u32
      - ``lost_msgs``
      - Set to the number of lost messages since the filehandle was opened
	or since the last time this event was dequeued for this
	filehandle. The messages lost are the oldest messages. So when a
	new message arrives and there is no more room, then the oldest
	message is discarded to make room for the new one. The internal
	size of the message queue guarantees that all messages received in
	the last two seconds will be stored. Since messages should be
	replied to within a second according to the CEC specification,
	this is more than enough.


.. tabularcolumns:: |p{1.0cm}|p{4.4cm}|p{2.5cm}|p{9.6cm}|

.. c:type:: cec_event

.. flat-table:: struct cec_event
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 1 8

    * - __u64
      - ``ts``
      - :cspan:`1`\ Timestamp of the event in ns.

	The timestamp has been taken from the ``CLOCK_MONOTONIC`` clock.

	To access the same clock from userspace use :c:func:`clock_gettime`.
    * - __u32
      - ``event``
      - :cspan:`1` The CEC event type, see :ref:`cec-events`.
    * - __u32
      - ``flags``
      - :cspan:`1` Event flags, see :ref:`cec-event-flags`.
    * - union
      - (anonymous)
      -
      -
    * -
      - struct cec_event_state_change
      - ``state_change``
      - The new adapter state as sent by the :ref:`CEC_EVENT_STATE_CHANGE <CEC-EVENT-STATE-CHANGE>`
	event.
    * -
      - struct cec_event_lost_msgs
      - ``lost_msgs``
      - The number of lost messages as sent by the :ref:`CEC_EVENT_LOST_MSGS <CEC-EVENT-LOST-MSGS>`
	event.


.. tabularcolumns:: |p{5.6cm}|p{0.9cm}|p{11.0cm}|

.. _cec-events:

.. flat-table:: CEC Events Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 16

    * .. _`CEC-EVENT-STATE-CHANGE`:

      - ``CEC_EVENT_STATE_CHANGE``
      - 1
      - Generated when the CEC Adapter's state changes. When open() is
	called an initial event will be generated for that filehandle with
	the CEC Adapter's state at that time.
    * .. _`CEC-EVENT-LOST-MSGS`:

      - ``CEC_EVENT_LOST_MSGS``
      - 2
      - Generated if one or more CEC messages were lost because the
	application didn't dequeue CEC messages fast enough.
    * .. _`CEC-EVENT-PIN-CEC-LOW`:

      - ``CEC_EVENT_PIN_CEC_LOW``
      - 3
      - Generated if the CEC pin goes from a high voltage to a low voltage.
        Only applies to adapters that have the ``CEC_CAP_MONITOR_PIN``
	capability set.
    * .. _`CEC-EVENT-PIN-CEC-HIGH`:

      - ``CEC_EVENT_PIN_CEC_HIGH``
      - 4
      - Generated if the CEC pin goes from a low voltage to a high voltage.
        Only applies to adapters that have the ``CEC_CAP_MONITOR_PIN``
	capability set.
    * .. _`CEC-EVENT-PIN-HPD-LOW`:

      - ``CEC_EVENT_PIN_HPD_LOW``
      - 5
      - Generated if the HPD pin goes from a high voltage to a low voltage.
	Only applies to adapters that have the ``CEC_CAP_MONITOR_PIN``
	capability set. When open() is called, the HPD pin can be read and
	if the HPD is low, then an initial event will be generated for that
	filehandle.
    * .. _`CEC-EVENT-PIN-HPD-HIGH`:

      - ``CEC_EVENT_PIN_HPD_HIGH``
      - 6
      - Generated if the HPD pin goes from a low voltage to a high voltage.
	Only applies to adapters that have the ``CEC_CAP_MONITOR_PIN``
	capability set. When open() is called, the HPD pin can be read and
	if the HPD is high, then an initial event will be generated for that
	filehandle.


.. tabularcolumns:: |p{6.0cm}|p{0.6cm}|p{10.9cm}|

.. _cec-event-flags:

.. flat-table:: CEC Event Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 8

    * .. _`CEC-EVENT-FL-INITIAL-STATE`:

      - ``CEC_EVENT_FL_INITIAL_STATE``
      - 1
      - Set for the initial events that are generated when the device is
	opened. See the table above for which events do this. This allows
	applications to learn the initial state of the CEC adapter at
	open() time.
    * .. _`CEC-EVENT-FL-DROPPED-EVENTS`:

      - ``CEC_EVENT_FL_DROPPED_EVENTS``
      - 2
      - Set if one or more events of the given event type have been dropped.
        This is an indication that the application cannot keep up.



Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

The :ref:`ioctl CEC_DQEVENT <CEC_DQEVENT>` can return the following
error codes:

EAGAIN
    This is returned when the filehandle is in non-blocking mode and there
    are no pending events.

ERESTARTSYS
    An interrupt (e.g. Ctrl-C) arrived while in blocking mode waiting for
    events to arrive.
