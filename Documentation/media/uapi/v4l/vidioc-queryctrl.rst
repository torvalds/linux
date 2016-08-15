.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_QUERYCTRL:

*******************************************************************
ioctls VIDIOC_QUERYCTRL, VIDIOC_QUERY_EXT_CTRL and VIDIOC_QUERYMENU
*******************************************************************

Name
====

VIDIOC_QUERYCTRL - VIDIOC_QUERY_EXT_CTRL - VIDIOC_QUERYMENU - Enumerate controls and menu control items


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_queryctrl *argp )

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_query_ext_ctrl *argp )

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_querymenu *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_QUERYCTRL, VIDIOC_QUERY_EXT_CTRL, VIDIOC_QUERYMENU

``argp``


Description
===========

To query the attributes of a control applications set the ``id`` field
of a struct :ref:`v4l2_queryctrl <v4l2-queryctrl>` and call the
``VIDIOC_QUERYCTRL`` ioctl with a pointer to this structure. The driver
fills the rest of the structure or returns an ``EINVAL`` error code when the
``id`` is invalid.

It is possible to enumerate controls by calling ``VIDIOC_QUERYCTRL``
with successive ``id`` values starting from ``V4L2_CID_BASE`` up to and
exclusive ``V4L2_CID_LASTP1``. Drivers may return ``EINVAL`` if a control in
this range is not supported. Further applications can enumerate private
controls, which are not defined in this specification, by starting at
``V4L2_CID_PRIVATE_BASE`` and incrementing ``id`` until the driver
returns ``EINVAL``.

