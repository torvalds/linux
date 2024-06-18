.. SPDX-License-Identifier: GPL-2.0

=========================
BeOS filesystem for Linux
=========================

Document last updated: Dec 6, 2001

Warning
=======
Make sure you understand that this is alpha software.  This means that the
implementation is neither complete nor well-tested.

I DISCLAIM ALL RESPONSIBILITY FOR ANY POSSIBLE BAD EFFECTS OF THIS CODE!

License
=======
This software is covered by the GNU General Public License.
See the file COPYING for the complete text of the license.
Or the GNU website: <http://www.gnu.org/licenses/licenses.html>

Author
======
The largest part of the code written by Will Dyson <will_dyson@pobox.com>
He has been working on the code since Aug 13, 2001. See the changelog for
details.

Original Author: Makoto Kato <m_kato@ga2.so-net.ne.jp>

His original code can still be found at:
<http://hp.vector.co.jp/authors/VA008030/bfs/>

Does anyone know of a more current email address for Makoto? He doesn't
respond to the address given above...

This filesystem doesn't have a maintainer.

What is this Driver?
====================
This module implements the native filesystem of BeOS http://www.beincorporated.com/
for the linux 2.4.1 and later kernels. Currently it is a read-only
implementation.

Which is it, BFS or BEFS?
=========================
Be, Inc said, "BeOS Filesystem is officially called BFS, not BeFS".
But Unixware Boot Filesystem is called bfs, too. And they are already in
the kernel. Because of this naming conflict, on Linux the BeOS
filesystem is called befs.

How to Install
==============
step 1.  Install the BeFS  patch into the source code tree of linux.

Apply the patchfile to your kernel source tree.
Assuming that your kernel source is in /foo/bar/linux and the patchfile
is called patch-befs-xxx, you would do the following:

	cd /foo/bar/linux
	patch -p1 < /path/to/patch-befs-xxx

if the patching step fails (i.e. there are rejected hunks), you can try to
figure it out yourself (it shouldn't be hard), or mail the maintainer
(Will Dyson <will_dyson@pobox.com>) for help.

step 2.  Configuration & make kernel

The linux kernel has many compile-time options. Most of them are beyond the
scope of this document. I suggest the Kernel-HOWTO document as a good general
reference on this topic. http://www.linuxdocs.org/HOWTOs/Kernel-HOWTO-4.html

However, to use the BeFS module, you must enable it at configure time::

	cd /foo/bar/linux
	make menuconfig (or xconfig)

The BeFS module is not a standard part of the linux kernel, so you must first
enable support for experimental code under the "Code maturity level" menu.

Then, under the "Filesystems" menu will be an option called "BeFS
filesystem (experimental)", or something like that. Enable that option
(it is fine to make it a module).

Save your kernel configuration and then build your kernel.

step 3.  Install

See the kernel howto <http://www.linux.com/howto/Kernel-HOWTO.html> for
instructions on this critical step.

Using BFS
=========
To use the BeOS filesystem, use filesystem type 'befs'.

ex::

    mount -t befs /dev/fd0 /beos

Mount Options
=============

=============  ===========================================================
uid=nnn        All files in the partition will be owned by user id nnn.
gid=nnn	       All files in the partition will be in group nnn.
iocharset=xxx  Use xxx as the name of the NLS translation table.
debug          The driver will output debugging information to the syslog.
=============  ===========================================================

How to Get Latest Version
=========================

The latest version is currently available at:
<http://befs-driver.sourceforge.net/>

Any Known Bugs?
===============
As of Jan 20, 2002:

	None

Special Thanks
==============
Dominic Giampalo ... Writing "Practical file system design with Be filesystem"

Hiroyuki Yamada  ... Testing LinuxPPC.



