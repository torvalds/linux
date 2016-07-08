.. -*- coding: utf-8; mode: rst -*-

.. _cec-ioc-adap-g-log-addrs:

************************************************
ioctl CEC_ADAP_G_LOG_ADDRS, CEC_ADAP_S_LOG_ADDRS
************************************************

*man CEC_ADAP_G_LOG_ADDRS(2)*

CEC_ADAP_S_LOG_ADDRS
Get or set the logical addresses


Synopsis
========

.. c:function:: int ioctl( int fd, int request, struct cec_log_addrs *argp )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <cec-func-open>`.

``request``
    CEC_ADAP_G_LOG_ADDRS, CEC_ADAP_S_LOG_ADDRS

``argp``


Description
===========

Note: this documents the proposed CEC API. This API is not yet finalized
and is currently only available as a staging kernel module.

To query the current CEC logical addresses, applications call the
``CEC_ADAP_G_LOG_ADDRS`` ioctl with a pointer to a
:c:type:`struct cec_log_addrs` structure where the drivers stores
the logical addresses.

To set new logical addresses, applications fill in struct
:c:type:`struct cec_log_addrs` and call the ``CEC_ADAP_S_LOG_ADDRS``
ioctl with a pointer to this struct. The ``CEC_ADAP_S_LOG_ADDRS`` ioctl
is only available if ``CEC_CAP_LOG_ADDRS`` is set (ENOTTY error code is
returned otherwise). This ioctl will block until all requested logical
addresses have been claimed. ``CEC_ADAP_S_LOG_ADDRS`` can only be called
by a file handle in initiator mode (see
:ref:`CEC_S_MODE <cec-ioc-g-mode>`).


.. _cec-log-addrs:

.. flat-table:: struct cec_log_addrs
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u8

       -  ``log_addr`` [CEC_MAX_LOG_ADDRS]

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
          Note that ``CEC_OP_CEC_VERSION_1_3A`` is not allowed by the CEC
          framework.

    -  .. row 4

       -  __u8

       -  ``num_log_addrs``

       -  Number of logical addresses to set up. Must be ≤
          ``available_log_addrs`` as returned by
          :ref:`CEC_ADAP_G_CAPS <cec-ioc-adap-g-caps>`. All arrays in
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

       -  ``osd_name``\ [15]

       -  The On-Screen Display name as is returned by the
          ``CEC_MSG_SET_OSD_NAME`` message.

    -  .. row 8

       -  __u8

       -  ``primary_device_type`` [CEC_MAX_LOG_ADDRS]

       -  Primary device type for each logical address. See
          :ref:`cec-prim-dev-types` for possible types.

    -  .. row 9

       -  __u8

       -  ``log_addr_type`` [CEC_MAX_LOG_ADDRS]

       -  Logical address types. See :ref:`cec-log-addr-types` for
          possible types. The driver will update this with the actual
          logical address type that it claimed (e.g. it may have to fallback
          to ``CEC_LOG_ADDR_TYPE_UNREGISTERED``).

    -  .. row 10

       -  __u8

       -  ``all_device_types`` [CEC_MAX_LOG_ADDRS]

       -  CEC 2.0 specific: all device types. See
          :ref:`cec-all-dev-types-flags`. Used to implement the
          ``CEC_MSG_REPORT_FEATURES`` message. This field is ignored if
          ``cec_version`` < ``CEC_OP_CEC_VERSION_2_0``.

    -  .. row 11

       -  __u8

       -  ``features`` [CEC_MAX_LOG_ADDRS][12]

       -  Features for each logical address. Used to implement the
          ``CEC_MSG_REPORT_FEATURES`` message. The 12 bytes include both the
          RC Profile and the Device Features. This field is ignored if
          ``cec_version`` < ``CEC_OP_CEC_VERSION_2_0``.



.. _cec-versions:

.. flat-table:: CEC Versions
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``CEC_OP_CEC_VERSION_1_3A``

       -  4

       -  CEC version according to the HDMI 1.3a standard.

    -  .. row 2

       -  ``CEC_OP_CEC_VERSION_1_4B``

       -  5

       -  CEC version according to the HDMI 1.4b standard.

    -  .. row 3

       -  ``CEC_OP_CEC_VERSION_2_0``

       -  6

       -  CEC version according to the HDMI 2.0 standard.



.. _cec-prim-dev-types:

.. flat-table:: CEC Primary Device Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``CEC_OP_PRIM_DEVTYPE_TV``

       -  0

       -  Use for a TV.

    -  .. row 2

       -  ``CEC_OP_PRIM_DEVTYPE_RECORD``

       -  1

       -  Use for a recording device.

    -  .. row 3

       -  ``CEC_OP_PRIM_DEVTYPE_TUNER``

       -  3

       -  Use for a device with a tuner.

    -  .. row 4

       -  ``CEC_OP_PRIM_DEVTYPE_PLAYBACK``

       -  4

       -  Use for a playback device.

    -  .. row 5

       -  ``CEC_OP_PRIM_DEVTYPE_AUDIOSYSTEM``

       -  5

       -  Use for an audio system (e.g. an audio/video receiver).

    -  .. row 6

       -  ``CEC_OP_PRIM_DEVTYPE_SWITCH``

       -  6

       -  Use for a CEC switch.

    -  .. row 7

       -  ``CEC_OP_PRIM_DEVTYPE_VIDEOPROC``

       -  7

       -  Use for a video processor device.



.. _cec-log-addr-types:

.. flat-table:: CEC Logical Address Types
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``CEC_LOG_ADDR_TYPE_TV``

       -  0

       -  Use for a TV.

    -  .. row 2

       -  ``CEC_LOG_ADDR_TYPE_RECORD``

       -  1

       -  Use for a recording device.

    -  .. row 3

       -  ``CEC_LOG_ADDR_TYPE_TUNER``

       -  2

       -  Use for a tuner device.

    -  .. row 4

       -  ``CEC_LOG_ADDR_TYPE_PLAYBACK``

       -  3

       -  Use for a playback device.

    -  .. row 5

       -  ``CEC_LOG_ADDR_TYPE_AUDIOSYSTEM``

       -  4

       -  Use for an audio system device.

    -  .. row 6

       -  ``CEC_LOG_ADDR_TYPE_SPECIFIC``

       -  5

       -  Use for a second TV or for a video processor device.

    -  .. row 7

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


    -  .. row 1

       -  ``CEC_OP_ALL_DEVTYPE_TV``

       -  0x80

       -  This supports the TV type.

    -  .. row 2

       -  ``CEC_OP_ALL_DEVTYPE_RECORD``

       -  0x40

       -  This supports the Recording type.

    -  .. row 3

       -  ``CEC_OP_ALL_DEVTYPE_TUNER``

       -  0x20

       -  This supports the Tuner type.

    -  .. row 4

       -  ``CEC_OP_ALL_DEVTYPE_PLAYBACK``

       -  0x10

       -  This supports the Playback type.

    -  .. row 5

       -  ``CEC_OP_ALL_DEVTYPE_AUDIOSYSTEM``

       -  0x08

       -  This supports the Audio System type.

    -  .. row 6

       -  ``CEC_OP_ALL_DEVTYPE_SWITCH``

       -  0x04

       -  This supports the CEC Switch or Video Processing type.



Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
