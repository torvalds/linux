.. SPDX-License-Identifier: GPL-2.0

============
NET_FAILOVER
============

Overview
========

The net_failover driver provides an automated failover mechanism via APIs
to create and destroy a failover master netdev and manages a primary and
standby slave netdevs that get registered via the generic failover
infrastructure.

The failover netdev acts a master device and controls 2 slave devices. The
original paravirtual interface is registered as 'standby' slave netdev and
a passthru/vf device with the same MAC gets registered as 'primary' slave
netdev. Both 'standby' and 'failover' netdevs are associated with the same
'pci' device. The user accesses the network interface via 'failover' netdev.
The 'failover' netdev chooses 'primary' netdev as default for transmits when
it is available with link up and running.

This can be used by paravirtual drivers to enable an alternate low latency
datapath. It also enables hypervisor controlled live migration of a VM with
direct attached VF by failing over to the paravirtual datapath when the VF
is unplugged.

virtio-net accelerated datapath: STANDBY mode
=============================================

net_failover enables hypervisor controlled accelerated datapath to virtio-net
enabled VMs in a transparent manner with no/minimal guest userspace changes.

To support this, the hypervisor needs to enable VIRTIO_NET_F_STANDBY
feature on the virtio-net interface and assign the same MAC address to both
virtio-net and VF interfaces.

Here is an example XML snippet that shows such configuration.
::

  <interface type='network'>
    <mac address='52:54:00:00:12:53'/>
    <source network='enp66s0f0_br'/>
    <target dev='tap01'/>
    <model type='virtio'/>
    <driver name='vhost' queues='4'/>
    <link state='down'/>
    <address type='pci' domain='0x0000' bus='0x00' slot='0x0a' function='0x0'/>
  </interface>
  <interface type='hostdev' managed='yes'>
    <mac address='52:54:00:00:12:53'/>
    <source>
      <address type='pci' domain='0x0000' bus='0x42' slot='0x02' function='0x5'/>
    </source>
    <address type='pci' domain='0x0000' bus='0x00' slot='0x0b' function='0x0'/>
  </interface>

Booting a VM with the above configuration will result in the following 3
netdevs created in the VM.
::

  4: ens10: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP group default qlen 1000
      link/ether 52:54:00:00:12:53 brd ff:ff:ff:ff:ff:ff
      inet 192.168.12.53/24 brd 192.168.12.255 scope global dynamic ens10
         valid_lft 42482sec preferred_lft 42482sec
      inet6 fe80::97d8:db2:8c10:b6d6/64 scope link
         valid_lft forever preferred_lft forever
  5: ens10nsby: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc fq_codel master ens10 state UP group default qlen 1000
      link/ether 52:54:00:00:12:53 brd ff:ff:ff:ff:ff:ff
  7: ens11: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc mq master ens10 state UP group default qlen 1000
      link/ether 52:54:00:00:12:53 brd ff:ff:ff:ff:ff:ff

ens10 is the 'failover' master netdev, ens10nsby and ens11 are the slave
'standby' and 'primary' netdevs respectively.

Live Migration of a VM with SR-IOV VF & virtio-net in STANDBY mode
==================================================================

net_failover also enables hypervisor controlled live migration to be supported
with VMs that have direct attached SR-IOV VF devices by automatic failover to
the paravirtual datapath when the VF is unplugged.

Here is a sample script that shows the steps to initiate live migration on
the source hypervisor.
::

  # cat vf_xml
  <interface type='hostdev' managed='yes'>
    <mac address='52:54:00:00:12:53'/>
    <source>
      <address type='pci' domain='0x0000' bus='0x42' slot='0x02' function='0x5'/>
    </source>
    <address type='pci' domain='0x0000' bus='0x00' slot='0x0b' function='0x0'/>
  </interface>

  # Source Hypervisor
  #!/bin/bash

  DOMAIN=fedora27-tap01
  PF=enp66s0f0
  VF_NUM=5
  TAP_IF=tap01
  VF_XML=

  MAC=52:54:00:00:12:53
  ZERO_MAC=00:00:00:00:00:00

  virsh domif-setlink $DOMAIN $TAP_IF up
  bridge fdb del $MAC dev $PF master
  virsh detach-device $DOMAIN $VF_XML
  ip link set $PF vf $VF_NUM mac $ZERO_MAC

  virsh migrate --live $DOMAIN qemu+ssh://$REMOTE_HOST/system

  # Destination Hypervisor
  #!/bin/bash

  virsh attach-device $DOMAIN $VF_XML
  virsh domif-setlink $DOMAIN $TAP_IF down
