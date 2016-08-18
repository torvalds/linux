.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_DQEVENT:

********************
ioctl VIDIOC_DQEVENT
********************

Name
====

VIDIOC_DQEVENT - Dequeue event


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_event *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_DQEVENT

``argp``


Description
===========

Dequeue an event from a video device. No input is required for this
ioctl. All the fields of the struct :ref:`v4l2_event <v4l2-event>`
structure are filled by the driver. The file handle will also receive
exceptions which the application may get by e.g. using the select system
call.


.. _v4l2-event:

.. tabularcolumns:: |p{3.0cm}|p{4.3cm}|p{2.5cm}|p{7.7cm}|

.. cssclass: longtable

.. flat-table:: struct v4l2_event
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2 1


    -  .. row 1

       -  __u32

       -  ``type``

       -
       -  Type of the event, see :ref:`event-type`.

    -  .. row 2

       -  union

       -  ``u``

       -
       -

    -  .. row 3

       -
       -  struct :ref:`v4l2_event_vsync <v4l2-event-vsync>`

       -  ``vsync``

       -  Event data for event ``V4L2_EVENT_VSYNC``.

    -  .. row 4

       -
       -  struct :ref:`v4l2_event_ctrl <v4l2-event-ctrl>`

       -  ``ctrl``

       -  Event data for event ``V4L2_EVENT_CTRL``.

    -  .. row 5

       -
       -  struct :ref:`v4l2_event_frame_sync <v4l2-event-frame-sync>`

       -  ``frame_sync``

       -  Event data for event ``V4L2_EVENT_FRAME_SYNC``.

    -  .. row 6

       -
       -  struct :ref:`v4l2_event_motion_det <v4l2-event-motion-det>`

       -  ``motion_det``

       -  Event data for event V4L2_EVENT_MOTION_DET.

    -  .. row 7

       -
       -  struct :ref:`v4l2_event_src_change <v4l2-event-src-change>`

       -  ``src_change``

       -  Event data for event V4L2_EVENT_SOURCE_CHANGE.

    -  .. row 8

       -
       -  __u8

       -  ``data``\ [64]

       -  Event data. Defined by the event type. The union should be used to
	  define easily accessible type for events.

    -  .. row 9

       -  __u32

       -  ``pending``

       -
       -  Number of pending events excluding this one.

    -  .. row 10

       -  __u32

       -  ``sequence``

       -
       -  Event sequence number. The sequence number is incremented for
	  every subscribed event that takes place. If sequence numbers are
	  not contiguous it means that events have been lost.

    -  .. row 11

       -  struct timespec

       -  ``timestamp``

       -
       -  Event timestamp. The timestamp has been taken from the
	  ``CLOCK_MONOTONIC`` clock. To access the same clock outside V4L2,
	  use :c:func:`clock_gettime(2)`.

    -  .. row 12

       -  u32

       -  ``id``

       -
       -  The ID associated with the event source. If the event does not
	  have an associated ID (this depends on the event type), then this
	  is 0.

    -  .. row 13

       -  __u32

       -  ``reserved``\ [8]

       -
       -  Reserved for future extensions. Drivers must set the array to
	  zero.



.. _event-type:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. cssclass:: longtable

.. flat-table:: Event Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_EVENT_ALL``

       -  0

       -  All events. V4L2_EVENT_ALL is valid only for
	  VIDIOC_UNSUBSCRIBE_EVENT for unsubscribing all events at once.

    -  .. row 2

       -  ``V4L2_EVENT_VSYNC``

       -  1

       -  This event is triggered on the vertical sync. This event has a
	  struct :ref:`v4l2_event_vsync <v4l2-event-vsync>` associated
	  with it.

    -  .. row 3

       -  ``V4L2_EVENT_EOS``

       -  2

       -  This event is triggered when the end of a stream is reached. This
	  is typically used with MPEG decoders to report to the application
	  when the last of the MPEG stream has been decoded.

    -  .. row 4

       -  ``V4L2_EVENT_CTRL``

       -  3

       -  This event requires that the ``id`` matches the control ID from
	  which you want to receive events. This event is triggered if the
	  control's value changes, if a button control is pressed or if the
	  control's flags change. This event has a struct
	  :ref:`v4l2_event_ctrl <v4l2-event-ctrl>` associated with it.
	  This struct contains much of the same information as struct
	  :ref:`v4l2_queryctrl <v4l2-queryctrl>` and struct
	  :ref:`v4l2_control <v4l2-control>`.

	  If the event is generated due to a call to
	  :ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` or
	  :ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>`, then the
	  event will *not* be sent to the file handle that called the ioctl
	  function. This prevents nasty feedback loops. If you *do* want to
	  get the event, then set the ``V4L2_EVENT_SUB_FL_ALLOW_FEEDBACK``
	  flag.

	  This event type will ensure that no information is lost when more
	  events are raised than there is room internally. In that case the
	  struct :ref:`v4l2_event_ctrl <v4l2-event-ctrl>` of the
	  second-oldest event is kept, but the ``changes`` field of the
	  second-oldest event is ORed with the ``changes`` field of the
	  oldest event.

    -  .. row 5

       -  ``V4L2_EVENT_FRAME_SYNC``

       -  4

       -  Triggered immediately when the reception of a frame has begun.
	  This event has a struct
	  :ref:`v4l2_event_frame_sync <v4l2-event-frame-sync>`
	  associated with it.

	  If the hardware needs to be stopped in the case of a buffer
	  underrun it might not be able to generate this event. In such
	  cases the ``frame_sequence`` field in struct
	  :ref:`v4l2_event_frame_sync <v4l2-event-frame-sync>` will not
	  be incremented. This causes two consecutive frame sequence numbers
	  to have n times frame interval in between them.

    -  .. row 6

       -  ``V4L2_EVENT_SOURCE_CHANGE``

       -  5

       -  This event is triggered when a source parameter change is detected
	  during runtime by the video device. It can be a runtime resolution
	  change triggered by a video decoder or the format change happening
	  on an input connector. This event requires that the ``id`` matches
	  the input index (when used with a video device node) or the pad
	  index (when used with a subdevice node) from which you want to
	  receive events.

	  This event has a struct
	  :ref:`v4l2_event_src_change <v4l2-event-src-change>`
	  associated with it. The ``changes`` bitfield denotes what has
	  changed for the subscribed pad. If multiple events occurred before
	  application could dequeue them, then the changes will have the
	  ORed value of all the events generated.

    -  .. row 7

       -  ``V4L2_EVENT_MOTION_DET``

       -  6

       -  Triggered whenever the motion detection state for one or more of
	  the regions changes. This event has a struct
	  :ref:`v4l2_event_motion_det <v4l2-event-motion-det>`
	  associated with it.

    -  .. row 8

       -  ``V4L2_EVENT_PRIVATE_START``

       -  0x08000000

       -  Base event number for driver-private events.



