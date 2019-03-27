In order for libpcap to be able to capture packets on a Linux system,
the "packet" protocol must be supported by your kernel.  If it is not,
you may get error messages such as

	modprobe: can't locate module net-pf-17

in "/var/adm/messages", or may get messages such as

	socket: Address family not supported by protocol

from applications using libpcap.

You must configure the kernel with the CONFIG_PACKET option for this
protocol; the following note is from the Linux "Configure.help" file for
the 2.0[.x] kernel:

	Packet socket
	CONFIG_PACKET
	  The Packet protocol is used by applications which communicate
	  directly with network devices without an intermediate network
	  protocol implemented in the kernel, e.g. tcpdump. If you want them
	  to work, choose Y.

	  This driver is also available as a module called af_packet.o ( =
	  code which can be inserted in and removed from the running kernel
	  whenever you want). If you want to compile it as a module, say M
	  here and read Documentation/modules.txt; if you use modprobe or
	  kmod, you may also want to add "alias net-pf-17 af_packet" to
	  /etc/modules.conf.

and the note for the 2.2[.x] kernel says:

	Packet socket
	CONFIG_PACKET
	  The Packet protocol is used by applications which communicate
	  directly with network devices without an intermediate network
	  protocol implemented in the kernel, e.g. tcpdump. If you want them
	  to work, choose Y. This driver is also available as a module called
	  af_packet.o ( = code which can be inserted in and removed from the
	  running kernel whenever you want). If you want to compile it as a
	  module, say M here and read Documentation/modules.txt.  You will
	  need to add 'alias net-pf-17 af_packet' to your /etc/conf.modules
	  file for the module version to function automatically.  If unsure,
	  say Y.

In addition, there is an option that, in 2.2 and later kernels, will
allow packet capture filters specified to programs such as tcpdump to be
executed in the kernel, so that packets that don't pass the filter won't
be copied from the kernel to the program, rather than having all packets
copied to the program and libpcap doing the filtering in user mode.

Copying packets from the kernel to the program consumes a significant
amount of CPU, so filtering in the kernel can reduce the overhead of
capturing packets if a filter has been specified that discards a
significant number of packets.  (If no filter is specified, it makes no
difference whether the filtering isn't performed in the kernel or isn't
performed in user mode. :-))

The option for this is the CONFIG_FILTER option; the "Configure.help"
file says:

	Socket filtering
	CONFIG_FILTER
	  The Linux Socket Filter is derived from the Berkeley Packet Filter.
	  If you say Y here, user-space programs can attach a filter to any
	  socket and thereby tell the kernel that it should allow or disallow
	  certain types of data to get through the socket. Linux Socket
	  Filtering works on all socket types except TCP for now. See the text
	  file linux/Documentation/networking/filter.txt for more information.
	  If unsure, say N.

Note that, by default, libpcap will, if libnl is present, build with it;
it uses libnl to support monitor mode on mac80211 devices.  There is a
configuration option to disable building with libnl, but, if that option
is chosen, the monitor-mode APIs (as used by tcpdump's "-I" flag, and as
will probably be used by other applications in the future) won't work
properly on mac80211 devices.

Linux's run-time linker allows shared libraries to be linked with other
shared libraries, which means that if an older version of a shared
library doesn't require routines from some other shared library, and a
later version of the shared library does require those routines, the
later version of the shared library can be linked with that other shared
library and, if it's otherwise binary-compatible with the older version,
can replace that older version without breaking applications built with
the older version, and without breaking configure scripts or the build
procedure for applications whose configure script doesn't use the
pcap-config script if they build with the shared library.  (The build
procedure for applications whose configure scripts use the pcap-config
script if present will not break even if they build with the static
library.)

Statistics:
Statistics reported by pcap are platform specific.  The statistics
reported by pcap_stats on Linux are as follows:

2.2.x
=====
ps_recv   Number of packets that were accepted by the pcap filter
ps_drop   Always 0, this statistic is not gatherd on this platform

2.4.x
=====
ps_recv   Number of packets that were accepted by the pcap filter
ps_drop   Number of packets that had passed filtering but were not
          passed on to pcap due to things like buffer shortage, etc.
          This is useful because these are packets you are interested in
          but won't be reported by, for example, tcpdump output.
