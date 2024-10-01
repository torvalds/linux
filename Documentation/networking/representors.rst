.. SPDX-License-Identifier: GPL-2.0
.. _representors:

=============================
Network Function Representors
=============================

This document describes the semantics and usage of representor netdevices, as
used to control internal switching on SmartNICs.  For the closely-related port
representors on physical (multi-port) switches, see
:ref:`Documentation/networking/switchdev.rst <switchdev>`.

Motivation
----------

Since the mid-2010s, network cards have started offering more complex
virtualisation capabilities than the legacy SR-IOV approach (with its simple
MAC/VLAN-based switching model) can support.  This led to a desire to offload
software-defined networks (such as OpenVSwitch) to these NICs to specify the
network connectivity of each function.  The resulting designs are variously
called SmartNICs or DPUs.

Network function representors bring the standard Linux networking stack to
virtual switches and IOV devices.  Just as each physical port of a Linux-
controlled switch has a separate netdev, so does each virtual port of a virtual
switch.
When the system boots, and before any offload is configured, all packets from
the virtual functions appear in the networking stack of the PF via the
representors.  The PF can thus always communicate freely with the virtual
functions.
The PF can configure standard Linux forwarding between representors, the uplink
or any other netdev (routing, bridging, TC classifiers).

Thus, a representor is both a control plane object (representing the function in
administrative commands) and a data plane object (one end of a virtual pipe).
As a virtual link endpoint, the representor can be configured like any other
netdevice; in some cases (e.g. link state) the representee will follow the
representor's configuration, while in others there are separate APIs to
configure the representee.

Definitions
-----------

This document uses the term "switchdev function" to refer to the PCIe function
which has administrative control over the virtual switch on the device.
Typically, this will be a PF, but conceivably a NIC could be configured to grant
these administrative privileges instead to a VF or SF (subfunction).
Depending on NIC design, a multi-port NIC might have a single switchdev function
for the whole device or might have a separate virtual switch, and hence
switchdev function, for each physical network port.
If the NIC supports nested switching, there might be separate switchdev
functions for each nested switch, in which case each switchdev function should
only create representors for the ports on the (sub-)switch it directly
administers.

A "representee" is the object that a representor represents.  So for example in
the case of a VF representor, the representee is the corresponding VF.

What does a representor do?
---------------------------

A representor has three main roles.

1. It is used to configure the network connection the representee sees, e.g.
   link up/down, MTU, etc.  For instance, bringing the representor
   administratively UP should cause the representee to see a link up / carrier
   on event.
2. It provides the slow path for traffic which does not hit any offloaded
   fast-path rules in the virtual switch.  Packets transmitted on the
   representor netdevice should be delivered to the representee; packets
   transmitted by the representee which fail to match any switching rule should
   be received on the representor netdevice.  (That is, there is a virtual pipe
   connecting the representor to the representee, similar in concept to a veth
   pair.)
   This allows software switch implementations (such as OpenVSwitch or a Linux
   bridge) to forward packets between representees and the rest of the network.
3. It acts as a handle by which switching rules (such as TC filters) can refer
   to the representee, allowing these rules to be offloaded.

The combination of 2) and 3) means that the behaviour (apart from performance)
should be the same whether a TC filter is offloaded or not.  E.g. a TC rule
on a VF representor applies in software to packets received on that representor
netdevice, while in hardware offload it would apply to packets transmitted by
the representee VF.  Conversely, a mirred egress redirect to a VF representor
corresponds in hardware to delivery directly to the representee VF.

What functions should have a representor?
-----------------------------------------