.. _v4l2-event-vsync:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_event_vsync
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u8

       -  ``field``

       -  The upcoming field. See enum :ref:`v4l2_field <v4l2-field>`.



.. _v4l2-event-ctrl:

.. tabularcolumns:: |p{3.5cm}|p{3.0cm}|p{1.8cm}|p{8.5cm}|

.. flat-table:: struct v4l2_event_ctrl
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2 1


    -  .. row 1

       -  __u32

       -  ``changes``

       -
       -  A bitmask that tells what has changed. See
	  :ref:`ctrl-changes-flags`.

    -  .. row 2

       -  __u32

       -  ``type``

       -
       -  The type of the control. See enum
	  :ref:`v4l2_ctrl_type <v4l2-ctrl-type>`.

    -  .. row 3

       -  union (anonymous)

       -
       -
       -

    -  .. row 4

       -
       -  __s32

       -  ``value``

       -  The 32-bit value of the control for 32-bit control types. This is
	  0 for string controls since the value of a string cannot be passed
	  using :ref:`VIDIOC_DQEVENT`.

    -  .. row 5

       -
       -  __s64

       -  ``value64``

       -  The 64-bit value of the control for 64-bit control types.

    -  .. row 6

       -  __u32

       -  ``flags``

       -
       -  The control flags. See :ref:`control-flags`.

    -  .. row 7

       -  __s32

       -  ``minimum``

       -
       -  The minimum value of the control. See struct
	  :ref:`v4l2_queryctrl <v4l2-queryctrl>`.

    -  .. row 8

       -  __s32

       -  ``maximum``

       -
       -  The maximum value of the control. See struct
	  :ref:`v4l2_queryctrl <v4l2-queryctrl>`.

    -  .. row 9

       -  __s32

       -  ``step``

       -
       -  The step value of the control. See struct
	  :ref:`v4l2_queryctrl <v4l2-queryctrl>`.

    -  .. row 10

       -  __s32

       -  ``default_value``

       -
       -  The default value value of the control. See struct
	  :ref:`v4l2_queryctrl <v4l2-queryctrl>`.



.. _v4l2-event-frame-sync:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_event_frame_sync
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``frame_sequence``

       -  The sequence number of the frame being received.



.. _v4l2-event-src-change:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_event_src_change
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``changes``

       -  A bitmask that tells what has changed. See
	  :ref:`src-changes-flags`.



.. _v4l2-event-motion-det:

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct v4l2_event_motion_det
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``flags``

       -  Currently only one flag is available: if
	  ``V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ`` is set, then the
	  ``frame_sequence`` field is valid, otherwise that field should be
	  ignored.

    -  .. row 2

       -  __u32

       -  ``frame_sequence``

       -  The sequence number of the frame being received. Only valid if the
	  ``V4L2_EVENT_MD_FL_HAVE_FRAME_SEQ`` flag was set.

    -  .. row 3

       -  __u32

       -  ``region_mask``

       -  The bitmask of the regions that reported motion. There is at least
	  one region. If this field is 0, then no motion was detected at
	  all. If there is no ``V4L2_CID_DETECT_MD_REGION_GRID`` control
	  (see :ref:`detect-controls`) to assign a different region to
	  each cell in the motion detection grid, then that all cells are
	  automatically assigned to the default region 0.



.. _ctrl-changes-flags:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Control Changes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_EVENT_CTRL_CH_VALUE``

       -  0x0001

       -  This control event was triggered because the value of the control
	  changed. Special cases: Volatile controls do no generate this
	  event; If a control has the ``V4L2_CTRL_FLAG_EXECUTE_ON_WRITE``
	  flag set, then this event is sent as well, regardless its value.

    -  .. row 2

       -  ``V4L2_EVENT_CTRL_CH_FLAGS``

       -  0x0002

       -  This control event was triggered because the control flags
	  changed.

    -  .. row 3

       -  ``V4L2_EVENT_CTRL_CH_RANGE``

       -  0x0004

       -  This control event was triggered because the minimum, maximum,
	  step or the default value of the control changed.



.. _src-changes-flags:

.. tabularcolumns:: |p{6.6cm}|p{2.2cm}|p{8.7cm}|

.. flat-table:: Source Changes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_EVENT_SRC_CH_RESOLUTION``

       -  0x0001

       -  This event gets triggered when a resolution change is detected at
	  an input. This can come from an input connector or from a video
	  decoder.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
