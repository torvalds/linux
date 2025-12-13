===========================
Documentation for /proc/sys
===========================

Copyright (c) 1998, 1999,  Rik van Riel <riel@nl.linux.org>

------------------------------------------------------------------------------

'Why', I hear you ask, 'would anyone even _want_ documentation
for them sysctl files? If anybody really needs it, it's all in
the source...'

Well, this documentation is written because some people either
don't know they need to tweak something, or because they don't
have the time or knowledge to read the source code.

Furthermore, the programmers who built sysctl have built it to
be actually used, not just for the fun of programming it :-)

------------------------------------------------------------------------------

Legal blurb:

As usual, there are two main things to consider:

1. you get what you pay for
2. it's free

The consequences are that I won't guarantee the correctness of
this document, and if you come to me complaining about how you
screwed up your system because of wrong documentation, I won't
feel sorry for you. I might even laugh at you...

But of course, if you _do_ manage to screw up your system using
only the sysctl options used in this file, I'd like to hear of
it. Not only to have a great laugh, but also to make sure that
you're the last RTFMing person to screw up.

In short, e-mail your suggestions, corrections and / or horror
stories to: <riel@nl.linux.org>

Rik van Riel.

--------------------------------------------------------------

Introduction
============

Sysctl is a means of configuring certain aspects of the kernel
at run-time, and the /proc/sys/ directory is there so that you
don't even need special tools to do it!
In fact, there are only four things needed to use these config
facilities:

- a running Linux system
- root access
- common sense (this is especially hard to come by these days)
- knowledge of what all those values mean

As a quick 'ls /proc/sys' will show, the directory consists of
several (arch-dependent?) subdirs. Each subdir is mainly about
one part of the kernel, so you can do configuration on a piece
by piece basis, or just some 'thematic frobbing'.

This documentation is about:

=============== ===============================================================
abi/		execution domains & personalities
<$ARCH>		tuning controls for various CPU architecture (e.g. csky, s390)
crypto/		<undocumented>
debug/		<undocumented>
dev/		device specific information (e.g. dev/cdrom/info)
fs/		specific filesystems
		filehandle, inode, dentry and quota tuning
		binfmt_misc <Documentation/admin-guide/binfmt-misc.rst>
kernel/		global kernel info / tuning
		miscellaneous stuff
		some architecture-specific controls
		security (LSM) stuff
net/		networking stuff, for documentation look in:
		<Documentation/networking/>
proc/		<empty>
sunrpc/		SUN Remote Procedure Call (NFS)
user/		Per user namespace limits
vm/		memory management tuning
		buffer and cache management
xen/		<undocumented>
=============== ===============================================================

These are the subdirs I have on my system or have been discovered by
searching through the source code. There might be more or other subdirs
in another setup. If you see another dir, I'd really like to hear about
it :-)

.. toctree::
   :maxdepth: 1

   abi
   fs
   kernel
   net
   sunrpc
   user
   vm
