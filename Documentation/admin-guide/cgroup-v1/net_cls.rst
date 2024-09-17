=========================
Network classifier cgroup
=========================

The Network classifier cgroup provides an interface to
tag network packets with a class identifier (classid).

The Traffic Controller (tc) can be used to assign
different priorities to packets from different cgroups.
Also, Netfilter (iptables) can use this tag to perform
actions on such packets.

Creating a net_cls cgroups instance creates a net_cls.classid file.
This net_cls.classid value is initialized to 0.

You can write hexadecimal values to net_cls.classid; the format for these
values is 0xAAAABBBB; AAAA is the major handle number and BBBB
is the minor handle number.
Reading net_cls.classid yields a decimal result.

Example::

	mkdir /sys/fs/cgroup/net_cls
	mount -t cgroup -onet_cls net_cls /sys/fs/cgroup/net_cls
	mkdir /sys/fs/cgroup/net_cls/0
	echo 0x100001 >  /sys/fs/cgroup/net_cls/0/net_cls.classid

- setting a 10:1 handle::

	cat /sys/fs/cgroup/net_cls/0/net_cls.classid
	1048577

- configuring tc::

	tc qdisc add dev eth0 root handle 10: htb
	tc class add dev eth0 parent 10: classid 10:1 htb rate 40mbit

- creating traffic class 10:1::

	tc filter add dev eth0 parent 10: protocol ip prio 10 handle 1: cgroup

configuring iptables, basic example::

	iptables -A OUTPUT -m cgroup ! --cgroup 0x100001 -j DROP
