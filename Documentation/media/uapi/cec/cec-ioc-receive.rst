.. -*- coding: utf-8; mode: rst -*-

.. _cec-ioc-receive:

*******************************
ioctl CEC_RECEIVE, CEC_TRANSMIT
*******************************

*man CEC_RECEIVE(2)*

CEC_TRANSMIT
Receive or transmit a CEC message


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

Note: this documents the proposed CEC API. This API is not yet finalized
and is currently only available as a staging kernel module.

To receive a CEC message the application has to fill in the
:c:type:`struct cec_msg` structure and pass it to the ``CEC_RECEIVE``
ioctl. ``CEC_RECEIVE`` is only available if ``CEC_CAP_RECEIVE`` is set.
If the file descriptor is in non-blocking mode and there are no received
messages pending, then it will return -1 and set errno to the EAGAIN
error code. If the file descriptor is in blocking mode and ``timeout``
is non-zero and no message arrived within ``timeout`` milliseconds, then
it will return -1 and set errno to the ETIMEDOUT error code.

To send a CEC message the application has to fill in the
:c:type:`struct cec_msg` structure and pass it to the
``CEC_TRANSMIT`` ioctl. ``CEC_TRANSMIT`` is only available if
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
          ``CEC_TRANSMIT`` with ``reply`` set to 0, or the timestamp of the
          received message in all other cases.

    -  .. row 2

       -  __u32

       -  ``len``

       -  The length of the message. For ``CEC_TRANSMIT`` this is filled in
          by the application. The driver will fill this in for
          ``CEC_RECEIVE`` and for ``CEC_TRANSMIT`` it will be filled in with
          the length of the reply message if ``reply`` was set.

    -  .. row 3

       -  __u32

       -  ``timeout``

       -  The timeout in milliseconds. This is the time the device will wait
          for a message to be received before timing out. If it is set to 0,
          then it will wait indefinitely when it is called by
          ``CEC_RECEIVE``. If it is 0 and it is called by ``CEC_TRANSMIT``,
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

       -  The message payload. For ``CEC_TRANSMIT`` this is filled in by the
          application. The driver will fill this in for ``CEC_RECEIVE`` and
          for ``CEC_TRANSMIT`` it will be filled in with the payload of the
          reply message if ``reply`` was set.

    -  .. row 9

       -  __u8

       -  ``reply``

       -  Wait until this message is replied. If ``reply`` is 0 and the
          ``timeout`` is 0, then don't wait for a reply but return after
          transmitting the message. If there was an error as indicated by a
          non-zero ``tx_status`` field, then ``reply`` and ``timeout`` are
          both set to 0 by the driver. Ignored by ``CEC_RECEIVE``. The case
          where ``reply`` is 0 (this is the opcode for the Feature Abort
          message) and ``timeout`` is non-zero is specifically allowed to
          send a message and wait up to ``timeout`` milliseconds for a
          Feature Abort reply. In this case ``rx_status`` will either be set
          to ``CEC_RX_STATUS_TIMEOUT`` or ``CEC_RX_STATUS_FEATURE_ABORT``.

    -  .. row 10

       -  __u8

       -  ``tx_arb_lost_cnt``

       -  A counter of the number of transmit attempts that resulted in the
          Arbitration Lost error. This is only set if the hardware supports
          this, otherwise it is always 0. This counter is only valid if the
          ``CEC_TX_STATUS_ARB_LOST`` status bit is set.

    -  .. row 11

       -  __u8

       -  ``tx_nack_cnt``

       -  A counter of the number of transmit attempts that resulted in the
          Not Acknowledged error. This is only set if the hardware supports
          this, otherwise it is always 0. This counter is only valid if the
          ``CEC_TX_STATUS_NACK`` status bit is set.

    -  .. row 12

       -  __u8

       -  ``tx_low_drive_cnt``

       -  A counter of the number of transmit attempts that resulted in the
          Arbitration Lost error. This is only set if the hardware supports
          this, otherwise it is always 0. This counter is only valid if the
          ``CEC_TX_STATUS_LOW_DRIVE`` status bit is set.

    -  .. row 13

       -  __u8

       -  ``tx_error_cnt``

       -  A counter of the number of transmit errors other than Arbitration
          Lost or Not Acknowledged. This is only set if the hardware
          supports this, otherwise it is always 0. This counter is only
          valid if the ``CEC_TX_STATUS_ERROR`` status bit is set.



.. _cec-tx-status:

.. flat-table:: CEC Transmit Status
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``CEC_TX_STATUS_OK``

       -  0x01

       -  The message was transmitted successfully. This is mutually
          exclusive with ``CEC_TX_STATUS_MAX_RETRIES``. Other bits can still
          be set if earlier attempts met with failure before the transmit
          was eventually successful.

    -  .. row 2

       -  ``CEC_TX_STATUS_ARB_LOST``

       -  0x02

       -  CEC line arbitration was lost.

    -  .. row 3

       -  ``CEC_TX_STATUS_NACK``

       -  0x04

       -  Message was not acknowledged.

    -  .. row 4

       -  ``CEC_TX_STATUS_LOW_DRIVE``

       -  0x08

       -  Low drive was detected on the CEC bus. This indicates that a
          follower detected an error on the bus and requests a
          retransmission.

    -  .. row 5

       -  ``CEC_TX_STATUS_ERROR``

       -  0x10

       -  Some error occurred. This is used for any errors that do not fit
          the previous two, either because the hardware could not tell which
          error occurred, or because the hardware tested for other
          conditions besides those two.

    -  .. row 6

       -  ``CEC_TX_STATUS_MAX_RETRIES``

       -  0x20

       -  The transmit failed after one or more retries. This status bit is
          mutually exclusive with ``CEC_TX_STATUS_OK``. Other bits can still
          be set to explain which failures were seen.



.. _cec-rx-status:

.. flat-table:: CEC Receive Status
    :header-rows:  0
    :stub-columns: 0
    :widths:       3 1 4


    -  .. row 1

       -  ``CEC_RX_STATUS_OK``

       -  0x01

       -  The message was received successfully.

    -  .. row 2

       -  ``CEC_RX_STATUS_TIMEOUT``

       -  0x02

       -  The reply to an earlier transmitted message timed out.

    -  .. row 3

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


.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
