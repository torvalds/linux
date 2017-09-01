.. -*- coding: utf-8; mode: rst -*-

.. _FE_DISEQC_RECV_SLAVE_REPLY:

********************************
ioctl FE_DISEQC_RECV_SLAVE_REPLY
********************************

Name
====

FE_DISEQC_RECV_SLAVE_REPLY - Receives reply from a DiSEqC 2.0 command


Synopsis
========

.. c:function:: int ioctl( int fd, FE_DISEQC_RECV_SLAVE_REPLY, struct dvb_diseqc_slave_reply *argp )
    :name: FE_DISEQC_RECV_SLAVE_REPLY


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``argp``
    pointer to struct :c:type:`dvb_diseqc_slave_reply`.


Description
===========

Receives reply from a DiSEqC 2.0 command.

The received message is stored at the buffer pointed by ``argp``.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
