Introduction
============

ATA over Ethernet is a network protocol that provides simple access to
block storage on the LAN.

  http://support.coraid.com/documents/AoEr11.txt

The EtherDrive (R) HOWTO for 2.6 and 3.x kernels is found at ...

  http://support.coraid.com/support/linux/EtherDrive-2.6-HOWTO.html

It has many tips and hints!  Please see, especially, recommended
tunings for virtual memory:

  http://support.coraid.com/support/linux/EtherDrive-2.6-HOWTO-5.html#ss5.19

The aoetools are userland programs that are designed to work with this
driver.  The aoetools are on sourceforge.

  http://aoetools.sourceforge.net/

The scripts in this Documentation/admin-guide/aoe directory are intended to
document the use of the driver and are not necessary if you install
the aoetools.


Creating Device Nodes
=====================

  Users of udev should find the block device nodes created
  automatically, but to create all the necessary device nodes, use the
  udev configuration rules provided in udev.txt (in this directory).

  There is a udev-install.sh script that shows how to install these
  rules on your system.

  There is also an autoload script that shows how to edit
  /etc/modprobe.d/aoe.conf to ensure that the aoe module is loaded when
  necessary.  Preloading the aoe module is preferable to autoloading,
  however, because AoE discovery takes a few seconds.  It can be
  confusing when an AoE device is not present the first time the a
  command is run but appears a second later.

Using Device Nodes
==================

  "cat /dev/etherd/err" blocks, waiting for error diagnostic output,
  like any retransmitted packets.

  "echo eth2 eth4 > /dev/etherd/interfaces" tells the aoe driver to
  limit ATA over Ethernet traffic to eth2 and eth4.  AoE traffic from
  untrusted networks should be ignored as a matter of security.  See
  also the aoe_iflist driver option described below.

  "echo > /dev/etherd/discover" tells the driver to find out what AoE
  devices are available.

  In the future these character devices may disappear and be replaced
  by sysfs counterparts.  Using the commands in aoetools insulates
  users from these implementation details.

  The block devices are named like this::

	e{shelf}.{slot}
	e{shelf}.{slot}p{part}

  ... so that "e0.2" is the third blade from the left (slot 2) in the
  first shelf (shelf address zero).  That's the whole disk.  The first
  partition on that disk would be "e0.2p1".

Using sysfs
===========

  Each aoe block device in /sys/block has the extra attributes of
  state, mac, and netif.  The state attribute is "up" when the device
  is ready for I/O and "down" if detected but unusable.  The
  "down,closewait" state shows that the device is still open and
  cannot come up again until it has been closed.

  The mac attribute is the ethernet address of the remote AoE device.
  The netif attribute is the network interface on the localhost
  through which we are communicating with the remote AoE device.

  There is a script in this directory that formats this information in
  a convenient way.  Users with aoetools should use the aoe-stat
  command::

    root@makki root# sh Documentation/admin-guide/aoe/status.sh
       e10.0            eth3              up
       e10.1            eth3              up
       e10.2            eth3              up
       e10.3            eth3              up
       e10.4            eth3              up
       e10.5            eth3              up
       e10.6            eth3              up
       e10.7            eth3              up
       e10.8            eth3              up
       e10.9            eth3              up
        e4.0            eth1              up
        e4.1            eth1              up
        e4.2            eth1              up
        e4.3            eth1              up
        e4.4            eth1              up
        e4.5            eth1              up
        e4.6            eth1              up
        e4.7            eth1              up
        e4.8            eth1              up
        e4.9            eth1              up

  Use /sys/module/aoe/parameters/aoe_iflist (or better, the driver
  option discussed below) instead of /dev/etherd/interfaces to limit
  AoE traffic to the network interfaces in the given
  whitespace-separated list.  Unlike the old character device, the
  sysfs entry can be read from as well as written to.

  It's helpful to trigger discovery after setting the list of allowed
  interfaces.  The aoetools package provides an aoe-discover script
  for this purpose.  You can also directly use the
  /dev/etherd/discover special file described above.

Driver Options
==============

  There is a boot option for the built-in aoe driver and a
  corresponding module parameter, aoe_iflist.  Without this option,
  all network interfaces may be used for ATA over Ethernet.  Here is a
  usage example for the module parameter::

    modprobe aoe_iflist="eth1 eth3"

  The aoe_deadsecs module parameter determines the maximum number of
  seconds that the driver will wait for an AoE device to provide a
  response to an AoE command.  After aoe_deadsecs seconds have
  elapsed, the AoE device will be marked as "down".  A value of zero
  is supported for testing purposes and makes the aoe driver keep
  trying AoE commands forever.

  The aoe_maxout module parameter has a default of 128.  This is the
  maximum number of unresponded packets that will be sent to an AoE
  target at one time.

  The aoe_dyndevs module parameter defaults to 1, meaning that the
  driver will assign a block device minor number to a discovered AoE
  target based on the order of its discovery.  With dynamic minor
  device numbers in use, a greater range of AoE shelf and slot
  addresses can be supported.  Users with udev will never have to
  think about minor numbers.  Using aoe_dyndevs=0 allows device nodes
  to be pre-created using a static minor-number scheme with the
  aoe-mkshelf script in the aoetools.
