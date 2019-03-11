.. Permission is granted to copy, distribute and/or modify this
.. document under the terms of the GNU Free Documentation License,
.. Version 1.1 or any later version published by the Free Software
.. Foundation, with no Invariant Sections, no Front-Cover Texts
.. and no Back-Cover Texts. A copy of the license is included at
.. Documentation/media/uapi/fdl-appendix.rst.
..
.. TODO: replace it to GFDL-1.1-or-later WITH no-invariant-sections

.. _FE_DISEQC_SEND_MASTER_CMD:

*******************************
ioctl FE_DISEQC_SEND_MASTER_CMD
*******************************

Name
====

FE_DISEQC_SEND_MASTER_CMD - Sends a DiSEqC command


Synopsis
========

.. c:function:: int ioctl( int fd, FE_DISEQC_SEND_MASTER_CMD, struct dvb_diseqc_master_cmd *argp )
    :name: FE_DISEQC_SEND_MASTER_CMD


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

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

