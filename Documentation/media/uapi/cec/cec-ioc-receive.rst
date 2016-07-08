.. -*- coding: utf-8; mode: rst -*-

.. _CEC_TRANSMIT:
.. _CEC_RECEIVE:

*******************************
ioctl CEC_RECEIVE, CEC_TRANSMIT
*******************************

Name
====

CEC_RECEIVE, CEC_TRANSMIT - Receive or transmit a CEC message


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct cec_msg *argp )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <cec-func-open>`.

``request``
    CEC_RECEIVE, CEC_TRANSMIT

``argp``


Description
===========

Note: this documents the proposed CEC API. This API is not yet finalized
and is currently only available as a staging kernel module.

To receive a CEC message the application has to fill in the
:c:type:`struct cec_msg` structure and pass it to the :ref:`CEC_RECEIVE`
ioctl. :ref:`CEC_RECEIVE` is only available if ``CEC_CAP_RECEIVE`` is set.
If the file descriptor is in non-blocking mode and there are no received
messages pending, then it will return -1 and set errno to the EAGAIN
error code. If the file descriptor is in blocking mode and ``timeout``
is non-zero and no message arrived within ``timeout`` milliseconds, then
it will return -1 and set errno to the ETIMEDOUT error code.

To send a CEC message the application has to fill in the
:c:type:`struct cec_msg` structure and pass it to the
:ref:`CEC_TRANSMIT` ioctl. :ref:`CEC_TRANSMIT` is only available if
``CEC_CAP_TRANSMIT`` is set. If there is no more room in the transmit
queue, then it will return -1 and set errno to the EBUSY error code.


.. _cec-msg:

.. flat-table:: struct cec_msg
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  __u64

       -  ``ts``

       -  Timestamp of when the message was transmitted in ns in the case of
          :ref:`CEC_TRANSMIT` with ``reply`` set to 0, or the timestamp of the
          received message in all other cases.

    -  .. row 2

       -  __u32

       -  ``len``

       -  The length of the message. For :ref:`CEC_TRANSMIT` this is filled in
          by the application. The driver will fill this in for
          :ref:`CEC_RECEIVE` and for :ref:`CEC_TRANSMIT` it will be filled in with
          the length of the reply message if ``reply`` was set.

    -  .. row 3

       -  __u32

       -  ``timeout``

       -  The timeout in milliseconds. This is the time the device will wait
          for a message to be received before timing out. If it is set to 0,
          then it will wait indefinitely when it is called by
          :ref:`CEC_RECEIVE`. If it is 0 and it is called by :ref:`CEC_TRANSMIT`,
          then it will be replaced by 1000 if the ``reply`` is non-zero or
          ignored if ``reply`` is 0.

    -  .. row 4

       -  __u32

       -  ``sequence``

       -  The sequence number is automatically assigned by the CEC framework
          for all transmitted messages. It can be later used by the
          framework to generate an event if a reply for a message was
          requested and the message was transmitted in a non-blocking mode.

    -  .. row 5

       -  __u32

       -  ``flags``

       -  Flags. No flags are defined yet, so set this to 0.

    -  .. row 6

       -  __u8

       -  ``rx_status``

       -  The status bits of the received message. See
          :ref:`cec-rx-status` for the possible status values. It is 0 if
          this message was transmitted, not received, unless this is the
          reply to a transmitted message. In that case both ``rx_status``
          and ``tx_status`` are set.

    -  .. row 7

       -  __u8

       -  ``tx_status``

       -  The status bits of the transmitted message. See
          :ref:`cec-tx-status` for the possible status values. It is 0 if
          this messages was received, not transmitted.

    -  .. row 8

       -  __u8

       -  ``msg``\ [16]

       -  The message payload. For :ref:`CEC_TRANSMIT` this is filled in by the
          application. The driver will fill this in for :ref:`CEC_RECEIVE` and
          for :ref:`CEC_TRANSMIT` it will be filled in with the payload of the
          reply message if ``reply`` was set.

    -  .. row 9

       -  __u8

       -  ``reply``

       -  Wait until this message is replied. If ``reply`` is 0 and the
          ``timeout`` is 0, then don't wait for a reply but return after
          transmitting the message. If there was an error as indicated by a
          non-zero ``tx_status`` field, then ``reply`` and ``timeout`` are
          both set to 0 by the driver. Ignored by :ref:`CEC_RECEIVE`. The case
          where ``reply`` is 0 (this is the opcode for the Feature Abort
          message) and ``timeout`` is non-zero is specifically allowed to
          send a message and wait up to ``timeout`` milliseconds for a
          Feature Abort reply. In this case ``rx_status`` will either be set
          to :ref:`CEC_RX_STATUS_TIMEOUT <CEC_RX_STATUS_TIMEOUT>` or :ref:`CEC_RX_STATUS_FEATURE_ABORT <CEC_RX_STATUS_FEATURE_ABORT>`.

    -  .. row 10

       -  __u8

       -  ``tx_arb_lost_cnt``

       -  A counter of the number of transmit attempts that resulted in the
          Arbitration Lost error. This is only set if the hardware supports
          this, otherwise it is always 0. This counter is only valid if the
          :ref:`CEC_TX_STATUS_ARB_LOST <CEC_TX_STATUS_ARB_LOST>` status bit is set.

    -  .. row 11

       -  __u8

       -  ``tx_nack_cnt``

       -  A counter of the number of transmit attempts that resulted in the
          Not Acknowledged error. This is only set if the hardware supports
          this, otherwise it is always 0. This counter is only valid if the
          :ref:`CEC_TX_STATUS_NACK <CEC_TX_STATUS_NACK>` status bit is set.

    -  .. row 12

       -  __u8

       -  ``tx_low_drive_cnt``

       -  A counter of the number of transmit attempts that resulted in the
          Arbitration Lost error. This is only set if the hardware supports
          this, otherwise it is always 0. This counter is only valid if the
          :ref:`CEC_TX_STATUS_LOW_DRIVE <CEC_TX_STATUS_LOW_DRIVE>` status bit is set.

    -  .. row 13

       -  __u8

       -  ``tx_error_cnt``

       -  A counter of the number of transmit errors other than Arbitration
          Lost or Not Acknowledged. This is only set if the hardware
          supports this, otherwise it is always 0. This counter is only
          valid if the :ref:`CEC_TX_STATUS_ERROR <CEC_TX_STATUS_ERROR>` status bit is set.



.. _cec-tx-status:

.. flat-table:: CEC Transmit Status
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. _`CEC_TX_STATUS_OK`:

       -  ``CEC_TX_STATUS_OK``

       -  0x01

       -  The message was transmitted successfully. This is mutually
          exclusive with :ref:`CEC_TX_STATUS_MAX_RETRIES <CEC_TX_STATUS_MAX_RETRIES>`. Other bits can still
          be set if earlier attempts met with failure before the transmit
          was eventually successful.

    -  .. _`CEC_TX_STATUS_ARB_LOST`:

       -  ``CEC_TX_STATUS_ARB_LOST``

       -  0x02

       -  CEC line arbitration was lost.

    -  .. _`CEC_TX_STATUS_NACK`:

       -  ``CEC_TX_STATUS_NACK``

       -  0x04

       -  Message was not acknowledged.

    -  .. _`CEC_TX_STATUS_LOW_DRIVE`:

       -  ``CEC_TX_STATUS_LOW_DRIVE``

       -  0x08

       -  Low drive was detected on the CEC bus. This indicates that a
          follower detected an error on the bus and requests a
          retransmission.

    -  .. _`CEC_TX_STATUS_ERROR`:

       -  ``CEC_TX_STATUS_ERROR``

       -  0x10

       -  Some error occurred. This is used for any errors that do not fit
          the previous two, either because the hardware could not tell which
          error occurred, or because the hardware tested for other
          conditions besides those two.

    -  .. _`CEC_TX_STATUS_MAX_RETRIES`:

       -  ``CEC_TX_STATUS_MAX_RETRIES``

       -  0x20

       -  The transmit failed after one or more retries. This status bit is
          mutually exclusive with :ref:`CEC_TX_STATUS_OK <CEC_TX_STATUS_OK>`. Other bits can still
          be set to explain which failures were seen.



.. _cec-rx-status:

.. flat-table:: CEC Receive Status
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. _`CEC_RX_STATUS_OK`:

       -  ``CEC_RX_STATUS_OK``

       -  0x01

       -  The message was received successfully.

    -  .. _CEC_RX_STATUS_TIMEOUT:

       -  ``CEC_RX_STATUS_TIMEOUT``

       -  0x02

       -  The reply to an earlier transmitted message timed out.

    -  .. _`CEC_RX_STATUS_FEATURE_ABORT`:

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