Essentially, for each virtual port on the device's internal switch, there
should be a representor.
Some vendors have chosen to omit representors for the uplink and the physical
network port, which can simplify usage (the uplink netdev becomes in effect the
physical port's representor) but does not generalise to devices with multiple
ports or uplinks.

Thus, the following should all have representors:

 - VFs belonging to the switchdev function.
 - Other PFs on the local PCIe controller, and any VFs belonging to them.
 - PFs and VFs on external PCIe controllers on the device (e.g. for any embedded
   System-on-Chip within the SmartNIC).
 - PFs and VFs with other personalities, including network block devices (such
   as a vDPA virtio-blk PF backed by remote/distributed storage), if (and only
   if) their network access is implemented through a virtual switch port. [#]_
   Note that such functions can require a representor despite the representee
   not having a netdev.
 - Subfunctions (SFs) belonging to any of the above PFs or VFs, if they have
   their own port on the switch (as opposed to using their parent PF's port).
 - Any accelerators or plugins on the device whose interface to the network is
   through a virtual switch port, even if they do not have a corresponding PCIe
   PF or VF.

This allows the entire switching behaviour of the NIC to be controlled through
representor TC rules.

It is a common misunderstanding to conflate virtual ports with PCIe virtual
functions or their netdevs.  While in simple cases there will be a 1:1
correspondence between VF netdevices and VF representors, more advanced device
configurations may not follow this.
A PCIe function which does not have network access through the internal switch
(not even indirectly through the hardware implementation of whatever services
the function provides) should *not* have a representor (even if it has a
netdev).
Such a function has no switch virtual port for the representor to configure or
to be the other end of the virtual pipe.
The representor represents the virtual port, not the PCIe function nor the 'end
user' netdevice.

.. [#] The concept here is that a hardware IP stack in the device performs the
   translation between block DMA requests and network packets, so that only
   network packets pass through the virtual port onto the switch.  The network
   access that the IP stack "sees" would then be configurable through tc rules;
   e.g. its traffic might all be wrapped in a specific VLAN or VxLAN.  However,
   any needed configuration of the block device *qua* block device, not being a
   networking entity, would not be appropriate for the representor and would
   thus use some other channel such as devlink.
   Contrast this with the case of a virtio-blk implementation which forwards the
   DMA requests unchanged to another PF whose driver then initiates and
   terminates IP traffic in software; in that case the DMA traffic would *not*
   run over the virtual switch and the virtio-blk PF should thus *not* have a
   representor.

How are representors created?
-----------------------------

The driver instance attached to the switchdev function should, for each virtual
port on the switch, create a pure-software netdevice which has some form of
in-kernel reference to the switchdev function's own netdevice or driver private
data (``netdev_priv()``).
This may be by enumerating ports at probe time, reacting dynamically to the
creation and destruction of ports at run time, or a combination of the two.

The operations of the representor netdevice will generally involve acting
through the switchdev function.  For example, ``ndo_start_xmit()`` might send
the packet through a hardware TX queue attached to the switchdev function, with
either packet metadata or queue configuration marking it for delivery to the
representee.

How are representors identified?
--------------------------------

The representor netdevice should *not* directly refer to a PCIe device (e.g.
through ``net_dev->dev.parent`` / ``SET_NETDEV_DEV()``), either of the
representee or of the switchdev function.
Instead, the driver should use the ``SET_NETDEV_DEVLINK_PORT`` macro to
assign a devlink port instance to the netdevice before registering the
netdevice; the kernel uses the devlink port to provide the ``phys_switch_id``
and ``phys_port_name`` sysfs nodes.
(Some legacy drivers implement ``ndo_get_port_parent_id()`` and
``ndo_get_phys_port_name()`` directly, but this is deprecated.)  See
:ref:`Documentation/networking/devlink/devlink-port.rst <devlink_port>` for the
details of this API.

It is expected that userland will use this information (e.g. through udev rules)
to construct an appropriately informative name or alias for the netdevice.  For
instance if the switchdev function is ``eth4`` then a representor with a
``phys_port_name`` of ``p0pf1vf2`` might be renamed ``eth4pf1vf2rep``.

There are as yet no established conventions for naming representors which do not
correspond to PCIe functions (e.g. accelerators and plugins).

How do representors interact with TC rules?
-------------------------------------------

Any TC rule on a representor applies (in software TC) to packets received by
that representor netdevice.  Thus, if the delivery part of the rule corresponds
to another port on the virtual switch, the driver may choose to offload it to
hardware, applying it to packets transmitted by the representee.

Similarly, since a TC mirred egress action targeting the representor would (in
software) send the packet through the representor (and thus indirectly deliver
it to the representee), hardware offload should interpret this as delivery to
the representee.

As a simple example, if ``PORT_DEV`` is the physical port representor and
``REP_DEV`` is a VF representor, the following rules::

    tc filter add dev $REP_DEV parent ffff: protocol ipv4 flower \
        action mirred egress redirect dev $PORT_DEV
    tc filter add dev $PORT_DEV parent ffff: protocol ipv4 flower skip_sw \
        action mirred egress mirror dev $REP_DEV

would mean that all IPv4 packets from the VF are sent out the physical port, and
all IPv4 packets received on the physical port are delivered to the VF in
addition to ``PORT_DEV``.  (Note that without ``skip_sw`` on the second rule,
the VF would get two copies, as the packet reception on ``PORT_DEV`` would
trigger the TC rule again and mirror the packet to ``REP_DEV``.)

On devices without separate port and uplink representors, ``PORT_DEV`` would
instead be the switchdev function's own uplink netdevice.

Of course the rules can (if supported by the NIC) include packet-modifying
actions (e.g. VLAN push/pop), which should be performed by the virtual switch.

Tunnel encapsulation and decapsulation are rather more complicated, as they
involve a third netdevice (a tunnel netdev operating in metadata mode, such as
a VxLAN device created with ``ip link add vxlan0 type vxlan external``) and
require an IP address to be bound to the underlay device (e.g. switchdev
function uplink netdev or port representor).  TC rules such as::

    tc filter add dev $REP_DEV parent ffff: flower \
        action tunnel_key set id $VNI src_ip $LOCAL_IP dst_ip $REMOTE_IP \
                              dst_port 4789 \
        action mirred egress redirect dev vxlan0
    tc filter add dev vxlan0 parent ffff: flower enc_src_ip $REMOTE_IP \
        enc_dst_ip $LOCAL_IP enc_key_id $VNI enc_dst_port 4789 \
        action tunnel_key unset action mirred egress redirect dev $REP_DEV

where ``LOCAL_IP`` is an IP address bound to ``PORT_DEV``, and ``REMOTE_IP`` is
another IP address on the same subnet, mean that packets sent by the VF should
be VxLAN encapsulated and sent out the physical port (the driver has to deduce
this by a route lookup of ``LOCAL_IP`` leading to ``PORT_DEV``, and also
perform an ARP/neighbour table lookup to find the MAC addresses to use in the
outer Ethernet frame), while UDP packets received on the physical port with UDP
port 4789 should be parsed as VxLAN and, if their VSID matches ``$VNI``,
decapsulated and forwarded to the VF.

If this all seems complicated, just remember the 'golden rule' of TC offload:
the hardware should ensure the same final results as if the packets were
processed through the slow path, traversed software TC (except ignoring any
``skip_hw`` rules and applying any ``skip_sw`` rules) and were transmitted or
received through the representor netdevices.

Configuring the representee's MAC
---------------------------------

The representee's link state is controlled through the representor.  Setting the
representor administratively UP or DOWN should cause carrier ON or OFF at the
representee.

Setting an MTU on the representor should cause that same MTU to be reported to
the representee.
(On hardware that allows configuring separate and distinct MTU and MRU values,
the representor MTU should correspond to the representee's MRU and vice-versa.)

Currently there is no way to use the representor to set the station permanent
MAC address of the representee; other methods available to do this include:

 - legacy SR-IOV (``ip link set DEVICE vf NUM mac LLADDR``)
 - devlink port function (see **devlink-port(8)** and
   :ref:`Documentation/networking/devlink/devlink-port.rst <devlink_port>`)
