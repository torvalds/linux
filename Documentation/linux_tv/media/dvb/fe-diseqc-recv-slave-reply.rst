.. -*- coding: utf-8; mode: rst -*-

.. _FE_DISEQC_RECV_SLAVE_REPLY:

********************************
ioctl FE_DISEQC_RECV_SLAVE_REPLY
********************************

*man FE_DISEQC_RECV_SLAVE_REPLY(2)*

Receives reply from a DiSEqC 2.0 command


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct dvb_diseqc_slave_reply *argp )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_DISEQC_RECV_SLAVE_REPLY

``argp``
    pointer to struct
    :ref:`dvb_diseqc_slave_reply <dvb-diseqc-slave-reply>`


Description
===========

Receives reply from a DiSEqC 2.0 command.

RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _dvb-diseqc-slave-reply:

.. flat-table:: struct dvb_diseqc_slave_reply
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  uint8_t

       -  msg[4]

       -  DiSEqC message (framing, data[3])

    -  .. row 2

       -  uint8_t

       -  msg_len

       -  Length of the DiSEqC message. Valid values are 0 to 4, where 0
          means no msg

    -  .. row 3

       -  int

       -  timeout

       -  Return from ioctl after timeout ms with errorcode when no message
          was received




.. ------------------------------------------------------------------------------
.. This file was automatically converted from DocBook-XML with the dbxml
.. library (https://github.com/return42/sphkerneldoc). The origin XML comes
.. from the linux kernel, refer to:
..
.. * https://github.com/torvalds/linux/tree/master/Documentation/DocBook
.. ------------------------------------------------------------------------------
