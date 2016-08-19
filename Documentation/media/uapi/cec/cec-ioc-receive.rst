.. -*- coding: utf-8; mode: rst -*-

.. _CEC_TRANSMIT:
.. _CEC_RECEIVE:

***********************************
ioctls CEC_RECEIVE and CEC_TRANSMIT
***********************************

Name
====

CEC_RECEIVE, CEC_TRANSMIT - Receive or transmit a CEC message


Synopsis
========

.. c:function:: int ioctl( int fd, int request, struct cec_msg *argp )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <cec-func-open>`.

``request``
    CEC_RECEIVE, CEC_TRANSMIT

``argp``


Description
===========

.. note::

   This documents the proposed CEC API. This API is not yet finalized
   and is currently only available as a staging kernel module.

To receive a CEC message the application has to fill in the
``timeout`` field of :c:type:`struct cec_msg` and pass it to :ref:`ioctl CEC_RECEIVE <CEC_RECEIVE>`.
If the file descriptor is in non-blocking mode and there are no received
messages pending, then it will return -1 and set errno to the ``EAGAIN``
error code. If the file descriptor is in blocking mode and ``timeout``
is non-zero and no message arrived within ``timeout`` milliseconds, then
it will return -1 and set errno to the ``ETIMEDOUT`` error code.

A received message can be:

1. a message received from another CEC device (the ``sequence`` field will
   be 0).
2. the result of an earlier non-blocking transmit (the ``sequence`` field will
   be non-zero).

To send a CEC message the application has to fill in the
:c:type:`struct cec_msg` and pass it to
:ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>`. The :ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>` is only available if
``CEC_CAP_TRANSMIT`` is set. If there is no more room in the transmit
queue, then it will return -1 and set errno to the ``EBUSY`` error code.
The transmit queue has enough room for 18 messages (about 1 second worth
of 2-byte messages). Note that the CEC kernel framework will also reply
to core messages (see :ref:cec-core-processing), so it is not a good
idea to fully fill up the transmit queue.

If the file descriptor is in non-blocking mode then the transmit will
return 0 and the result of the transmit will be available via
:ref:`ioctl CEC_RECEIVE <CEC_RECEIVE>` once the transmit has finished
(including waiting for a reply, if requested).

The ``sequence`` field is filled in for every transmit and this can be
checked against the received messages to find the corresponding transmit
result.


.. tabularcolumns:: |p{1.0cm}|p{3.5cm}|p{13.0cm}|

.. _cec-msg:

.. cssclass:: longtable

.. flat-table:: struct cec_msg
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 16


    -  .. row 1

       -  __u64

       -  ``tx_ts``

       -  Timestamp in ns of when the last byte of the message was transmitted.
	  The timestamp has been taken from the ``CLOCK_MONOTONIC`` clock. To access
	  the same clock from userspace use :c:func:`clock_gettime(2)`.

    -  .. row 2

       -  __u64

       -  ``rx_ts``

       -  Timestamp in ns of when the last byte of the message was received.
	  The timestamp has been taken from the ``CLOCK_MONOTONIC`` clock. To access
	  the same clock from userspace use :c:func:`clock_gettime(2)`.

    -  .. row 3

       -  __u32

       -  ``len``

       -  The length of the message. For :ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>` this is filled in
	  by the application. The driver will fill this in for
	  :ref:`ioctl CEC_RECEIVE <CEC_RECEIVE>`. For :ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>` it will be
	  filled in by the driver with the length of the reply message if ``reply`` was set.

    -  .. row 4

       -  __u32

       -  ``timeout``

       -  The timeout in milliseconds. This is the time the device will wait
	  for a message to be received before timing out. If it is set to 0,
	  then it will wait indefinitely when it is called by :ref:`ioctl CEC_RECEIVE <CEC_RECEIVE>`.
	  If it is 0 and it is called by :ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>`,
	  then it will be replaced by 1000 if the ``reply`` is non-zero or
	  ignored if ``reply`` is 0.

    -  .. row 5

       -  __u32

       -  ``sequence``

       -  A non-zero sequence number is automatically assigned by the CEC framework
	  for all transmitted messages. It is used by the CEC framework when it queues
	  the transmit result (when transmit was called in non-blocking mode). This
	  allows the application to associate the received message with the original
	  transmit.

    -  .. row 6

       -  __u32

       -  ``flags``

       -  Flags. No flags are defined yet, so set this to 0.

    -  .. row 7

       -  __u8

       -  ``tx_status``

       -  The status bits of the transmitted message. See
	  :ref:`cec-tx-status` for the possible status values. It is 0 if
	  this messages was received, not transmitted.

    -  .. row 8

       -  __u8

       -  ``msg[16]``

       -  The message payload. For :ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>` this is filled in by the
	  application. The driver will fill this in for :ref:`ioctl CEC_RECEIVE <CEC_RECEIVE>`.
	  For :ref:`ioctl CEC_TRANSMIT <CEC_TRANSMIT>` it will be filled in by the driver with
	  the payload of the reply message if ``timeout`` was set.

    -  .. row 8

       -  __u8

       -  ``reply``

       -  Wait until this message is replied. If ``reply`` is 0 and the
	  ``timeout`` is 0, then don't wait for a reply but return after
	  transmitting the message. Ignored by :ref:`ioctl CEC_RECEIVE <CEC_RECEIVE>`.
	  The case where ``reply`` is 0 (this is the opcode for the Feature Abort
	  message) and ``timeout`` is non-zero is specifically allowed to make it
	  possible to send a message and wait up to ``timeout`` milliseconds for a
	  Feature Abort reply. In this case ``rx_status`` will either be set
	  to :ref:`CEC_RX_STATUS_TIMEOUT <CEC-RX-STATUS-TIMEOUT>` or
	  :ref:`CEC_RX_STATUS_FEATURE_ABORT <CEC-RX-STATUS-FEATURE-ABORT>`.

    -  .. row 9

       -  __u8

       -  ``rx_status``

       -  The status bits of the received message. See
	  :ref:`cec-rx-status` for the possible status values. It is 0 if
	  this message was transmitted, not received, unless this is the
	  reply to a transmitted message. In that case both ``rx_status``
	  and ``tx_status`` are set.

    -  .. row 10

       -  __u8

       -  ``tx_status``

       -  The status bits of the transmitted message. See
	  :ref:`cec-tx-status` for the possible status values. It is 0 if
	  this messages was received, not transmitted.

    -  .. row 11

       -  __u8

       -  ``tx_arb_lost_cnt``

       -  A counter of the number of transmit attempts that resulted in the
	  Arbitration Lost error. This is only set if the hardware supports
	  this, otherwise it is always 0. This counter is only valid if the
	  :ref:`CEC_TX_STATUS_ARB_LOST <CEC-TX-STATUS-ARB-LOST>` status bit is set.

    -  .. row 12

       -  __u8

       -  ``tx_nack_cnt``

       -  A counter of the number of transmit attempts that resulted in the
	  Not Acknowledged error. This is only set if the hardware supports
	  this, otherwise it is always 0. This counter is only valid if the
	  :ref:`CEC_TX_STATUS_NACK <CEC-TX-STATUS-NACK>` status bit is set.

    -  .. row 13

       -  __u8

       -  ``tx_low_drive_cnt``

       -  A counter of the number of transmit attempts that resulted in the
	  Arbitration Lost error. This is only set if the hardware supports
	  this, otherwise it is always 0. This counter is only valid if the
	  :ref:`CEC_TX_STATUS_LOW_DRIVE <CEC-TX-STATUS-LOW-DRIVE>` status bit is set.

    -  .. row 14

       -  __u8

       -  ``tx_error_cnt``

       -  A counter of the number of transmit errors other than Arbitration
	  Lost or Not Acknowledged. This is only set if the hardware
	  supports this, otherwise it is always 0. This counter is only
	  valid if the :ref:`CEC_TX_STATUS_ERROR <CEC-TX-STATUS-ERROR>` status bit is set.


.. tabularcolumns:: |p{5.6cm}|p{0.9cm}|p{11.0cm}|

.. _cec-tx-status:

.. flat-table:: CEC Transmit Status
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 16


    -  .. _`CEC-TX-STATUS-OK`:

       -  ``CEC_TX_STATUS_OK``

       -  0x01

       -  The message was transmitted successfully. This is mutually
	  exclusive with :ref:`CEC_TX_STATUS_MAX_RETRIES <CEC-TX-STATUS-MAX-RETRIES>`. Other bits can still
	  be set if earlier attempts met with failure before the transmit
	  was eventually successful.

    -  .. _`CEC-TX-STATUS-ARB-LOST`:

       -  ``CEC_TX_STATUS_ARB_LOST``

       -  0x02

       -  CEC line arbitration was lost.

    -  .. _`CEC-TX-STATUS-NACK`:

       -  ``CEC_TX_STATUS_NACK``

       -  0x04

       -  Message was not acknowledged.

    -  .. _`CEC-TX-STATUS-LOW-DRIVE`:

       -  ``CEC_TX_STATUS_LOW_DRIVE``

       -  0x08

       -  Low drive was detected on the CEC bus. This indicates that a
	  follower detected an error on the bus and requests a
	  retransmission.

    -  .. _`CEC-TX-STATUS-ERROR`:

       -  ``CEC_TX_STATUS_ERROR``

       -  0x10

       -  Some error occurred. This is used for any errors that do not fit
	  the previous two, either because the hardware could not tell which
	  error occurred, or because the hardware tested for other
	  conditions besides those two.

    -  .. _`CEC-TX-STATUS-MAX-RETRIES`:

       -  ``CEC_TX_STATUS_MAX_RETRIES``

       -  0x20

       -  The transmit failed after one or more retries. This status bit is
	  mutually exclusive with :ref:`CEC_TX_STATUS_OK <CEC-TX-STATUS-OK>`. Other bits can still
	  be set to explain which failures were seen.


.. tabularcolumns:: |p{5.6cm}|p{0.9cm}|p{11.0cm}|

.. _cec-rx-status:

.. flat-table:: CEC Receive Status
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 16


    -  .. _`CEC-RX-STATUS-OK`:

       -  ``CEC_RX_STATUS_OK``

       -  0x01

       -  The message was received successfully.

    -  .. _`CEC-RX-STATUS-TIMEOUT`:

       -  ``CEC_RX_STATUS_TIMEOUT``

       -  0x02

       -  The reply to an earlier transmitted message timed out.

    -  .. _`CEC-RX-STATUS-FEATURE-ABORT`:

       -  ``CEC_RX_STATUS_FEATURE_ABORT``

       -  0x04

       -  The message was received successfully but the reply was
	  ``CEC_MSG_FEATURE_ABORT``. This status is only set if this message
	  was the reply to an earlier transmitted message.



Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
