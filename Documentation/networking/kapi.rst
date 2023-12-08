=========================================
Linux Networking and Network Devices APIs
=========================================

Linux Networking
================

Networking Base Types
---------------------

.. kernel-doc:: include/linux/net.h
   :internal:

Socket Buffer Functions
-----------------------

.. kernel-doc:: include/linux/skbuff.h
   :internal:

.. kernel-doc:: include/net/sock.h
   :internal:

.. kernel-doc:: net/socket.c
   :export:

.. kernel-doc:: net/core/skbuff.c
   :export:

.. kernel-doc:: net/core/sock.c
   :export:

.. kernel-doc:: net/core/datagram.c
   :export:

.. kernel-doc:: net/core/stream.c
   :export:

Socket Filter
-------------

.. kernel-doc:: net/core/filter.c
   :export:

Generic Network Statistics
--------------------------

.. kernel-doc:: include/uapi/linux/gen_stats.h
   :internal:

.. kernel-doc:: net/core/gen_stats.c
   :export:

.. kernel-doc:: net/core/gen_estimator.c
   :export:

SUN RPC subsystem
-----------------

.. kernel-doc:: net/sunrpc/xdr.c
   :export:

.. kernel-doc:: net/sunrpc/svc_xprt.c
   :export:

.. kernel-doc:: net/sunrpc/xprt.c
   :export:

.. kernel-doc:: net/sunrpc/sched.c
   :export:

.. kernel-doc:: net/sunrpc/socklib.c
   :export:

.. kernel-doc:: net/sunrpc/stats.c
   :export:

.. kernel-doc:: net/sunrpc/rpc_pipe.c
   :export:

.. kernel-doc:: net/sunrpc/rpcb_clnt.c
   :export:

.. kernel-doc:: net/sunrpc/clnt.c
   :export:

Network device support
======================

Driver Support
--------------

.. kernel-doc:: net/core/dev.c
   :export:

.. kernel-doc:: net/ethernet/eth.c
   :export:

.. kernel-doc:: net/sched/sch_generic.c
   :export:

.. kernel-doc:: include/linux/etherdevice.h
   :internal:

.. kernel-doc:: include/linux/netdevice.h
   :internal:

PHY Support
-----------

.. kernel-doc:: drivers/net/phy/phy.c
   :export:

.. kernel-doc:: drivers/net/phy/phy.c
   :internal:

.. kernel-doc:: drivers/net/phy/phy-core.c
   :export:

.. kernel-doc:: drivers/net/phy/phy-c45.c
   :export:

.. kernel-doc:: include/linux/phy.h
   :internal:

.. kernel-doc:: drivers/net/phy/phy_device.c
   :export:

.. kernel-doc:: drivers/net/phy/phy_device.c
   :internal:

.. kernel-doc:: drivers/net/phy/mdio_bus.c
   :export:

.. kernel-doc:: drivers/net/phy/mdio_bus.c
   :internal:

PHYLINK
-------

  PHYLINK interfaces traditional network drivers with PHYLIB, fixed-links,
  and SFF modules (eg, hot-pluggable SFP) that may contain PHYs.  PHYLINK
  provides management of the link state and link modes.

.. kernel-doc:: include/linux/phylink.h
   :internal:

.. kernel-doc:: drivers/net/phy/phylink.c

SFP support
-----------

.. kernel-doc:: drivers/net/phy/sfp-bus.c
   :internal:

.. kernel-doc:: include/linux/sfp.h
   :internal:

.. kernel-doc:: drivers/net/phy/sfp-bus.c
   :export:
