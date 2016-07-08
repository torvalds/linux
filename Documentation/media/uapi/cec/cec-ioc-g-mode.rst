.. -*- coding: utf-8; mode: rst -*-

.. _cec-ioc-g-mode:

****************************
ioctl CEC_G_MODE, CEC_S_MODE
****************************

*man CEC_G_MODE(2)*

CEC_S_MODE
Get or set exclusive use of the CEC adapter


Synopsis
========

.. c:function:: int ioctl( int fd, int request, __u32 *argp )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <cec-func-open>`.

``request``
    CEC_G_MODE, CEC_S_MODE

``argp``


Description
===========

Note: this documents the proposed CEC API. This API is not yet finalized
and is currently only available as a staging kernel module.

By default any filehandle can use
:ref:`CEC_TRANSMIT <cec-ioc-receive>` and
:ref:`CEC_RECEIVE <cec-ioc-receive>`, but in order to prevent
applications from stepping on each others toes it must be possible to
obtain exclusive access to the CEC adapter. This ioctl sets the
filehandle to initiator and/or follower mode which can be exclusive
depending on the chosen mode. The initiator is the filehandle that is
used to initiate messages, i.e. it commands other CEC devices. The
follower is the filehandle that receives messages sent to the CEC
adapter and processes them. The same filehandle can be both initiator
and follower, or this role can be taken by two different filehandles.

When a CEC message is received, then the CEC framework will decide how
it will be processed. If the message is a reply to an earlier
transmitted message, then the reply is sent back to the filehandle that
is waiting for it. In addition the CEC framework will process it.

If the message is not a reply, then the CEC framework will process it
first. If there is no follower, then the message is just discarded and a
feature abort is sent back to the initiator if the framework couldn't
process it. If there is a follower, then the message is passed on to the
follower who will use :ref:`CEC_RECEIVE <cec-ioc-receive>` to dequeue
the new message. The framework expects the follower to make the right
decisions.

The CEC framework will process core messages unless requested otherwise
by the follower. The follower can enable the passthrough mode. In that
case, the CEC framework will pass on most core messages without
processing them and the follower will have to implement those messages.
There are some messages that the core will always process, regardless of
the passthrough mode. See :ref:`cec-core-processing` for details.

If there is no initiator, then any CEC filehandle can use
:ref:`CEC_TRANSMIT <cec-ioc-receive>`. If there is an exclusive
initiator then only that initiator can call
:ref:`CEC_TRANSMIT <cec-ioc-receive>`. The follower can of course
always call :ref:`CEC_TRANSMIT <cec-ioc-receive>`.

Available initiator modes are:


.. _cec-mode-initiator:

.. flat-table:: Initiator Modes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``CEC_MODE_NO_INITIATOR``

       -  0x0

       -  This is not an initiator, i.e. it cannot transmit CEC messages or
          make any other changes to the CEC adapter.

    -  .. row 2

       -  ``CEC_MODE_INITIATOR``

       -  0x1

       -  This is an initiator (the default when the device is opened) and
          it can transmit CEC messages and make changes to the CEC adapter,
          unless there is an exclusive initiator.

    -  .. row 3

       -  ``CEC_MODE_EXCL_INITIATOR``

       -  0x2

       -  This is an exclusive initiator and this file descriptor is the
          only one that can transmit CEC messages and make changes to the
          CEC adapter. If someone else is already the exclusive initiator
          then an attempt to become one will return the EBUSY error code
          error.


Available follower modes are:


.. _cec-mode-follower:

.. flat-table:: Follower Modes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``CEC_MODE_NO_FOLLOWER``

       -  0x00

       -  This is not a follower (the default when the device is opened).

    -  .. row 2

       -  ``CEC_MODE_FOLLOWER``

       -  0x10

       -  This is a follower and it will receive CEC messages unless there
          is an exclusive follower. You cannot become a follower if
          ``CEC_CAP_TRANSMIT`` is not set or if ``CEC_MODE_NO_INITIATOR``
          was specified, EINVAL error code is returned in that case.

    -  .. row 3

       -  ``CEC_MODE_EXCL_FOLLOWER``

       -  0x20

       -  This is an exclusive follower and only this file descriptor will
          receive CEC messages for processing. If someone else is already
          the exclusive follower then an attempt to become one will return
          the EBUSY error code error. You cannot become a follower if
          ``CEC_CAP_TRANSMIT`` is not set or if ``CEC_MODE_NO_INITIATOR``
          was specified, EINVAL error code is returned in that case.

    -  .. row 4

       -  ``CEC_MODE_EXCL_FOLLOWER_PASSTHRU``

       -  0x30

       -  This is an exclusive follower and only this file descriptor will
          receive CEC messages for processing. In addition it will put the
          CEC device into passthrough mode, allowing the exclusive follower
          to handle most core messages instead of relying on the CEC
          framework for that. If someone else is already the exclusive
          follower then an attempt to become one will return the EBUSY error
          code error. You cannot become a follower if ``CEC_CAP_TRANSMIT``
          is not set or if ``CEC_MODE_NO_INITIATOR`` was specified, EINVAL
          error code is returned in that case.

    -  .. row 5

       -  ``CEC_MODE_MONITOR``

       -  0xe0

       -  Put the file descriptor into monitor mode. Can only be used in
          combination with ``CEC_MODE_NO_INITIATOR``, otherwise EINVAL error
          code will be returned. In monitor mode all messages this CEC
          device transmits and all messages it receives (both broadcast
          messages and directed messages for one its logical addresses) will
          be reported. This is very useful for debugging. This is only
          allowed if the process has the ``CAP_NET_ADMIN`` capability. If
          that is not set, then EPERM error code is returned.

    -  .. row 6

       -  ``CEC_MODE_MONITOR_ALL``

       -  0xf0

       -  Put the file descriptor into 'monitor all' mode. Can only be used
          in combination with ``CEC_MODE_NO_INITIATOR``, otherwise EINVAL
          error code will be returned. In 'monitor all' mode all messages
          this CEC device transmits and all messages it receives, including
          directed messages for other CEC devices will be reported. This is
          very useful for debugging, but not all devices support this. This
          mode requires that the ``CEC_CAP_MONITOR_ALL`` capability is set,
          otherwise EINVAL error code is returned. This is only allowed if
          the process has the ``CAP_NET_ADMIN`` capability. If that is not
          set, then EPERM error code is returned.


Core message processing details:


.. _cec-core-processing:

.. flat-table:: Core Message Processing
    :header-rows:  0
    :stub-columns: 0


    -  .. row 1

       -  ``CEC_MSG_GET_CEC_VERSION``

       -  When in passthrough mode this message has to be handled by
          userspace, otherwise the core will return the CEC version that was
          set with
          :ref:`CEC_ADAP_S_LOG_ADDRS <cec-ioc-adap-g-log-addrs>`.

    -  .. row 2

       -  ``CEC_MSG_GIVE_DEVICE_VENDOR_ID``

       -  When in passthrough mode this message has to be handled by
          userspace, otherwise the core will return the vendor ID that was
          set with
          :ref:`CEC_ADAP_S_LOG_ADDRS <cec-ioc-adap-g-log-addrs>`.

    -  .. row 3

       -  ``CEC_MSG_ABORT``

       -  When in passthrough mode this message has to be handled by
          userspace, otherwise the core will return a feature refused
          message as per the specification.

    -  .. row 4

       -  ``CEC_MSG_GIVE_PHYSICAL_ADDR``

       -  When in passthrough mode this message has to be handled by
          userspace, otherwise the core will report the current physical
          address.

    -  .. row 5

       -  ``CEC_MSG_GIVE_OSD_NAME``

       -  When in passthrough mode this message has to be handled by
          userspace, otherwise the core will report the current OSD name as
          was set with
          :ref:`CEC_ADAP_S_LOG_ADDRS <cec-ioc-adap-g-log-addrs>`.

    -  .. row 6

       -  ``CEC_MSG_GIVE_FEATURES``

       -  When in passthrough mode this message has to be handled by
          userspace, otherwise the core will report the current features as
          was set with
          :ref:`CEC_ADAP_S_LOG_ADDRS <cec-ioc-adap-g-log-addrs>` or
          the message is ignore if the CEC version was older than 2.0.

    -  .. row 7

       -  ``CEC_MSG_USER_CONTROL_PRESSED``

       -  If ``CEC_CAP_RC`` is set, then generate a remote control key
          press. This message is always passed on to userspace.

    -  .. row 8

       -  ``CEC_MSG_USER_CONTROL_RELEASED``

       -  If ``CEC_CAP_RC`` is set, then generate a remote control key
          release. This message is always passed on to userspace.

    -  .. row 9

       -  ``CEC_MSG_REPORT_PHYSICAL_ADDR``

       -  The CEC framework will make note of the reported physical address
          and then just pass the message on to userspace.



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
