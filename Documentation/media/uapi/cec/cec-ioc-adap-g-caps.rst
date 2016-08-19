.. -*- coding: utf-8; mode: rst -*-

.. _CEC_ADAP_G_CAPS:

*********************
ioctl CEC_ADAP_G_CAPS
*********************

Name
====

CEC_ADAP_G_CAPS - Query device capabilities

Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct cec_caps *argp )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <cec-func-open>`.

``request``
    CEC_ADAP_G_CAPS

``argp``


Description
===========

.. note::

   This documents the proposed CEC API. This API is not yet finalized
   and is currently only available as a staging kernel module.

All cec devices must support :ref:`ioctl CEC_ADAP_G_CAPS <CEC_ADAP_G_CAPS>`. To query
device information, applications call the ioctl with a pointer to a
struct :ref:`cec_caps <cec-caps>`. The driver fills the structure and
returns the information to the application. The ioctl never fails.


.. _cec-caps:

.. tabularcolumns:: |p{1.2cm}|p{2.5cm}|p{13.8cm}|

.. flat-table:: struct cec_caps
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 16


    -  .. row 1

       -  char

       -  ``driver[32]``

       -  The name of the cec adapter driver.

    -  .. row 2

       -  char

       -  ``name[32]``

       -  The name of this CEC adapter. The combination ``driver`` and
	  ``name`` must be unique.

    -  .. row 3

       -  __u32

       -  ``capabilities``

       -  The capabilities of the CEC adapter, see
	  :ref:`cec-capabilities`.

    -  .. row 4

       -  __u32

       -  ``version``

       -  CEC Framework API version, formatted with the ``KERNEL_VERSION()``
	  macro.



.. _cec-capabilities:

.. tabularcolumns:: |p{4.4cm}|p{2.5cm}|p{10.6cm}|

.. flat-table:: CEC Capabilities Flags
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 8


    -  .. _`CEC-CAP-PHYS-ADDR`:

       -  ``CEC_CAP_PHYS_ADDR``

       -  0x00000001

       -  Userspace has to configure the physical address by calling
	  :ref:`ioctl CEC_ADAP_S_PHYS_ADDR <CEC_ADAP_S_PHYS_ADDR>`. If
	  this capability isn't set, then setting the physical address is
	  handled by the kernel whenever the EDID is set (for an HDMI
	  receiver) or read (for an HDMI transmitter).

    -  .. _`CEC-CAP-LOG-ADDRS`:

       -  ``CEC_CAP_LOG_ADDRS``

       -  0x00000002

       -  Userspace has to configure the logical addresses by calling
	  :ref:`ioctl CEC_ADAP_S_LOG_ADDRS <CEC_ADAP_S_LOG_ADDRS>`. If
	  this capability isn't set, then the kernel will have configured
	  this.

    -  .. _`CEC-CAP-TRANSMIT`:

       -  ``CEC_CAP_TRANSMIT``

       -  0x00000004

       -  Userspace can transmit CEC messages by calling
	  :ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>`. This implies that
	  userspace can be a follower as well, since being able to transmit
	  messages is a prerequisite of becoming a follower. If this
	  capability isn't set, then the kernel will handle all CEC
	  transmits and process all CEC messages it receives.

    -  .. _`CEC-CAP-PASSTHROUGH`:

       -  ``CEC_CAP_PASSTHROUGH``

       -  0x00000008

       -  Userspace can use the passthrough mode by calling
	  :ref:`ioctl CEC_S_MODE <CEC_S_MODE>`.

    -  .. _`CEC-CAP-RC`:

       -  ``CEC_CAP_RC``

       -  0x00000010

       -  This adapter supports the remote control protocol.

    -  .. _`CEC-CAP-MONITOR-ALL`:

       -  ``CEC_CAP_MONITOR_ALL``

       -  0x00000020

       -  The CEC hardware can monitor all messages, not just directed and
	  broadcast messages.



Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
