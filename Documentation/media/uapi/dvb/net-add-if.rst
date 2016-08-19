.. -*- coding: utf-8; mode: rst -*-

.. _NET_ADD_IF:

****************
ioctl NET_ADD_IF
****************

Name
====

NET_ADD_IF - Creates a new network interface for a given Packet ID.


Synopsis
========

.. c:function:: int ioctl( int fd, int request, struct dvb_net_if *net_if )


Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_SET_TONE

``net_if``
    pointer to struct :ref:`dvb_net_if <dvb-net-if>`


Description
===========

The NET_ADD_IF ioctl system call selects the Packet ID (PID) that
contains a TCP/IP traffic, the type of encapsulation to be used (MPE or
ULE) and the interface number for the new interface to be created. When
the system call successfully returns, a new virtual network interface is
created.

The struct :ref:`dvb_net_if <dvb-net-if>`::ifnum field will be
filled with the number of the created interface.


.. _dvb-net-if-t:

struct dvb_net_if description
=============================

.. _dvb-net-if:

.. flat-table:: struct dvb_net_if
    :header-rows:  1
    :stub-columns: 0


    -  .. row 1

       -  ID

       -  Description

    -  .. row 2

       -  pid

       -  Packet ID (PID) of the MPEG-TS that contains data

    -  .. row 3

       -  ifnum

       -  number of the DVB interface.

    -  .. row 4

       -  feedtype

       -  Encapsulation type of the feed. It can be:
	  ``DVB_NET_FEEDTYPE_MPE`` for MPE encoding or
	  ``DVB_NET_FEEDTYPE_ULE`` for ULE encoding.


Return Value
============

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.
