.. _admin_devices:

Linux allocated devices (4.x+ version)
======================================

This list is the Linux Device List, the official registry of allocated
device numbers and ``/dev`` directory nodes for the Linux operating
system.

The LaTeX version of this document is no longer maintained, nor is
the document that used to reside at lanana.org.  This version in the
mainline Linux kernel is the master document.  Updates shall be sent
as patches to the kernel maintainers (see the
:ref:`Documentation/process/submitting-patches.rst <submittingpatches>` document).
Specifically explore the sections titled "CHAR and MISC DRIVERS", and
"BLOCK LAYER" in the MAINTAINERS file to find the right maintainers
to involve for character and block devices.

This document is included by reference into the Filesystem Hierarchy
Standard (FHS).	 The FHS is available from https://www.pathname.com/fhs/.

Allocations marked (68k/Amiga) apply to Linux/68k on the Amiga
platform only.	Allocations marked (68k/Atari) apply to Linux/68k on
the Atari platform only.

This document is in the public domain.	The authors requests, however,
that semantically altered versions are not distributed without
permission of the authors, assuming the authors can be contacted without
an unreasonable effort.


.. attention::

  DEVICE DRIVERS AUTHORS PLEASE READ THIS

  Linux now has extensive support for dynamic allocation of device numbering
  and can use ``sysfs`` and ``udev`` (``systemd``) to handle the naming needs.
  There are still some exceptions in the serial and boot device area. Before
  asking   for a device number make sure you actually need one.

  To have a major number allocated, or a minor number in situations
  where that applies (e.g. busmice), please submit a patch and send to
  the authors as indicated above.

  Keep the description of the device *in the same format
  as this list*. The reason for this is that it is the only way we have
  found to ensure we have all the requisite information to publish your
  device and avoid conflicts.

  Finally, sometimes we have to play "namespace police."  Please don't be
  offended.  We often get submissions for ``/dev`` names that would be bound
  to cause conflicts down the road.  We are trying to avoid getting in a
  situation where we would have to suffer an incompatible forward
  change.  Therefore, please consult with us **before** you make your
  device names and numbers in any way public, at least to the point
  where it would be at all difficult to get them changed.

  Your cooperation is appreciated.

.. include:: devices.txt
   :literal:

Additional ``/dev/`` directory entries
--------------------------------------

This section details additional entries that should or may exist in
the /dev directory.  It is preferred that symbolic links use the same
form (absolute or relative) as is indicated here.  Links are
classified as "hard" or "symbolic" depending on the preferred type of
link; if possible, the indicated type of link should be used.

Compulsory links
++++++++++++++++

These links should exist on all systems:

=============== =============== =============== ===============================
/dev/fd		/proc/self/fd	symbolic	File descriptors
/dev/stdin	fd/0		symbolic	stdin file descriptor
/dev/stdout	fd/1		symbolic	stdout file descriptor
/dev/stderr	fd/2		symbolic	stderr file descriptor
/dev/nfsd	socksys		symbolic	Required by iBCS-2
/dev/X0R	null		symbolic	Required by iBCS-2
=============== =============== =============== ===============================

Note: ``/dev/X0R`` is <letter X>-<digit 0>-<letter R>.

Recommended links
+++++++++++++++++

It is recommended that these links exist on all systems:


