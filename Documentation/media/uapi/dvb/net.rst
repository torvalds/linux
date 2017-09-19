.. -*- coding: utf-8; mode: rst -*-

.. _net:

######################
Digital TV Network API
######################

The Digital TV net device controls the mapping of data packages that are part
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

Digital TV net Function Calls
#############################

.. toctree::
    :maxdepth: 1

    net-types
    net-add-if
    net-remove-if
    net-get-if
