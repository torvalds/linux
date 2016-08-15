.. -*- coding: utf-8; mode: rst -*-

.. _CEC_ADAP_LOG_ADDRS:
.. _CEC_ADAP_G_LOG_ADDRS:
.. _CEC_ADAP_S_LOG_ADDRS:

****************************************************
ioctls CEC_ADAP_G_LOG_ADDRS and CEC_ADAP_S_LOG_ADDRS
****************************************************

Name
====

CEC_ADAP_G_LOG_ADDRS, CEC_ADAP_S_LOG_ADDRS - Get or set the logical addresses


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct cec_log_addrs *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <cec-func-open>`.

``request``
    CEC_ADAP_G_LOG_ADDRS, CEC_ADAP_S_LOG_ADDRS

``argp``


Description
===========

.. note::

   This documents the proposed CEC API. This API is not yet finalized
   and is currently only available as a staging kernel module.

To query the current CEC logical addresses, applications call
:ref:`ioctl CEC_ADAP_G_LOG_ADDRS <CEC_ADAP_G_LOG_ADDRS>` with a pointer to a
:c:type:`struct cec_log_addrs` where the driver stores the logical addresses.

To set new logical addresses, applications fill in
:c:type:`struct cec_log_addrs` and call :ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`
with a pointer to this struct. The :ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`
is only available if ``CEC_CAP_LOG_ADDRS`` is set (the ``ENOTTY`` error code is
returned otherwise). The :ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`
can only be called by a file descriptor in initiator mode (see :ref:`CEC_S_MODE`), if not
the ``EBUSY`` error code will be returned.

To clear existing logical addresses set ``num_log_addrs`` to 0. All other fields
will be ignored in that case. The adapter will go to the unconfigured state.

If the physical address is valid (see :ref:`ioctl CEC_ADAP_S_PHYS_ADDR <CEC_ADAP_S_PHYS_ADDR>`),
then this ioctl will block until all requested logical
addresses have been claimed. If the file descriptor is in non-blocking mode then it will
not wait for the logical addresses to be claimed, instead it just returns 0.

A :ref:`CEC_EVENT_STATE_CHANGE <CEC-EVENT-STATE-CHANGE>` event is sent when the
logical addresses are claimed or cleared.