In both cases, when the driver sets the ``V4L2_CTRL_FLAG_DISABLED`` flag
in the ``flags`` field this control is permanently disabled and should
be ignored by the application. [#f1]_

When the application ORs ``id`` with ``V4L2_CTRL_FLAG_NEXT_CTRL`` the
driver returns the next supported non-compound control, or ``EINVAL`` if
there is none. In addition, the ``V4L2_CTRL_FLAG_NEXT_COMPOUND`` flag
can be specified to enumerate all compound controls (i.e. controls with
type ≥ ``V4L2_CTRL_COMPOUND_TYPES`` and/or array control, in other words
controls that contain more than one value). Specify both
``V4L2_CTRL_FLAG_NEXT_CTRL`` and ``V4L2_CTRL_FLAG_NEXT_COMPOUND`` in
order to enumerate all controls, compound or not. Drivers which do not
support these flags yet always return ``EINVAL``.

The ``VIDIOC_QUERY_EXT_CTRL`` ioctl was introduced in order to better
support controls that can use compound types, and to expose additional
control information that cannot be returned in struct
:ref:`v4l2_queryctrl <v4l2-queryctrl>` since that structure is full.

``VIDIOC_QUERY_EXT_CTRL`` is used in the same way as
``VIDIOC_QUERYCTRL``, except that the ``reserved`` array must be zeroed
as well.

Additional information is required for menu controls: the names of the
menu items. To query them applications set the ``id`` and ``index``
fields of struct :ref:`v4l2_querymenu <v4l2-querymenu>` and call the
``VIDIOC_QUERYMENU`` ioctl with a pointer to this structure. The driver
fills the rest of the structure or returns an ``EINVAL`` error code when the
``id`` or ``index`` is invalid. Menu items are enumerated by calling
``VIDIOC_QUERYMENU`` with successive ``index`` values from struct
:ref:`v4l2_queryctrl <v4l2-queryctrl>` ``minimum`` to ``maximum``,
inclusive.

.. note::

   It is possible for ``VIDIOC_QUERYMENU`` to return
   an ``EINVAL`` error code for some indices between ``minimum`` and
   ``maximum``. In that case that particular menu item is not supported by
   this driver. Also note that the ``minimum`` value is not necessarily 0.

See also the examples in :ref:`control`.


.. _v4l2-queryctrl:

.. flat-table:: struct v4l2_queryctrl
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``id``

       -  Identifies the control, set by the application. See
	  :ref:`control-id` for predefined IDs. When the ID is ORed with
	  V4L2_CTRL_FLAG_NEXT_CTRL the driver clears the flag and
	  returns the first control with a higher ID. Drivers which do not
	  support this flag yet always return an ``EINVAL`` error code.

    -  .. row 2

       -  __u32

       -  ``type``

       -  Type of control, see :ref:`v4l2-ctrl-type`.

    -  .. row 3

       -  __u8

       -  ``name``\ [32]

       -  Name of the control, a NUL-terminated ASCII string. This
	  information is intended for the user.

    -  .. row 4

       -  __s32

       -  ``minimum``

       -  Minimum value, inclusive. This field gives a lower bound for the
	  control. See enum :ref:`v4l2_ctrl_type <v4l2-ctrl-type>` how
	  the minimum value is to be used for each possible control type.
	  Note that this a signed 32-bit value.

    -  .. row 5

       -  __s32

       -  ``maximum``

       -  Maximum value, inclusive. This field gives an upper bound for the
	  control. See enum :ref:`v4l2_ctrl_type <v4l2-ctrl-type>` how
	  the maximum value is to be used for each possible control type.
	  Note that this a signed 32-bit value.

    -  .. row 6

       -  __s32

       -  ``step``

       -  This field gives a step size for the control. See enum
	  :ref:`v4l2_ctrl_type <v4l2-ctrl-type>` how the step value is
	  to be used for each possible control type. Note that this an
	  unsigned 32-bit value.

	  Generally drivers should not scale hardware control values. It may
	  be necessary for example when the ``name`` or ``id`` imply a
	  particular unit and the hardware actually accepts only multiples
	  of said unit. If so, drivers must take care values are properly
	  rounded when scaling, such that errors will not accumulate on
	  repeated read-write cycles.

	  This field gives the smallest change of an integer control
	  actually affecting hardware. Often the information is needed when
	  the user can change controls by keyboard or GUI buttons, rather
	  than a slider. When for example a hardware register accepts values
	  0-511 and the driver reports 0-65535, step should be 128.

	  Note that although signed, the step value is supposed to be always
	  positive.

    -  .. row 7

       -  __s32

       -  ``default_value``

       -  The default value of a ``V4L2_CTRL_TYPE_INTEGER``, ``_BOOLEAN``,
	  ``_BITMASK``, ``_MENU`` or ``_INTEGER_MENU`` control. Not valid
	  for other types of controls.

	  .. note::

	     Drivers reset controls to their default value only when
	     the driver is first loaded, never afterwards.

    -  .. row 8

       -  __u32

       -  ``flags``

       -  Control flags, see :ref:`control-flags`.

    -  .. row 9

       -  __u32

       -  ``reserved``\ [2]

       -  Reserved for future extensions. Drivers must set the array to
	  zero.



.. _v4l2-query-ext-ctrl:

.. flat-table:: struct v4l2_query_ext_ctrl
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u32

       -  ``id``

       -  Identifies the control, set by the application. See
	  :ref:`control-id` for predefined IDs. When the ID is ORed with
	  ``V4L2_CTRL_FLAG_NEXT_CTRL`` the driver clears the flag and
	  returns the first non-compound control with a higher ID. When the
	  ID is ORed with ``V4L2_CTRL_FLAG_NEXT_COMPOUND`` the driver clears
	  the flag and returns the first compound control with a higher ID.
	  Set both to get the first control (compound or not) with a higher
	  ID.

    -  .. row 2

       -  __u32

       -  ``type``

       -  Type of control, see :ref:`v4l2-ctrl-type`.

    -  .. row 3

       -  char

       -  ``name``\ [32]

       -  Name of the control, a NUL-terminated ASCII string. This
	  information is intended for the user.

    -  .. row 4

       -  __s64

       -  ``minimum``

       -  Minimum value, inclusive. This field gives a lower bound for the
	  control. See enum :ref:`v4l2_ctrl_type <v4l2-ctrl-type>` how
	  the minimum value is to be used for each possible control type.
	  Note that this a signed 64-bit value.

    -  .. row 5

       -  __s64

       -  ``maximum``

       -  Maximum value, inclusive. This field gives an upper bound for the
	  control. See enum :ref:`v4l2_ctrl_type <v4l2-ctrl-type>` how
	  the maximum value is to be used for each possible control type.
	  Note that this a signed 64-bit value.

    -  .. row 6

       -  __u64

       -  ``step``

       -  This field gives a step size for the control. See enum
	  :ref:`v4l2_ctrl_type <v4l2-ctrl-type>` how the step value is
	  to be used for each possible control type. Note that this an
	  unsigned 64-bit value.

	  Generally drivers should not scale hardware control values. It may
	  be necessary for example when the ``name`` or ``id`` imply a
	  particular unit and the hardware actually accepts only multiples
	  of said unit. If so, drivers must take care values are properly
	  rounded when scaling, such that errors will not accumulate on
	  repeated read-write cycles.

	  This field gives the smallest change of an integer control
	  actually affecting hardware. Often the information is needed when
	  the user can change controls by keyboard or GUI buttons, rather
	  than a slider. When for example a hardware register accepts values
	  0-511 and the driver reports 0-65535, step should be 128.

    -  .. row 7

       -  __s64

       -  ``default_value``

       -  The default value of a ``V4L2_CTRL_TYPE_INTEGER``, ``_INTEGER64``,
	  ``_BOOLEAN``, ``_BITMASK``, ``_MENU``, ``_INTEGER_MENU``, ``_U8``
	  or ``_U16`` control. Not valid for other types of controls.

	  .. note::

	     Drivers reset controls to their default value only when
	     the driver is first loaded, never afterwards.

    -  .. row 8

       -  __u32

       -  ``flags``

       -  Control flags, see :ref:`control-flags`.

    -  .. row 9

       -  __u32

       -  ``elem_size``

       -  The size in bytes of a single element of the array. Given a char
	  pointer ``p`` to a 3-dimensional array you can find the position
	  of cell ``(z, y, x)`` as follows:
	  ``p + ((z * dims[1] + y) * dims[0] + x) * elem_size``.
	  ``elem_size`` is always valid, also when the control isn't an
	  array. For string controls ``elem_size`` is equal to
	  ``maximum + 1``.

    -  .. row 10

       -  __u32

       -  ``elems``

       -  The number of elements in the N-dimensional array. If this control
	  is not an array, then ``elems`` is 1. The ``elems`` field can
	  never be 0.

    -  .. row 11

       -  __u32

       -  ``nr_of_dims``

       -  The number of dimension in the N-dimensional array. If this
	  control is not an array, then this field is 0.

    -  .. row 12

       -  __u32

       -  ``dims[V4L2_CTRL_MAX_DIMS]``

       -  The size of each dimension. The first ``nr_of_dims`` elements of
	  this array must be non-zero, all remaining elements must be zero.

    -  .. row 13

       -  __u32

       -  ``reserved``\ [32]

       -  Reserved for future extensions. Applications and drivers must set
	  the array to zero.



.. _v4l2-querymenu:

.. flat-table:: struct v4l2_querymenu
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2 1


    -  .. row 1

       -  __u32

       -
       -  ``id``

       -  Identifies the control, set by the application from the respective
	  struct :ref:`v4l2_queryctrl <v4l2-queryctrl>` ``id``.

    -  .. row 2

       -  __u32

       -
       -  ``index``

       -  Index of the menu item, starting at zero, set by the application.

    -  .. row 3

       -  union

       -
       -
       -

    -  .. row 4

       -
       -  __u8

       -  ``name``\ [32]

       -  Name of the menu item, a NUL-terminated ASCII string. This
	  information is intended for the user. This field is valid for
	  ``V4L2_CTRL_FLAG_MENU`` type controls.

    -  .. row 5

       -
       -  __s64

       -  ``value``

       -  Value of the integer menu item. This field is valid for
	  ``V4L2_CTRL_FLAG_INTEGER_MENU`` type controls.

    -  .. row 6

       -  __u32

       -
       -  ``reserved``

       -  Reserved for future extensions. Drivers must set the array to
	  zero.



.. _v4l2-ctrl-type:

.. flat-table:: enum v4l2_ctrl_type
    :header-rows:  1
    :stub-columns: 0
    :widths:       30 5 5 5 55


    -  .. row 1

       -  Type

       -  ``minimum``

       -  ``step``

       -  ``maximum``

       -  Description

    -  .. row 2

       -  ``V4L2_CTRL_TYPE_INTEGER``

       -  any

       -  any

       -  any

       -  An integer-valued control ranging from minimum to maximum
	  inclusive. The step value indicates the increment between values.

    -  .. row 3

       -  ``V4L2_CTRL_TYPE_BOOLEAN``

       -  0

       -  1

       -  1

       -  A boolean-valued control. Zero corresponds to "disabled", and one
	  means "enabled".

    -  .. row 4

       -  ``V4L2_CTRL_TYPE_MENU``

       -  ≥ 0

       -  1

       -  N-1

       -  The control has a menu of N choices. The names of the menu items
	  can be enumerated with the ``VIDIOC_QUERYMENU`` ioctl.

    -  .. row 5

       -  ``V4L2_CTRL_TYPE_INTEGER_MENU``

       -  ≥ 0

       -  1

       -  N-1

       -  The control has a menu of N choices. The values of the menu items
	  can be enumerated with the ``VIDIOC_QUERYMENU`` ioctl. This is
	  similar to ``V4L2_CTRL_TYPE_MENU`` except that instead of strings,
	  the menu items are signed 64-bit integers.

    -  .. row 6

       -  ``V4L2_CTRL_TYPE_BITMASK``

       -  0

       -  n/a

       -  any

       -  A bitmask field. The maximum value is the set of bits that can be
	  used, all other bits are to be 0. The maximum value is interpreted
	  as a __u32, allowing the use of bit 31 in the bitmask.

    -  .. row 7

       -  ``V4L2_CTRL_TYPE_BUTTON``

       -  0

       -  0

       -  0

       -  A control which performs an action when set. Drivers must ignore
	  the value passed with ``VIDIOC_S_CTRL`` and return an ``EINVAL`` error
	  code on a ``VIDIOC_G_CTRL`` attempt.

    -  .. row 8

       -  ``V4L2_CTRL_TYPE_INTEGER64``

       -  any

       -  any

       -  any

       -  A 64-bit integer valued control. Minimum, maximum and step size
	  cannot be queried using ``VIDIOC_QUERYCTRL``. Only
	  ``VIDIOC_QUERY_EXT_CTRL`` can retrieve the 64-bit min/max/step
	  values, they should be interpreted as n/a when using
	  ``VIDIOC_QUERYCTRL``.

    -  .. row 9

       -  ``V4L2_CTRL_TYPE_STRING``

       -  ≥ 0

       -  ≥ 1

       -  ≥ 0

       -  The minimum and maximum string lengths. The step size means that
	  the string must be (minimum + N * step) characters long for N ≥ 0.
	  These lengths do not include the terminating zero, so in order to
	  pass a string of length 8 to
	  :ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` you need to
	  set the ``size`` field of struct
	  :ref:`v4l2_ext_control <v4l2-ext-control>` to 9. For
	  :ref:`VIDIOC_G_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` you can set
	  the ``size`` field to ``maximum`` + 1. Which character encoding is
	  used will depend on the string control itself and should be part
	  of the control documentation.

    -  .. row 10

       -  ``V4L2_CTRL_TYPE_CTRL_CLASS``

       -  n/a

       -  n/a

       -  n/a

       -  This is not a control. When ``VIDIOC_QUERYCTRL`` is called with a
	  control ID equal to a control class code (see :ref:`ctrl-class`)
	  + 1, the ioctl returns the name of the control class and this
	  control type. Older drivers which do not support this feature
	  return an ``EINVAL`` error code.

    -  .. row 11

       -  ``V4L2_CTRL_TYPE_U8``

       -  any

       -  any

       -  any

       -  An unsigned 8-bit valued control ranging from minimum to maximum
	  inclusive. The step value indicates the increment between values.

    -  .. row 12

       -  ``V4L2_CTRL_TYPE_U16``

       -  any

       -  any

       -  any

       -  An unsigned 16-bit valued control ranging from minimum to maximum
	  inclusive. The step value indicates the increment between values.

    -  .. row 13

       -  ``V4L2_CTRL_TYPE_U32``

       -  any

       -  any

       -  any

       -  An unsigned 32-bit valued control ranging from minimum to maximum
	  inclusive. The step value indicates the increment between values.



.. _control-flags:

.. flat-table:: Control Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_CTRL_FLAG_DISABLED``

       -  0x0001

       -  This control is permanently disabled and should be ignored by the
	  application. Any attempt to change the control will result in an
	  ``EINVAL`` error code.

    -  .. row 2

       -  ``V4L2_CTRL_FLAG_GRABBED``

       -  0x0002

       -  This control is temporarily unchangeable, for example because
	  another application took over control of the respective resource.
	  Such controls may be displayed specially in a user interface.
	  Attempts to change the control may result in an ``EBUSY`` error code.

    -  .. row 3

       -  ``V4L2_CTRL_FLAG_READ_ONLY``

       -  0x0004

       -  This control is permanently readable only. Any attempt to change
	  the control will result in an ``EINVAL`` error code.

    -  .. row 4

       -  ``V4L2_CTRL_FLAG_UPDATE``

       -  0x0008

       -  A hint that changing this control may affect the value of other
	  controls within the same control class. Applications should update
	  their user interface accordingly.

    -  .. row 5

       -  ``V4L2_CTRL_FLAG_INACTIVE``

       -  0x0010

       -  This control is not applicable to the current configuration and
	  should be displayed accordingly in a user interface. For example
	  the flag may be set on a MPEG audio level 2 bitrate control when
	  MPEG audio encoding level 1 was selected with another control.

    -  .. row 6

       -  ``V4L2_CTRL_FLAG_SLIDER``

       -  0x0020

       -  A hint that this control is best represented as a slider-like
	  element in a user interface.

    -  .. row 7

       -  ``V4L2_CTRL_FLAG_WRITE_ONLY``

       -  0x0040

       -  This control is permanently writable only. Any attempt to read the
	  control will result in an ``EACCES`` error code error code. This flag
	  is typically present for relative controls or action controls
	  where writing a value will cause the device to carry out a given
	  action (e. g. motor control) but no meaningful value can be
	  returned.

    -  .. row 8

       -  ``V4L2_CTRL_FLAG_VOLATILE``

       -  0x0080

       -  This control is volatile, which means that the value of the
	  control changes continuously. A typical example would be the
	  current gain value if the device is in auto-gain mode. In such a
	  case the hardware calculates the gain value based on the lighting
	  conditions which can change over time.

	  .. note::

	     Setting a new value for a volatile control will have no
	     effect and no ``V4L2_EVENT_CTRL_CH_VALUE`` will be sent, unless
	     the ``V4L2_CTRL_FLAG_EXECUTE_ON_WRITE`` flag (see below) is
	     also set. Otherwise the new value will just be ignored.

    -  .. row 9

       -  ``V4L2_CTRL_FLAG_HAS_PAYLOAD``

       -  0x0100

       -  This control has a pointer type, so its value has to be accessed
	  using one of the pointer fields of struct
	  :ref:`v4l2_ext_control <v4l2-ext-control>`. This flag is set
	  for controls that are an array, string, or have a compound type.
	  In all cases you have to set a pointer to memory containing the
	  payload of the control.

    -  .. row 10

       -  ``V4L2_CTRL_FLAG_EXECUTE_ON_WRITE``

       -  0x0200

       -  The value provided to the control will be propagated to the driver
	  even if it remains constant. This is required when the control
	  represents an action on the hardware. For example: clearing an
	  error flag or triggering the flash. All the controls of the type
	  ``V4L2_CTRL_TYPE_BUTTON`` have this flag set.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :ref:`v4l2_queryctrl <v4l2-queryctrl>` ``id`` is
    invalid. The struct :ref:`v4l2_querymenu <v4l2-querymenu>` ``id``
    is invalid or ``index`` is out of range (less than ``minimum`` or
    greater than ``maximum``) or this particular menu item is not
    supported by the driver.

EACCES
    An attempt was made to read a write-only control.

.. [#f1]
   ``V4L2_CTRL_FLAG_DISABLED`` was intended for two purposes: Drivers
   can skip predefined controls not supported by the hardware (although
   returning ``EINVAL`` would do as well), or disable predefined and private
   controls after hardware detection without the trouble of reordering
   control arrays and indices (``EINVAL`` cannot be used to skip private
   controls because it would prematurely end the enumeration).
