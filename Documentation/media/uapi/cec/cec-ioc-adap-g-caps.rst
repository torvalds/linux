.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _CEC_ADAP_G_CAPS:

*********************
ioctl CEC_ADAP_G_CAPS
*********************

Name
====

CEC_ADAP_G_CAPS - Query device capabilities

Synopsis
========

.. c:function:: int ioctl( int fd, CEC_ADAP_G_CAPS, struct cec_caps *argp )
    :name: CEC_ADAP_G_CAPS

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open() <cec-open>`.

``argp``


Description
===========

All cec devices must support :ref:`ioctl CEC_ADAP_G_CAPS <CEC_ADAP_G_CAPS>`. To query
device information, applications call the ioctl with a pointer to a
struct :c:type:`cec_caps`. The driver fills the structure and
returns the information to the application. The ioctl never fails.

.. tabularcolumns:: |p{1.2cm}|p{2.5cm}|p{13.8cm}|

.. c:type:: cec_caps

.. flat-table:: struct cec_caps
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 16

    * - char
      - ``driver[32]``
      - The name of the cec adapter driver.
    * - char
      - ``name[32]``
      - The name of this CEC adapter. The combination ``driver`` and
	``name`` must be unique.
    * - __u32
      - ``capabilities``
      - The capabilities of the CEC adapter, see
	:ref:`cec-capabilities`.
    * - __u32
      - ``version``
      - CEC Framework API version, formatted with the ``KERNEL_VERSION()``
	macro.


.. tabularcolumns:: |p{4.4cm}|p{2.5cm}|p{10.6cm}|

.. _cec-capabilities:

.. flat-table:: CEC Capabilities Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 8

    * .. _`CEC-CAP-PHYS-ADDR`:

      - ``CEC_CAP_PHYS_ADDR``
      - 0x00000001
      - Userspace has to configure the physical address by calling
	:ref:`ioctl CEC_ADAP_S_PHYS_ADDR <CEC_ADAP_S_PHYS_ADDR>`. If
	this capability isn't set, then setting the physical address is
	handled by the kernel whenever the EDID is set (for an HDMI
	receiver) or read (for an HDMI transmitter).
    * .. _`CEC-CAP-LOG-ADDRS`:

      - ``CEC_CAP_LOG_ADDRS``
      - 0x00000002
      - Userspace has to configure the logical addresses by calling
	:ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`. If
	this capability isn't set, then the kernel will have configured
	this.
    * .. _`CEC-CAP-TRANSMIT`:

      - ``CEC_CAP_TRANSMIT``
      - 0x00000004
      - Userspace can transmit CEC messages by calling
	:ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>`. This implies that
	userspace can be a follower as well, since being able to transmit
	messages is a prerequisite of becoming a follower. If this
	capability isn't set, then the kernel will handle all CEC
	transmits and process all CEC messages it receives.
    * .. _`CEC-CAP-PASSTHROUGH`:

      - ``CEC_CAP_PASSTHROUGH``
      - 0x00000008
      - Userspace can use the passthrough mode by calling
	:ref:`ioctl CEC_S_MODE <CEC_S_MODE>`.
    * .. _`CEC-CAP-RC`:

      - ``CEC_CAP_RC``
      - 0x00000010
      - This adapter supports the remote control protocol.
    * .. _`CEC-CAP-MONITOR-ALL`:

      - ``CEC_CAP_MONITOR_ALL``
      - 0x00000020
      - The CEC hardware can monitor all messages, not just directed and
	broadcast messages.
    * .. _`CEC-CAP-NEEDS-HPD`:

      - ``CEC_CAP_NEEDS_HPD``
      - 0x00000040
      - The CEC hardware is only active if the HDMI Hotplug Detect pin is
        high. This makes it impossible to use CEC to wake up displays that
	set the HPD pin low when in standby mode, but keep the CEC bus
	alive.
    * .. _`CEC-CAP-MONITOR-PIN`:

      - ``CEC_CAP_MONITOR_PIN``
      - 0x00000080
      - The CEC hardware can monitor CEC pin changes from low to high voltage
        and vice versa. When in pin monitoring mode the application will
	receive ``CEC_EVENT_PIN_CEC_LOW`` and ``CEC_EVENT_PIN_CEC_HIGH`` events.



Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
