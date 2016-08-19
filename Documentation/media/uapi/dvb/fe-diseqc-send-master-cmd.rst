.. -*- coding: utf-8; mode: rst -*-

.. _FE_DISEQC_SEND_MASTER_CMD:

*******************************
ioctl FE_DISEQC_SEND_MASTER_CMD
*******************************

Name
====

FE_DISEQC_SEND_MASTER_CMD - Sends a DiSEqC command


Synopsis
========

.. c:function:: int ioctl( int fd, int request, struct dvb_diseqc_master_cmd *argp )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_DISEQC_SEND_MASTER_CMD

``argp``
    pointer to struct
    :ref:`dvb_diseqc_master_cmd <dvb-diseqc-master-cmd>`


Description
===========

Sends a DiSEqC command to the antenna subsystem.

.. _dvb-diseqc-master-cmd:

struct dvb_diseqc_master_cmd
============================

.. tabularcolumns:: |p{4.4cm}|p{4.4cm}|p{8.7cm}|

.. flat-table:: struct dvb_diseqc_master_cmd
    :header-rows:  0
    :stub-columns: 0
    :widths:       1 1 2


    -  .. row 1

       -  uint8_t

       -  msg[6]

       -  DiSEqC message (framing, address, command, data[3])

    -  .. row 2

       -  uint8_t

       -  msg_len

       -  Length of the DiSEqC message. Valid values are 3 to 6

Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