Attempting to call :ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>` when
logical address types are already defined will return with error ``EBUSY``.


.. _cec-log-addrs:

.. flat-table:: struct cec_log_addrs
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 16


    -  .. row 1

       -  __u8

       -  ``log_addr[CEC_MAX_LOG_ADDRS]``

       -  The actual logical addresses that were claimed. This is set by the
	  driver. If no logical address could be claimed, then it is set to
	  ``CEC_LOG_ADDR_INVALID``. If this adapter is Unregistered, then
	  ``log_addr[0]`` is set to 0xf and all others to
	  ``CEC_LOG_ADDR_INVALID``.

    -  .. row 2

       -  __u16

       -  ``log_addr_mask``

       -  The bitmask of all logical addresses this adapter has claimed. If
	  this adapter is Unregistered then ``log_addr_mask`` sets bit 15
	  and clears all other bits. If this adapter is not configured at
	  all, then ``log_addr_mask`` is set to 0. Set by the driver.

    -  .. row 3

       -  __u8

       -  ``cec_version``

       -  The CEC version that this adapter shall use. See
	  :ref:`cec-versions`. Used to implement the
	  ``CEC_MSG_CEC_VERSION`` and ``CEC_MSG_REPORT_FEATURES`` messages.
	  Note that :ref:`CEC_OP_CEC_VERSION_1_3A <CEC-OP-CEC-VERSION-1-3A>` is not allowed by the CEC
	  framework.

    -  .. row 4

       -  __u8

       -  ``num_log_addrs``

       -  Number of logical addresses to set up. Must be ≤
	  ``available_log_addrs`` as returned by
	  :ref:`CEC_ADAP_G_CAPS`. All arrays in
	  this structure are only filled up to index
	  ``available_log_addrs``-1. The remaining array elements will be
	  ignored. Note that the CEC 2.0 standard allows for a maximum of 2
	  logical addresses, although some hardware has support for more.
	  ``CEC_MAX_LOG_ADDRS`` is 4. The driver will return the actual
	  number of logical addresses it could claim, which may be less than
	  what was requested. If this field is set to 0, then the CEC
	  adapter shall clear all claimed logical addresses and all other
	  fields will be ignored.

    -  .. row 5

       -  __u32

       -  ``vendor_id``

       -  The vendor ID is a 24-bit number that identifies the specific
	  vendor or entity. Based on this ID vendor specific commands may be
	  defined. If you do not want a vendor ID then set it to
	  ``CEC_VENDOR_ID_NONE``.

    -  .. row 6

       -  __u32

       -  ``flags``

       -  Flags. No flags are defined yet, so set this to 0.

    -  .. row 7

       -  char

       -  ``osd_name[15]``

       -  The On-Screen Display name as is returned by the
	  ``CEC_MSG_SET_OSD_NAME`` message.

    -  .. row 8

       -  __u8

       -  ``primary_device_type[CEC_MAX_LOG_ADDRS]``

       -  Primary device type for each logical address. See
	  :ref:`cec-prim-dev-types` for possible types.

    -  .. row 9

       -  __u8

       -  ``log_addr_type[CEC_MAX_LOG_ADDRS]``

       -  Logical address types. See :ref:`cec-log-addr-types` for
	  possible types. The driver will update this with the actual
	  logical address type that it claimed (e.g. it may have to fallback
	  to :ref:`CEC_LOG_ADDR_TYPE_UNREGISTERED <CEC-LOG-ADDR-TYPE-UNREGISTERED>`).

    -  .. row 10

       -  __u8

       -  ``all_device_types[CEC_MAX_LOG_ADDRS]``

       -  CEC 2.0 specific: the bit mask of all device types. See
	  :ref:`cec-all-dev-types-flags`. It is used in the CEC 2.0
	  ``CEC_MSG_REPORT_FEATURES`` message. For CEC 1.4 you can either leave
	  this field to 0, or fill it in according to the CEC 2.0 guidelines to
	  give the CEC framework more information about the device type, even
	  though the framework won't use it directly in the CEC message.

    -  .. row 11

       -  __u8

       -  ``features[CEC_MAX_LOG_ADDRS][12]``

       -  Features for each logical address. It is used in the CEC 2.0
	  ``CEC_MSG_REPORT_FEATURES`` message. The 12 bytes include both the
	  RC Profile and the Device Features. For CEC 1.4 you can either leave
          this field to all 0, or fill it in according to the CEC 2.0 guidelines to
          give the CEC framework more information about the device type, even
          though the framework won't use it directly in the CEC message.

.. _cec-versions:

.. flat-table:: CEC Versions
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. _`CEC-OP-CEC-VERSION-1-3A`:

       -  ``CEC_OP_CEC_VERSION_1_3A``

       -  4

       -  CEC version according to the HDMI 1.3a standard.

    -  .. _`CEC-OP-CEC-VERSION-1-4B`:

       -  ``CEC_OP_CEC_VERSION_1_4B``

       -  5

       -  CEC version according to the HDMI 1.4b standard.

    -  .. _`CEC-OP-CEC-VERSION-2-0`:

       -  ``CEC_OP_CEC_VERSION_2_0``

       -  6

       -  CEC version according to the HDMI 2.0 standard.



.. _cec-prim-dev-types:

.. flat-table:: CEC Primary Device Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. _`CEC-OP-PRIM-DEVTYPE-TV`:

       -  ``CEC_OP_PRIM_DEVTYPE_TV``

       -  0

       -  Use for a TV.

    -  .. _`CEC-OP-PRIM-DEVTYPE-RECORD`:

       -  ``CEC_OP_PRIM_DEVTYPE_RECORD``

       -  1

       -  Use for a recording device.

    -  .. _`CEC-OP-PRIM-DEVTYPE-TUNER`:

       -  ``CEC_OP_PRIM_DEVTYPE_TUNER``

       -  3

       -  Use for a device with a tuner.

    -  .. _`CEC-OP-PRIM-DEVTYPE-PLAYBACK`:

       -  ``CEC_OP_PRIM_DEVTYPE_PLAYBACK``

       -  4

       -  Use for a playback device.

    -  .. _`CEC-OP-PRIM-DEVTYPE-AUDIOSYSTEM`:

       -  ``CEC_OP_PRIM_DEVTYPE_AUDIOSYSTEM``

       -  5

       -  Use for an audio system (e.g. an audio/video receiver).

    -  .. _`CEC-OP-PRIM-DEVTYPE-SWITCH`:

       -  ``CEC_OP_PRIM_DEVTYPE_SWITCH``

       -  6

       -  Use for a CEC switch.

    -  .. _`CEC-OP-PRIM-DEVTYPE-VIDEOPROC`:

       -  ``CEC_OP_PRIM_DEVTYPE_VIDEOPROC``

       -  7

       -  Use for a video processor device.



.. _cec-log-addr-types:

.. flat-table:: CEC Logical Address Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 16


    -  .. _`CEC-LOG-ADDR-TYPE-TV`:

       -  ``CEC_LOG_ADDR_TYPE_TV``

       -  0

       -  Use for a TV.

    -  .. _`CEC-LOG-ADDR-TYPE-RECORD`:

       -  ``CEC_LOG_ADDR_TYPE_RECORD``

       -  1

       -  Use for a recording device.

    -  .. _`CEC-LOG-ADDR-TYPE-TUNER`:

       -  ``CEC_LOG_ADDR_TYPE_TUNER``

       -  2

       -  Use for a tuner device.

    -  .. _`CEC-LOG-ADDR-TYPE-PLAYBACK`:

       -  ``CEC_LOG_ADDR_TYPE_PLAYBACK``

       -  3

       -  Use for a playback device.

    -  .. _`CEC-LOG-ADDR-TYPE-AUDIOSYSTEM`:

       -  ``CEC_LOG_ADDR_TYPE_AUDIOSYSTEM``

       -  4

       -  Use for an audio system device.

    -  .. _`CEC-LOG-ADDR-TYPE-SPECIFIC`:

       -  ``CEC_LOG_ADDR_TYPE_SPECIFIC``

       -  5

       -  Use for a second TV or for a video processor device.

    -  .. _`CEC-LOG-ADDR-TYPE-UNREGISTERED`:

       -  ``CEC_LOG_ADDR_TYPE_UNREGISTERED``

       -  6

       -  Use this if you just want to remain unregistered. Used for pure
	  CEC switches or CDC-only devices (CDC: Capability Discovery and
	  Control).



.. _cec-all-dev-types-flags:

.. flat-table:: CEC All Device Types Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. _`CEC-OP-ALL-DEVTYPE-TV`:

       -  ``CEC_OP_ALL_DEVTYPE_TV``

       -  0x80

       -  This supports the TV type.

    -  .. _`CEC-OP-ALL-DEVTYPE-RECORD`:

       -  ``CEC_OP_ALL_DEVTYPE_RECORD``

       -  0x40

       -  This supports the Recording type.

    -  .. _`CEC-OP-ALL-DEVTYPE-TUNER`:

       -  ``CEC_OP_ALL_DEVTYPE_TUNER``

       -  0x20

       -  This supports the Tuner type.

    -  .. _`CEC-OP-ALL-DEVTYPE-PLAYBACK`:

       -  ``CEC_OP_ALL_DEVTYPE_PLAYBACK``

       -  0x10

       -  This supports the Playback type.

    -  .. _`CEC-OP-ALL-DEVTYPE-AUDIOSYSTEM`:

       -  ``CEC_OP_ALL_DEVTYPE_AUDIOSYSTEM``

       -  0x08

       -  This supports the Audio System type.

    -  .. _`CEC-OP-ALL-DEVTYPE-SWITCH`:

       -  ``CEC_OP_ALL_DEVTYPE_SWITCH``

       -  0x04

       -  This supports the CEC Switch or Video Processing type.



Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

