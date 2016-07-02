.. -*- coding: utf-8; mode: rst -*-

.. _net:

###############
DVB Network API
###############
The DVB net device controls the mapping of data packages that are part
of a transport stream to be mapped into a virtual network interface,
visible through the standard Linux network protocol stack.

Currently, two encapsulations are supported:

-  `Multi Protocol Encapsulation (MPE) <http://en.wikipedia.org/wiki/Multiprotocol_Encapsulation>`__

-  `Ultra Lightweight Encapsulation (ULE) <http://en.wikipedia.org/wiki/Unidirectional_Lightweight_Encapsulation>`__

In order to create the Linux virtual network interfaces, an application
needs to tell to the Kernel what are the PIDs and the encapsulation
types that are present on the transport stream. This is done through
``/dev/dvb/adapter?/net?`` device node. The data will be available via
virtual ``dvb?_?`` network interfaces, and will be controlled/routed via
the standard ip tools (like ip, route, netstat, ifconfig, etc).

Data types and and ioctl definitions are defined via ``linux/dvb/net.h``
header.


.. _net_fcalls:

######################
DVB net Function Calls
######################

.. _NET_ADD_IF:

****************
ioctl NET_ADD_IF
****************

*man NET_ADD_IF(2)*

Creates a new network interface for a given Packet ID.


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct dvb_net_if *net_if )

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

RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


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



.. _NET_REMOVE_IF:

*******************
ioctl NET_REMOVE_IF
*******************

*man NET_REMOVE_IF(2)*

Removes a network interface.


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, int ifnum )

Arguments
=========

``fd``
    File descriptor returned by :ref:`open() <frontend_f_open>`.

``request``
    FE_SET_TONE

``net_if``
    number of the interface to be removed


Description
===========

The NET_REMOVE_IF ioctl deletes an interface previously created via
:ref:`NET_ADD_IF <net>`.

RETURN VALUE

On success 0 is returned, on error -1 and the ``errno`` variable is set
appropriately. The generic error codes are described at the
:ref:`Generic Error Codes <gen-errors>` chapter.


.. _NET_GET_IF:

****************
ioctl NET_GET_IF
****************

*man NET_GET_IF(2)*

Read the configuration data of an interface created via
:ref:`NET_ADD_IF <net>`.


Synopsis
========

.. cpp:function:: int ioctl( int fd, int request, struct dvb_net_if *net_if )

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

The NET_GET_IF ioctl uses the interface number given by the struct
:ref:`dvb_net_if <dvb-net-if>`::ifnum field and fills the content of
struct :ref:`dvb_net_if <dvb-net-if>` with the packet ID and
encapsulation type used on such interface. If the interface was not
created yet with :ref:`NET_ADD_IF <net>`, it will return -1 and fill
the ``errno`` with ``EINVAL`` error code.

RETURN VALUE

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
