.. -*- coding: utf-8; mode: rst -*-

.. _VIDIOC_G_EXT_CTRLS:

******************************************************************
ioctl VIDIOC_G_EXT_CTRLS, VIDIOC_S_EXT_CTRLS, VIDIOC_TRY_EXT_CTRLS
******************************************************************

Name
====

VIDIOC_G_EXT_CTRLS - VIDIOC_S_EXT_CTRLS - VIDIOC_TRY_EXT_CTRLS - Get or set the value of several controls, try control values


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct v4l2_ext_controls *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <func-open>`.

``request``
    VIDIOC_G_EXT_CTRLS, VIDIOC_S_EXT_CTRLS,
    VIDIOC_TRY_EXT_CTRLS

``argp``


Description
===========

These ioctls allow the caller to get or set multiple controls
atomically. Control IDs are grouped into control classes (see
:ref:`ctrl-class`) and all controls in the control array must belong
to the same control class.

Applications must always fill in the ``count``, ``which``, ``controls``
and ``reserved`` fields of struct
:ref:`v4l2_ext_controls <v4l2-ext-controls>`, and initialize the
struct :ref:`v4l2_ext_control <v4l2-ext-control>` array pointed to
by the ``controls`` fields.

To get the current value of a set of controls applications initialize
the ``id``, ``size`` and ``reserved2`` fields of each struct
:ref:`v4l2_ext_control <v4l2-ext-control>` and call the
:ref:`VIDIOC_G_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` ioctl. String controls controls must also set the
``string`` field. Controls of compound types
(``V4L2_CTRL_FLAG_HAS_PAYLOAD`` is set) must set the ``ptr`` field.

If the ``size`` is too small to receive the control result (only
relevant for pointer-type controls like strings), then the driver will
set ``size`` to a valid value and return an ``ENOSPC`` error code. You
should re-allocate the memory to this new size and try again. For the
string type it is possible that the same issue occurs again if the
string has grown in the meantime. It is recommended to call
:ref:`VIDIOC_QUERYCTRL` first and use
``maximum``\ +1 as the new ``size`` value. It is guaranteed that that is
sufficient memory.

N-dimensional arrays are set and retrieved row-by-row. You cannot set a
partial array, all elements have to be set or retrieved. The total size
is calculated as ``elems`` * ``elem_size``. These values can be obtained
by calling :ref:`VIDIOC_QUERY_EXT_CTRL <VIDIOC_QUERYCTRL>`.

To change the value of a set of controls applications initialize the
``id``, ``size``, ``reserved2`` and ``value/value64/string/ptr`` fields
of each struct :ref:`v4l2_ext_control <v4l2-ext-control>` and call
the :ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` ioctl. The controls will only be set if *all*
control values are valid.

