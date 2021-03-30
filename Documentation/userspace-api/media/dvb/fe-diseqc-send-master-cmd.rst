.. SPDX-License-Identifier: GFDL-1.1-no-invariants-or-later
.. c:namespace:: DTV.fe

.. _FE_DISEQC_SEND_MASTER_CMD:

*******************************
ioctl FE_DISEQC_SEND_MASTER_CMD
*******************************

Name
====

FE_DISEQC_SEND_MASTER_CMD - Sends a DiSEqC command

Synopsis
========

.. c:macro:: FE_DISEQC_SEND_MASTER_CMD

``int ioctl(int fd, FE_DISEQC_SEND_MASTER_CMD, struct dvb_diseqc_master_cmd *argp)``

Arguments
=========

``fd``
    File descriptor returned by :c:func:`open()`.

``argp``
    pointer to struct
    :c:type:`dvb_diseqc_master_cmd`

Description
===========

Sends the DiSEqC command pointed by :c:type:`dvb_diseqc_master_cmd`
to the antenna subsystem.

Return Value
============

On success 0 is returned.

On error -1 is returned, and the ``errno`` variable is set
appropriately.

Generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.

