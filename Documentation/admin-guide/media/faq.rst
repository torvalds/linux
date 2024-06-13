.. SPDX-License-Identifier: GPL-2.0

FAQ
===

.. note::

     1. With Digital TV, a single physical channel may have different
	contents inside it. The specs call each one as a *service*.
	This is what a TV user would call "channel". So, in order to
	avoid confusion, we're calling *transponders* as the physical
	channel on this FAQ, and *services* for the logical channel.
     2. The LinuxTV community maintains some Wiki pages with contain
        a lot of information related to the media subsystem. If you
        don't find an answer for your needs here, it is likely that
        you'll be able to get something useful there. It is hosted
	at:

	https://www.linuxtv.org/wiki/

Some very frequently asked questions about Linux Digital TV support

1. The signal seems to die a few seconds after tuning.

	It's not a bug, it's a feature. Because the frontends have
	significant power requirements (and hence get very hot), they
	are powered down if they are unused (i.e. if the frontend device
	is closed). The ``dvb-core`` module parameter ``dvb_shutdown_timeout``
	allow you to change the timeout (default 5 seconds). Setting the
	timeout to 0 disables the timeout feature.

2. How can I watch TV?

	Together with the Linux Kernel, the Digital TV developers support
	some simple utilities which are mainly intended for testing
	and to demonstrate how the DVB API works. This is called DVB v5
	tools and are grouped together with the ``v4l-utils`` git repository:

	    https://git.linuxtv.org/v4l-utils.git/

	You can find more information at the LinuxTV wiki:

	    https://www.linuxtv.org/wiki/index.php/DVBv5_Tools

	The first step is to get a list of services that are transmitted.

	This is done by using several existing tools. You can use
	for example the ``dvbv5-scan`` tool. You can find more information
	about it at:

	    https://www.linuxtv.org/wiki/index.php/Dvbv5-scan

	There are some other applications like ``w_scan`` [#]_ that do a
	blind scan, trying hard to find all possible channels, but
	those consumes a large amount of time to run.

	.. [#] https://www.linuxtv.org/wiki/index.php/W_scan

	Also, some applications like ``kaffeine`` have their own code
	to scan for services. So, you don't need to use an external
	application to obtain such list.

	Most of such tools need a file containing a list of channel
	transponders available on your area. So, LinuxTV developers
	maintain tables of Digital TV channel transponders, receiving
	patches from the community to keep them updated.

	This list is hosted at:

	    https://git.linuxtv.org/dtv-scan-tables.git

	And packaged on several distributions.

	Kaffeine has some blind scan support for some terrestrial standards.
	It also relies on DTV scan tables, although it contains a copy
	of it internally (and, if requested by the user, it will download
	newer versions of it).

	If you are lucky you can just use one of the supplied channel
	transponders. If not, you may need to seek for such info at
	the Internet and create a new file. There are several sites with
	contains physical channel lists. For cable and satellite, usually
	knowing how to tune into a single channel is enough for the
	scanning tool to identify the other channels. On some places,
	this could also work for terrestrial transmissions.

	Once you have a transponders list, you need to generate a services
	list with a tool like ``dvbv5-scan``.

	Almost all modern Digital TV cards don't have built-in hardware
	MPEG-decoders. So, it is up to the application to get a MPEG-TS
	stream provided by the board, split it into audio, video and other
	data and decode.

3. Which Digital TV applications exist?

	Several media player applications are capable of tuning into
	digital TV channels, including Kaffeine, Vlc, mplayer and MythTV.

	Kaffeine aims to be very user-friendly, and it is maintained
	by one of the Kernel driver developers.

	A comprehensive list of those and other apps can be found at:

	    https://www.linuxtv.org/wiki/index.php/TV_Related_Software

	Some of the most popular ones are linked below:

	https://kde.org/applications/multimedia/org.kde.kaffeine
		KDE media player, focused on Digital TV support

	https://www.linuxtv.org/vdrwiki/index.php/Main_Page
		Klaus Schmidinger's Video Disk Recorder

	https://linuxtv.org/downloads and https://git.linuxtv.org/
		Digital TV and other media-related applications and
		Kernel drivers. The ``v4l-utils`` package there contains
		several swiss knife tools for using with Digital TV.

	http://sourceforge.net/projects/dvbtools/
		Dave Chapman's dvbtools package, including
		dvbstream and dvbtune

	http://www.dbox2.info/
		LinuxDVB on the dBox2

	http://www.tuxbox.org/
		the TuxBox CVS many interesting DVB applications and the dBox2
		DVB source

	http://www.nenie.org/misc/mpsys/
		MPSYS: a MPEG2 system library and tools

	https://www.videolan.org/vlc/index.pt.html
		Vlc

	http://mplayerhq.hu/
		MPlayer

	http://xine.sourceforge.net/ and http://xinehq.de/
		Xine

	http://www.mythtv.org/
		MythTV - analog TV and digital TV PVR

	http://dvbsnoop.sourceforge.net/
		DVB sniffer program to monitor, analyze, debug, dump
		or view dvb/mpeg/dsm-cc/mhp stream information (TS,
		PES, SECTION)

4. Can't get a signal tuned correctly

	That could be due to a lot of problems. On my personal experience,
	usually TV cards need stronger signals than TV sets, and are more
	sensitive to noise. So, perhaps you just need a better antenna or
	cabling. Yet, it could also be some hardware or driver issue.

	For example, if you are using a Technotrend/Hauppauge DVB-C card
	*without* analog module, you might have to use module parameter
	adac=-1 (dvb-ttpci.o).

	Please see the FAQ page at linuxtv.org, as it could contain some
	valuable information:

	    https://www.linuxtv.org/wiki/index.php/FAQ_%26_Troubleshooting

	If that doesn't work, check at the linux-media ML archives, to
	see if someone else had a similar problem with your hardware
	and/or digital TV service provider:

	    https://lore.kernel.org/linux-media/

	If none of this works, you can try sending an e-mail to the
	linux-media ML and see if someone else could shed some light.
	The e-mail is linux-media AT vger.kernel.org.

5. The dvb_net device doesn't give me any packets at all

	Run ``tcpdump`` on the ``dvb0_0`` interface. This sets the interface
	into promiscuous mode so it accepts any packets from the PID
	you have configured with the ``dvbnet`` utility. Check if there
	are any packets with the IP addr and MAC addr you have
	configured with ``ifconfig`` or with ``ip addr``.

	If ``tcpdump`` doesn't give you any output, check the statistics
	which ``ifconfig`` or ``netstat -ni`` outputs. (Note: If the MAC
	address is wrong, ``dvb_net`` won't get any input; thus you have to
	run ``tcpdump`` before checking the statistics.) If there are no
	packets at all then maybe the PID is wrong. If there are error packets,
	then either the PID is wrong or the stream does not conform to
	the MPE standard (EN 301 192, http://www.etsi.org/). You can
	use e.g. ``dvbsnoop`` for debugging.

6. The ``dvb_net`` device doesn't give me any multicast packets

	Check your routes if they include the multicast address range.
	Additionally make sure that "source validation by reversed path
	lookup" is disabled::

	  $ "echo 0 > /proc/sys/net/ipv4/conf/dvb0/rp_filter"

7. What are all those modules that need to be loaded?

	In order to make it more flexible and support different hardware
	combinations, the media subsystem is written on a modular way.

	So, besides the Digital TV hardware module for the main chipset,
	it also needs to load a frontend driver, plus the Digital TV
	core. If the board also has remote controller, it will also
	need the remote controller core and the remote controller tables.
	The same happens if the board has support for analog TV: the
	core support for video4linux need to be loaded.

	The actual module names are Linux-kernel version specific, as,
	from time to time, things change, in order to make the media
	support more flexible.