To check if a set of controls have correct values applications
initialize the ``id``, ``size``, ``reserved2`` and
``value/value64/string/ptr`` fields of each struct
:ref:`v4l2_ext_control <v4l2-ext-control>` and call the
:ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` ioctl. It is up to the driver whether wrong
values are automatically adjusted to a valid value or if an error is
returned.

When the ``id`` or ``which`` is invalid drivers return an ``EINVAL`` error
code. When the value is out of bounds drivers can choose to take the
closest valid value or return an ``ERANGE`` error code, whatever seems more
appropriate. In the first case the new value is set in struct
:ref:`v4l2_ext_control <v4l2-ext-control>`. If the new control value
is inappropriate (e.g. the given menu index is not supported by the menu
control), then this will also result in an ``EINVAL`` error code error.

The driver will only set/get these controls if all control values are
correct. This prevents the situation where only some of the controls
were set/get. Only low-level errors (e. g. a failed i2c command) can
still cause this situation.


.. _v4l2-ext-control:

.. flat-table:: struct v4l2_ext_control
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 1 2


    -  .. row 1

       -  __u32

       -  ``id``

       -
       -  Identifies the control, set by the application.

    -  .. row 2

       -  __u32

       -  ``size``

       -
       -  The total size in bytes of the payload of this control. This is
	  normally 0, but for pointer controls this should be set to the
	  size of the memory containing the payload, or that will receive
	  the payload. If :ref:`VIDIOC_G_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` finds that this value is
	  less than is required to store the payload result, then it is set
	  to a value large enough to store the payload result and ``ENOSPC`` is
	  returned.

	  .. note:: For string controls, this ``size`` field should
	     not be confused with the length of the string. This field refers
	     to the size of the memory that contains the string. The actual
	     *length* of the string may well be much smaller.

    -  .. row 3

       -  __u32

       -  ``reserved2``\ [1]

       -
       -  Reserved for future extensions. Drivers and applications must set
	  the array to zero.

    -  .. row 4

       -  union

       -  (anonymous)

    -  .. row 5

       -
       -  __s32

       -  ``value``

       -  New value or current value. Valid if this control is not of type
	  ``V4L2_CTRL_TYPE_INTEGER64`` and ``V4L2_CTRL_FLAG_HAS_PAYLOAD`` is
	  not set.

    -  .. row 6

       -
       -  __s64

       -  ``value64``

       -  New value or current value. Valid if this control is of type
	  ``V4L2_CTRL_TYPE_INTEGER64`` and ``V4L2_CTRL_FLAG_HAS_PAYLOAD`` is
	  not set.

    -  .. row 7

       -
       -  char *

       -  ``string``

       -  A pointer to a string. Valid if this control is of type
	  ``V4L2_CTRL_TYPE_STRING``.

    -  .. row 8

       -
       -  __u8 *

       -  ``p_u8``

       -  A pointer to a matrix control of unsigned 8-bit values. Valid if
	  this control is of type ``V4L2_CTRL_TYPE_U8``.

    -  .. row 9

       -
       -  __u16 *

       -  ``p_u16``

       -  A pointer to a matrix control of unsigned 16-bit values. Valid if
	  this control is of type ``V4L2_CTRL_TYPE_U16``.

    -  .. row 10

       -
       -  __u32 *

       -  ``p_u32``

       -  A pointer to a matrix control of unsigned 32-bit values. Valid if
	  this control is of type ``V4L2_CTRL_TYPE_U32``.

    -  .. row 11

       -
       -  void *

       -  ``ptr``

       -  A pointer to a compound type which can be an N-dimensional array
	  and/or a compound type (the control's type is >=
	  ``V4L2_CTRL_COMPOUND_TYPES``). Valid if
	  ``V4L2_CTRL_FLAG_HAS_PAYLOAD`` is set for this control.



.. _v4l2-ext-controls:

.. flat-table:: struct v4l2_ext_controls
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2 1


    -  .. row 1

       -  union

       -  (anonymous)

    -  .. row 2

       -
       -  __u32

       -  ``ctrl_class``

       -  The control class to which all controls belong, see
	  :ref:`ctrl-class`. Drivers that use a kernel framework for
	  handling controls will also accept a value of 0 here, meaning that
	  the controls can belong to any control class. Whether drivers
	  support this can be tested by setting ``ctrl_class`` to 0 and
	  calling :ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` with a ``count`` of 0. If that
	  succeeds, then the driver supports this feature.

    -  .. row 3

       -
       -  __u32

       -  ``which``

       -  Which value of the control to get/set/try.
	  ``V4L2_CTRL_WHICH_CUR_VAL`` will return the current value of the
	  control and ``V4L2_CTRL_WHICH_DEF_VAL`` will return the default
	  value of the control.

	  .. note:: You can only get the default value of the control,
	     you cannot set or try it.

	  For backwards compatibility you can also use a control class here
	  (see :ref:`ctrl-class`). In that case all controls have to
	  belong to that control class. This usage is deprecated, instead
	  just use ``V4L2_CTRL_WHICH_CUR_VAL``. There are some very old
	  drivers that do not yet support ``V4L2_CTRL_WHICH_CUR_VAL`` and
	  that require a control class here. You can test for such drivers
	  by setting ctrl_class to ``V4L2_CTRL_WHICH_CUR_VAL`` and calling
	  VIDIOC_TRY_EXT_CTRLS with a count of 0. If that fails, then the
	  driver does not support ``V4L2_CTRL_WHICH_CUR_VAL``.

    -  .. row 4

       -  __u32

       -  ``count``

       -  The number of controls in the controls array. May also be zero.

    -  .. row 5

       -  __u32

       -  ``error_idx``

       -  Set by the driver in case of an error. If the error is associated
	  with a particular control, then ``error_idx`` is set to the index
	  of that control. If the error is not related to a specific
	  control, or the validation step failed (see below), then
	  ``error_idx`` is set to ``count``. The value is undefined if the
	  ioctl returned 0 (success).

	  Before controls are read from/written to hardware a validation
	  step takes place: this checks if all controls in the list are
	  valid controls, if no attempt is made to write to a read-only
	  control or read from a write-only control, and any other up-front
	  checks that can be done without accessing the hardware. The exact
	  validations done during this step are driver dependent since some
	  checks might require hardware access for some devices, thus making
	  it impossible to do those checks up-front. However, drivers should
	  make a best-effort to do as many up-front checks as possible.

	  This check is done to avoid leaving the hardware in an
	  inconsistent state due to easy-to-avoid problems. But it leads to
	  another problem: the application needs to know whether an error
	  came from the validation step (meaning that the hardware was not
	  touched) or from an error during the actual reading from/writing
	  to hardware.

	  The, in hindsight quite poor, solution for that is to set
	  ``error_idx`` to ``count`` if the validation failed. This has the
	  unfortunate side-effect that it is not possible to see which
	  control failed the validation. If the validation was successful
	  and the error happened while accessing the hardware, then
	  ``error_idx`` is less than ``count`` and only the controls up to
	  ``error_idx-1`` were read or written correctly, and the state of
	  the remaining controls is undefined.

	  Since :ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` does not access hardware there is
	  also no need to handle the validation step in this special way, so
	  ``error_idx`` will just be set to the control that failed the
	  validation step instead of to ``count``. This means that if
	  :ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` fails with ``error_idx`` set to ``count``,
	  then you can call :ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` to try to discover the
	  actual control that failed the validation step. Unfortunately,
	  there is no ``TRY`` equivalent for :ref:`VIDIOC_G_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>`.

    -  .. row 6

       -  __u32

       -  ``reserved``\ [2]

       -  Reserved for future extensions. Drivers and applications must set
	  the array to zero.

    -  .. row 7

       -  struct :ref:`v4l2_ext_control <v4l2-ext-control>` *

       -  ``controls``

       -  Pointer to an array of ``count`` v4l2_ext_control structures.
	  Ignored if ``count`` equals zero.



.. _ctrl-class:

.. flat-table:: Control classes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``V4L2_CTRL_CLASS_USER``

       -  0x980000

       -  The class containing user controls. These controls are described
	  in :ref:`control`. All controls that can be set using the
	  :ref:`VIDIOC_S_CTRL <VIDIOC_G_CTRL>` and
	  :ref:`VIDIOC_G_CTRL <VIDIOC_G_CTRL>` ioctl belong to this
	  class.

    -  .. row 2

       -  ``V4L2_CTRL_CLASS_MPEG``

       -  0x990000

       -  The class containing MPEG compression controls. These controls are
	  described in :ref:`mpeg-controls`.

    -  .. row 3

       -  ``V4L2_CTRL_CLASS_CAMERA``

       -  0x9a0000

       -  The class containing camera controls. These controls are described
	  in :ref:`camera-controls`.

    -  .. row 4

       -  ``V4L2_CTRL_CLASS_FM_TX``

       -  0x9b0000

       -  The class containing FM Transmitter (FM TX) controls. These
	  controls are described in :ref:`fm-tx-controls`.

    -  .. row 5

       -  ``V4L2_CTRL_CLASS_FLASH``

       -  0x9c0000

       -  The class containing flash device controls. These controls are
	  described in :ref:`flash-controls`.

    -  .. row 6

       -  ``V4L2_CTRL_CLASS_JPEG``

       -  0x9d0000

       -  The class containing JPEG compression controls. These controls are
	  described in :ref:`jpeg-controls`.

    -  .. row 7

       -  ``V4L2_CTRL_CLASS_IMAGE_SOURCE``

       -  0x9e0000

       -  The class containing image source controls. These controls are
	  described in :ref:`image-source-controls`.

    -  .. row 8

       -  ``V4L2_CTRL_CLASS_IMAGE_PROC``

       -  0x9f0000

       -  The class containing image processing controls. These controls are
	  described in :ref:`image-process-controls`.

    -  .. row 9

       -  ``V4L2_CTRL_CLASS_FM_RX``

       -  0xa10000

       -  The class containing FM Receiver (FM RX) controls. These controls
	  are described in :ref:`fm-rx-controls`.

    -  .. row 10

       -  ``V4L2_CTRL_CLASS_RF_TUNER``

       -  0xa20000

       -  The class containing RF tuner controls. These controls are
	  described in :ref:`rf-tuner-controls`.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

EINVAL
    The struct :ref:`v4l2_ext_control <v4l2-ext-control>` ``id`` is
    invalid, the struct :ref:`v4l2_ext_controls <v4l2-ext-controls>`
    ``which`` is invalid, or the struct
    :ref:`v4l2_ext_control <v4l2-ext-control>` ``value`` was
    inappropriate (e.g. the given menu index is not supported by the
    driver). This error code is also returned by the
    :ref:`VIDIOC_S_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` and :ref:`VIDIOC_TRY_EXT_CTRLS <VIDIOC_G_EXT_CTRLS>` ioctls if two or
    more control values are in conflict.

ERANGE
    The struct :ref:`v4l2_ext_control <v4l2-ext-control>` ``value``
    is out of bounds.

EBUSY
    The control is temporarily not changeable, possibly because another
    applications took over control of the device function this control
    belongs to.

ENOSPC
    The space reserved for the control's payload is insufficient. The
    field ``size`` is set to a value that is enough to store the payload
    and this error code is returned.

EACCES
    Attempt to try or set a read-only control or to get a write-only
    control.
