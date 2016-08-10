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

.. cpp:function:: int ioctl( int fd, int request, struct cec_event *argp )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <cec-func-open>`.

``request``
    CEC_DQEVENT

``argp``


Description
===========

.. note:: This documents the proposed CEC API. This API is not yet finalized
   and is currently only available as a staging kernel module.

CEC devices can send asynchronous events. These can be retrieved by
calling :ref:`ioctl CEC_DQEVENT <CEC_DQEVENT>`. If the file descriptor is in
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


.. _cec-event-state-change_s:

.. flat-table:: struct cec_event_state_change
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 8


    -  .. row 1

       -  __u16

       -  ``phys_addr``

       -  The current physical address.

    -  .. row 2

       -  __u16

       -  ``log_addr_mask``

       -  The current set of claimed logical addresses.



.. _cec-event-lost-msgs_s:

.. flat-table:: struct cec_event_lost_msgs
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 16


    -  .. row 1

       -  __u32

       -  ``lost_msgs``

       -  Set to the number of lost messages since the filehandle was opened
	  or since the last time this event was dequeued for this
	  filehandle. The messages lost are the oldest messages. So when a
	  new message arrives and there is no more room, then the oldest
	  message is discarded to make room for the new one. The internal
	  size of the message queue guarantees that all messages received in
	  the last two seconds will be stored. Since messages should be
	  replied to within a second according to the CEC specification,
	  this is more than enough.



.. _cec-event:

.. flat-table:: struct cec_event
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 1 8


    -  .. row 1

       -  __u64

       -  ``ts``

       -  Timestamp of the event in ns.
	  The timestamp has been taken from the ``CLOCK_MONOTONIC`` clock. To access
	  the same clock from userspace use :c:func:`clock_gettime(2)`.

       -

    -  .. row 2

       -  __u32

       -  ``event``

       -  The CEC event type, see :ref:`cec-events`.

       -

    -  .. row 3

       -  __u32

       -  ``flags``

       -  Event flags, see :ref:`cec-event-flags`.

       -

    -  .. row 4

       -  union

       -  (anonymous)

       -
       -

    -  .. row 5

       -
       -  struct cec_event_state_change

       -  ``state_change``

       -  The new adapter state as sent by the :ref:`CEC_EVENT_STATE_CHANGE <CEC-EVENT-STATE-CHANGE>`
	  event.

    -  .. row 6

       -
       -  struct cec_event_lost_msgs

       -  ``lost_msgs``

       -  The number of lost messages as sent by the :ref:`CEC_EVENT_LOST_MSGS <CEC-EVENT-LOST-MSGS>`
	  event.



.. _cec-events:

.. flat-table:: CEC Events Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 16


    -  .. _`CEC-EVENT-STATE-CHANGE`:

       -  ``CEC_EVENT_STATE_CHANGE``

       -  1

       -  Generated when the CEC Adapter's state changes. When open() is
	  called an initial event will be generated for that filehandle with
	  the CEC Adapter's state at that time.

    -  .. _`CEC-EVENT-LOST-MSGS`:

       -  ``CEC_EVENT_LOST_MSGS``

       -  2

       -  Generated if one or more CEC messages were lost because the
	  application didn't dequeue CEC messages fast enough.



.. _cec-event-flags:

.. flat-table:: CEC Event Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 8


    -  .. _`CEC-EVENT-FL-INITIAL-VALUE`:

       -  ``CEC_EVENT_FL_INITIAL_VALUE``

       -  1

       -  Set for the initial events that are generated when the device is
	  opened. See the table above for which events do this. This allows
	  applications to learn the initial state of the CEC adapter at
	  open() time.



Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