=============== =============== =============== ===============================
/dev/core	/proc/kcore	symbolic	Backward compatibility
/dev/ramdisk	ram0		symbolic	Backward compatibility
/dev/ftape	qft0		symbolic	Backward compatibility
/dev/bttv0	video0		symbolic	Backward compatibility
/dev/radio	radio0		symbolic	Backward compatibility
/dev/i2o*	/dev/i2o/*	symbolic	Backward compatibility
/dev/scd?	sr?		hard		Alternate SCSI CD-ROM name
=============== =============== =============== ===============================

Locally defined links
+++++++++++++++++++++

The following links may be established locally to conform to the
configuration of the system.  This is merely a tabulation of existing
practice, and does not constitute a recommendation.  However, if they
exist, they should have the following uses.

=============== =============== =============== ===============================
/dev/mouse	mouse port	symbolic	Current mouse device
/dev/tape	tape device	symbolic	Current tape device
/dev/cdrom	CD-ROM device	symbolic	Current CD-ROM device
/dev/cdwriter	CD-writer	symbolic	Current CD-writer device
/dev/scanner	scanner		symbolic	Current scanner device
/dev/modem	modem port	symbolic	Current dialout device
/dev/root	root device	symbolic	Current root filesystem
/dev/swap	swap device	symbolic	Current swap device
=============== =============== =============== ===============================

``/dev/modem`` should not be used for a modem which supports dialin as
well as dialout, as it tends to cause lock file problems.  If it
exists, ``/dev/modem`` should point to the appropriate primary TTY device
(the use of the alternate callout devices is deprecated).

For SCSI devices, ``/dev/tape`` and ``/dev/cdrom`` should point to the
*cooked* devices (``/dev/st*`` and ``/dev/sr*``, respectively), whereas
``/dev/cdwriter`` and /dev/scanner should point to the appropriate generic
SCSI devices (/dev/sg*).

``/dev/mouse`` may point to a primary serial TTY device, a hardware mouse
device, or a socket for a mouse driver program (e.g. ``/dev/gpmdata``).

Sockets and pipes
+++++++++++++++++

Non-transient sockets and named pipes may exist in /dev.  Common entries are:

=============== =============== ===============================================
/dev/printer	socket		lpd local socket
/dev/log	socket		syslog local socket
/dev/gpmdata	socket		gpm mouse multiplexer
=============== =============== ===============================================

Mount points
++++++++++++

The following names are reserved for mounting special filesystems
under /dev.  These special filesystems provide kernel interfaces that
cannot be provided with standard device nodes.

=============== =============== ===============================================
/dev/pts	devpts		PTY slave filesystem
/dev/shm	tmpfs		POSIX shared memory maintenance access
=============== =============== ===============================================

Terminal devices
----------------

Terminal, or TTY devices are a special class of character devices.  A
terminal device is any device that could act as a controlling terminal
for a session; this includes virtual consoles, serial ports, and
pseudoterminals (PTYs).

All terminal devices share a common set of capabilities known as line
disciplines; these include the common terminal line discipline as well
as SLIP and PPP modes.

All terminal devices are named similarly; this section explains the
naming and use of the various types of TTYs.  Note that the naming
conventions include several historical warts; some of these are
Linux-specific, some were inherited from other systems, and some
reflect Linux outgrowing a borrowed convention.

A hash mark (``#``) in a device name is used here to indicate a decimal
number without leading zeroes.

Virtual consoles and the console device
+++++++++++++++++++++++++++++++++++++++

Virtual consoles are full-screen terminal displays on the system video
monitor.  Virtual consoles are named ``/dev/tty#``, with numbering
starting at ``/dev/tty1``; ``/dev/tty0`` is the current virtual console.
``/dev/tty0`` is the device that should be used to access the system video
card on those architectures for which the frame buffer devices
(``/dev/fb*``) are not applicable. Do not use ``/dev/console``
for this purpose.

The console device, ``/dev/console``, is the device to which system
messages should be sent, and on which logins should be permitted in
single-user mode.  Starting with Linux 2.1.71, ``/dev/console`` is managed
by the kernel; for previous versions it should be a symbolic link to
either ``/dev/tty0``, a specific virtual console such as ``/dev/tty1``, or to
a serial port primary (``tty*``, not ``cu*``) device, depending on the
configuration of the system.

Serial ports
++++++++++++

Serial ports are RS-232 serial ports and any device which simulates
one, either in hardware (such as internal modems) or in software (such
as the ISDN driver.)  Under Linux, each serial ports has two device
names, the primary or callin device and the alternate or callout one.
Each kind of device is indicated by a different letter.	 For any
letter X, the names of the devices are ``/dev/ttyX#`` and ``/dev/cux#``,
respectively; for historical reasons, ``/dev/ttyS#`` and ``/dev/ttyC#``
correspond to ``/dev/cua#`` and ``/dev/cub#``. In the future, it should be
expected that multiple letters will be used; all letters will be upper
case for the "tty" device (e.g. ``/dev/ttyDP#``) and lower case for the
"cu" device (e.g. ``/dev/cudp#``).

The names ``/dev/ttyQ#`` and ``/dev/cuq#`` are reserved for local use.

The alternate devices provide for kernel-based exclusion and somewhat
different defaults than the primary devices.  Their main purpose is to
allow the use of serial ports with programs with no inherent or broken
support for serial ports.  Their use is deprecated, and they may be
removed from a future version of Linux.

Arbitration of serial ports is provided by the use of lock files with
the names ``/var/lock/LCK..ttyX#``. The contents of the lock file should
be the PID of the locking process as an ASCII number.

It is common practice to install links such as /dev/modem
which point to serial ports.  In order to ensure proper locking in the
presence of these links, it is recommended that software chase
symlinks and lock all possible names; additionally, it is recommended
that a lock file be installed with the corresponding alternate
device.	 In order to avoid deadlocks, it is recommended that the locks
are acquired in the following order, and released in the reverse:

	1. The symbolic link name, if any (``/var/lock/LCK..modem``)
	2. The "tty" name (``/var/lock/LCK..ttyS2``)
	3. The alternate device name (``/var/lock/LCK..cua2``)

In the case of nested symbolic links, the lock files should be
installed in the order the symlinks are resolved.

Under no circumstances should an application hold a lock while waiting
for another to be released.  In addition, applications which attempt
to create lock files for the corresponding alternate device names
should take into account the possibility of being used on a non-serial
port TTY, for which no alternate device would exist.

Pseudoterminals (PTYs)
++++++++++++++++++++++

Pseudoterminals, or PTYs, are used to create login sessions or provide
other capabilities requiring a TTY line discipline (including SLIP or
PPP capability) to arbitrary data-generation processes.	 Each PTY has
a master side, named ``/dev/pty[p-za-e][0-9a-f]``, and a slave side, named
``/dev/tty[p-za-e][0-9a-f]``.  The kernel arbitrates the use of PTYs by
allowing each master side to be opened only once.

Once the master side has been opened, the corresponding slave device
can be used in the same manner as any TTY device.  The master and
slave devices are connected by the kernel, generating the equivalent
of a bidirectional pipe with TTY capabilities.

Recent versions of the Linux kernels and GNU libc contain support for
the System V/Unix98 naming scheme for PTYs, which assigns a common
device, ``/dev/ptmx``, to all the masters (opening it will automatically
give you a previously unassigned PTY) and a subdirectory, ``/dev/pts``,
for the slaves; the slaves are named with decimal integers (``/dev/pts/#``
in our notation).  This removes the problem of exhausting the
namespace and enables the kernel to automatically create the device
nodes for the slaves on demand using the "devpts" filesystem.
