.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _CEC_MODE:
.. _CEC_G_MODE:
.. _CEC_S_MODE:

********************************
ioctls CEC_G_MODE and CEC_S_MODE
********************************

CEC_G_MODE, CEC_S_MODE - Get or set exclusive use of the CEC adapter

Synopsis
========

.. c:function:: int ioctl( int fd, CEC_G_MODE, __u32 *argp )
   :name: CEC_G_MODE

.. c:function:: int ioctl( int fd, CEC_S_MODE, __u32 *argp )
   :name: CEC_S_MODE

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <cec-open>`.

``argp``
    Pointer to CEC mode.

Description
===========

By default any filehandle can use :ref:`CEC_TRANSMIT`, but in order to prevent
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
follower who will use :ref:`ioctl CEC_RECEIVE <CEC_RECEIVE>` to dequeue
the new message. The framework expects the follower to make the right
decisions.

The CEC framework will process core messages unless requested otherwise
by the follower. The follower can enable the passthrough mode. In that
case, the CEC framework will pass on most core messages without
processing them and the follower will have to implement those messages.
There are some messages that the core will always process, regardless of
the passthrough mode. See :ref:`cec-core-processing` for details.

If there is no initiator, then any CEC filehandle can use
:ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>`. If there is an exclusive
initiator then only that initiator can call
:ref:`CEC_TRANSMIT`. The follower can of course
always call :ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>`.

Available initiator modes are:

.. tabularcolumns:: |p{5.6cm}|p{0.9cm}|p{11.0cm}|

.. _cec-mode-initiator_e:

.. flat-table:: Initiator Modes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 16

    * .. _`CEC-MODE-NO-INITIATOR`:

      - ``CEC_MODE_NO_INITIATOR``
      - 0x0
      - This is not an initiator, i.e. it cannot transmit CEC messages or
	make any other changes to the CEC adapter.
    * .. _`CEC-MODE-INITIATOR`:

      - ``CEC_MODE_INITIATOR``
      - 0x1
      - This is an initiator (the default when the device is opened) and
	it can transmit CEC messages and make changes to the CEC adapter,
	unless there is an exclusive initiator.
    * .. _`CEC-MODE-EXCL-INITIATOR`:

      - ``CEC_MODE_EXCL_INITIATOR``
      - 0x2
      - This is an exclusive initiator and this file descriptor is the
	only one that can transmit CEC messages and make changes to the
	CEC adapter. If someone else is already the exclusive initiator
	then an attempt to become one will return the ``EBUSY`` error code
	error.


Available follower modes are:

.. tabularcolumns:: |p{6.6cm}|p{0.9cm}|p{10.0cm}|

.. _cec-mode-follower_e:

.. cssclass:: longtable

.. flat-table:: Follower Modes
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 16

    * .. _`CEC-MODE-NO-FOLLOWER`:

      - ``CEC_MODE_NO_FOLLOWER``
      - 0x00
      - This is not a follower (the default when the device is opened).
    * .. _`CEC-MODE-FOLLOWER`:

      - ``CEC_MODE_FOLLOWER``
      - 0x10
      - This is a follower and it will receive CEC messages unless there
	is an exclusive follower. You cannot become a follower if
	:ref:`CEC_CAP_TRANSMIT <CEC-CAP-TRANSMIT>` is not set or if :ref:`CEC_MODE_NO_INITIATOR <CEC-MODE-NO-INITIATOR>`
	was specified, the ``EINVAL`` error code is returned in that case.
    * .. _`CEC-MODE-EXCL-FOLLOWER`:

      - ``CEC_MODE_EXCL_FOLLOWER``
      - 0x20
      - This is an exclusive follower and only this file descriptor will
	receive CEC messages for processing. If someone else is already
	the exclusive follower then an attempt to become one will return
	the ``EBUSY`` error code. You cannot become a follower if
	:ref:`CEC_CAP_TRANSMIT <CEC-CAP-TRANSMIT>` is not set or if :ref:`CEC_MODE_NO_INITIATOR <CEC-MODE-NO-INITIATOR>`
	was specified, the ``EINVAL`` error code is returned in that case.
    * .. _`CEC-MODE-EXCL-FOLLOWER-PASSTHRU`:

      - ``CEC_MODE_EXCL_FOLLOWER_PASSTHRU``
      - 0x30
      - This is an exclusive follower and only this file descriptor will
	receive CEC messages for processing. In addition it will put the
	CEC device into passthrough mode, allowing the exclusive follower
	to handle most core messages instead of relying on the CEC
	framework for that. If someone else is already the exclusive
	follower then an attempt to become one will return the ``EBUSY`` error
	code. You cannot become a follower if :ref:`CEC_CAP_TRANSMIT <CEC-CAP-TRANSMIT>`
	is not set or if :ref:`CEC_MODE_NO_INITIATOR <CEC-MODE-NO-INITIATOR>` was specified,
	the ``EINVAL`` error code is returned in that case.
    * .. _`CEC-MODE-MONITOR-PIN`:

      - ``CEC_MODE_MONITOR_PIN``
      - 0xd0
      - Put the file descriptor into pin monitoring mode. Can only be used in
	combination with :ref:`CEC_MODE_NO_INITIATOR <CEC-MODE-NO-INITIATOR>`,
	otherwise the ``EINVAL`` error code will be returned.
	This mode requires that the :ref:`CEC_CAP_MONITOR_PIN <CEC-CAP-MONITOR-PIN>`
	capability is set, otherwise the ``EINVAL`` error code is returned.
	While in pin monitoring mode this file descriptor can receive the
	``CEC_EVENT_PIN_CEC_LOW`` and ``CEC_EVENT_PIN_CEC_HIGH`` events to see the
	low-level CEC pin transitions. This is very useful for debugging.
	This mode is only allowed if the process has the ``CAP_NET_ADMIN``
	capability. If that is not set, then the ``EPERM`` error code is returned.
    * .. _`CEC-MODE-MONITOR`:

      - ``CEC_MODE_MONITOR``
      - 0xe0
      - Put the file descriptor into monitor mode. Can only be used in
	combination with :ref:`CEC_MODE_NO_INITIATOR <CEC-MODE-NO-INITIATOR>`,
	otherwise the ``EINVAL`` error code will be returned.
	In monitor mode all messages this CEC
	device transmits and all messages it receives (both broadcast
	messages and directed messages for one its logical addresses) will
	be reported. This is very useful for debugging. This is only
	allowed if the process has the ``CAP_NET_ADMIN`` capability. If
	that is not set, then the ``EPERM`` error code is returned.
    * .. _`CEC-MODE-MONITOR-ALL`:

      - ``CEC_MODE_MONITOR_ALL``
      - 0xf0
      - Put the file descriptor into 'monitor all' mode. Can only be used
	in combination with :ref:`CEC_MODE_NO_INITIATOR <CEC-MODE-NO-INITIATOR>`, otherwise
	the ``EINVAL`` error code will be returned. In 'monitor all' mode all messages
	this CEC device transmits and all messages it receives, including
	directed messages for other CEC devices will be reported. This is
	very useful for debugging, but not all devices support this. This
	mode requires that the :ref:`CEC_CAP_MONITOR_ALL <CEC-CAP-MONITOR-ALL>` capability is set,
	otherwise the ``EINVAL`` error code is returned. This is only allowed if
	the process has the ``CAP_NET_ADMIN`` capability. If that is not
	set, then the ``EPERM`` error code is returned.


Core message processing details:

.. tabularcolumns:: |p{6.6cm}|p{10.9cm}|

.. _cec-core-processing:

.. flat-table:: Core Message Processing
    :header-rows:  0
    :stub-columns: 0
    :widths: 1 8

    * .. _`CEC-MSG-GET-CEC-VERSION`:

      - ``CEC_MSG_GET_CEC_VERSION``
      - The core will return the CEC version that was set with
	:ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`,
	except when in passthrough mode. In passthrough mode the core
	does nothing and this message has to be handled by a follower
	instead.
    * .. _`CEC-MSG-GIVE-DEVICE-VENDOR-ID`:

      - ``CEC_MSG_GIVE_DEVICE_VENDOR_ID``
      - The core will return the vendor ID that was set with
	:ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`,
	except when in passthrough mode. In passthrough mode the core
	does nothing and this message has to be handled by a follower
	instead.
    * .. _`CEC-MSG-ABORT`:

      - ``CEC_MSG_ABORT``
      - The core will return a Feature Abort message with reason
        'Feature Refused' as per the specification, except when in
	passthrough mode. In passthrough mode the core does nothing
	and this message has to be handled by a follower instead.
    * .. _`CEC-MSG-GIVE-PHYSICAL-ADDR`:

      - ``CEC_MSG_GIVE_PHYSICAL_ADDR``
      - The core will report the current physical address, except when
        in passthrough mode. In passthrough mode the core does nothing
	and this message has to be handled by a follower instead.
    * .. _`CEC-MSG-GIVE-OSD-NAME`:

      - ``CEC_MSG_GIVE_OSD_NAME``
      - The core will report the current OSD name that was set with
	:ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`,
	except when in passthrough mode. In passthrough mode the core
	does nothing and this message has to be handled by a follower
	instead.
    * .. _`CEC-MSG-GIVE-FEATURES`:

      - ``CEC_MSG_GIVE_FEATURES``
      - The core will do nothing if the CEC version is older than 2.0,
        otherwise it will report the current features that were set with
	:ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`,
	except when in passthrough mode. In passthrough mode the core
	does nothing (for any CEC version) and this message has to be handled
	by a follower instead.
    * .. _`CEC-MSG-USER-CONTROL-PRESSED`:

      - ``CEC_MSG_USER_CONTROL_PRESSED``
      - If :ref:`CEC_CAP_RC <CEC-CAP-RC>` is set and if
        :ref:`CEC_LOG_ADDRS_FL_ALLOW_RC_PASSTHRU <CEC-LOG-ADDRS-FL-ALLOW-RC-PASSTHRU>`
	is set, then generate a remote control key
	press. This message is always passed on to the follower(s).
    * .. _`CEC-MSG-USER-CONTROL-RELEASED`:

      - ``CEC_MSG_USER_CONTROL_RELEASED``
      - If :ref:`CEC_CAP_RC <CEC-CAP-RC>` is set and if
        :ref:`CEC_LOG_ADDRS_FL_ALLOW_RC_PASSTHRU <CEC-LOG-ADDRS-FL-ALLOW-RC-PASSTHRU>`
        is set, then generate a remote control key
	release. This message is always passed on to the follower(s).
    * .. _`CEC-MSG-REPORT-PHYSICAL-ADDR`:

      - ``CEC_MSG_REPORT_PHYSICAL_ADDR``
      - The CEC framework will make note of the reported physical address
	and then just pass the message on to the follower(s).



Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

The :ref:`ioctl CEC_S_MODE <CEC_S_MODE>` can return the following
error codes:

EINVAL
    The requested mode is invalid.

EPERM
    Monitor mode is requested, but the process does have the ``CAP_NET_ADMIN``
    capability.

EBUSY
    Someone else is already an exclusive follower or initiator.
