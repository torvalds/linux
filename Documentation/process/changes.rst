.. _changes:

Minimal requirements to compile the Kernel
++++++++++++++++++++++++++++++++++++++++++

Intro
=====

This document is designed to provide a list of the minimum levels of
software necessary to run the current kernel version.

This document is originally based on my "Changes" file for 2.0.x kernels
and therefore owes credit to the same people as that file (Jared Mauch,
Axel Boldt, Alessandro Sigala, and countless other users all over the
'net).

Current Minimal Requirements
****************************

Upgrade to at **least** these software revisions before thinking you've
encountered a bug!  If you're unsure what version you're currently
running, the suggested command should tell you.

Again, keep in mind that this list assumes you are already functionally
running a Linux kernel.  Also, not all tools are necessary on all
systems; obviously, if you don't have any PC Card hardware, for example,
you probably needn't concern yourself with pcmciautils.

====================== ===============  ========================================
        Program        Minimal version       Command to check the version
====================== ===============  ========================================
GNU C                  5.1              gcc --version
Clang/LLVM (optional)  11.0.0           clang --version
Rust (optional)        1.73.0           rustc --version
bindgen (optional)     0.65.1           bindgen --version
GNU make               3.82             make --version
bash                   4.2              bash --version
binutils               2.25             ld -v
flex                   2.5.35           flex --version
bison                  2.0              bison --version
pahole                 1.16             pahole --version
util-linux             2.10o            fdformat --version
kmod                   13               depmod -V
e2fsprogs              1.41.4           e2fsck -V
jfsutils               1.1.3            fsck.jfs -V
reiserfsprogs          3.6.3            reiserfsck -V
xfsprogs               2.6.0            xfs_db -V
squashfs-tools         4.0              mksquashfs -version
btrfs-progs            0.18             btrfsck
pcmciautils            004              pccardctl -V
quota-tools            3.09             quota -V
PPP                    2.4.0            pppd --version
nfs-utils              1.0.5            showmount --version
procps                 3.2.0            ps --version
udev                   081              udevd --version
grub                   0.93             grub --version || grub-install --version
mcelog                 0.6              mcelog --version
iptables               1.4.2            iptables -V
openssl & libcrypto    1.0.0            openssl version
bc                     1.06.95          bc --version
Sphinx\ [#f1]_         2.4.4            sphinx-build --version
cpio                   any              cpio --version
GNU tar                1.28             tar --version
gtags (optional)       6.6.5            gtags --version
====================== ===============  ========================================

.. [#f1] Sphinx is needed only to build the Kernel documentation

Kernel compilation
******************

GCC
---

The gcc version requirements may vary depending on the type of CPU in your
computer.

Clang/LLVM (optional)
---------------------

The latest formal release of clang and LLVM utils (according to
`releases.llvm.org <https://releases.llvm.org>`_) are supported for building
kernels. Older releases aren't guaranteed to work, and we may drop workarounds
from the kernel that were used to support older versions. Please see additional
docs on :ref:`Building Linux with Clang/LLVM <kbuild_llvm>`.

Rust (optional)
---------------

A particular version of the Rust toolchain is required. Newer versions may or
may not work because the kernel depends on some unstable Rust features, for
the moment.

Each Rust toolchain comes with several "components", some of which are required
(like ``rustc``) and some that are optional. The ``rust-src`` component (which
is optional) needs to be installed to build the kernel. Other components are
useful for developing.

Please see Documentation/rust/quick-start.rst for instructions on how to
satisfy the build requirements of Rust support. In particular, the ``Makefile``
target ``rustavailable`` is useful to check why the Rust toolchain may not
be detected.

bindgen (optional)
------------------

``bindgen`` is used to generate the Rust bindings to the C side of the kernel.
It depends on ``libclang``.

Make
----

You will need GNU make 3.82 or later to build the kernel.

Bash
----

Some bash scripts are used for the kernel build.
Bash 4.2 or newer is needed.

Binutils
--------

Binutils 2.25 or newer is needed to build the kernel.

pkg-config
----------

The build system, as of 4.18, requires pkg-config to check for installed
kconfig tools and to determine flags settings for use in
'make {g,x}config'.  Previously pkg-config was being used but not
verified or documented.

Flex
----

Since Linux 4.16, the build system generates lexical analyzers
during build.  This requires flex 2.5.35 or later.


Bison
-----

Since Linux 4.16, the build system generates parsers
during build.  This requires bison 2.0 or later.

pahole:
-------

Since Linux 5.2, if CONFIG_DEBUG_INFO_BTF is selected, the build system
generates BTF (BPF Type Format) from DWARF in vmlinux, a bit later from kernel
modules as well.  This requires pahole v1.16 or later.

It is found in the 'dwarves' or 'pahole' distro packages or from
https://fedorapeople.org/~acme/dwarves/.

Perl
----

You will need perl 5 and the following modules: ``Getopt::Long``,
``Getopt::Std``, ``File::Basename``, and ``File::Find`` to build the kernel.

BC
--

You will need bc to build kernels 3.10 and higher


OpenSSL
-------

Module signing and external certificate handling use the OpenSSL program and
crypto library to do key creation and signature generation.

You will need openssl to build kernels 3.7 and higher if module signing is
enabled.  You will also need openssl development packages to build kernels 4.3
and higher.

Tar
---

GNU tar is needed if you want to enable access to the kernel headers via sysfs
(CONFIG_IKHEADERS).

gtags / GNU GLOBAL (optional)
-----------------------------

The kernel build requires GNU GLOBAL version 6.6.5 or later to generate
tag files through ``make gtags``.  This is due to its use of the gtags
``-C (--directory)`` flag.

System utilities
****************

Architectural changes
---------------------

DevFS has been obsoleted in favour of udev
(https://www.kernel.org/pub/linux/utils/kernel/hotplug/)

32-bit UID support is now in place.  Have fun!

Linux documentation for functions is transitioning to inline
documentation via specially-formatted comments near their
definitions in the source.  These comments can be combined with ReST
files the Documentation/ directory to make enriched documentation, which can
then be converted to PostScript, HTML, LaTex, ePUB and PDF files.
In order to convert from ReST format to a format of your choice, you'll need
Sphinx.

Util-linux
----------

New versions of util-linux provide ``fdisk`` support for larger disks,
support new options to mount, recognize more supported partition
types, have a fdformat which works with 2.4 kernels, and similar goodies.
You'll probably want to upgrade.

Ksymoops
--------

If the unthinkable happens and your kernel oopses, you may need the
ksymoops tool to decode it, but in most cases you don't.
It is generally preferred to build the kernel with ``CONFIG_KALLSYMS`` so
that it produces readable dumps that can be used as-is (this also
produces better output than ksymoops).  If for some reason your kernel
is not build with ``CONFIG_KALLSYMS`` and you have no way to rebuild and
reproduce the Oops with that option, then you can still decode that Oops
with ksymoops.

Mkinitrd
--------

These changes to the ``/lib/modules`` file tree layout also require that
mkinitrd be upgraded.

E2fsprogs
---------

The latest version of ``e2fsprogs`` fixes several bugs in fsck and
debugfs.  Obviously, it's a good idea to upgrade.

JFSutils
--------

The ``jfsutils`` package contains the utilities for the file system.
The following utilities are available:

- ``fsck.jfs`` - initiate replay of the transaction log, and check
  and repair a JFS formatted partition.

- ``mkfs.jfs`` - create a JFS formatted partition.

- other file system utilities are also available in this package.

Reiserfsprogs
-------------

The reiserfsprogs package should be used for reiserfs-3.6.x
(Linux kernels 2.4.x). It is a combined package and contains working
versions of ``mkreiserfs``, ``resize_reiserfs``, ``debugreiserfs`` and
``reiserfsck``. These utils work on both i386 and alpha platforms.

Xfsprogs
--------

The latest version of ``xfsprogs`` contains ``mkfs.xfs``, ``xfs_db``, and the
``xfs_repair`` utilities, among others, for the XFS filesystem.  It is
architecture independent and any version from 2.0.0 onward should
work correctly with this version of the XFS kernel code (2.6.0 or
later is recommended, due to some significant improvements).

PCMCIAutils
-----------

PCMCIAutils replaces ``pcmcia-cs``. It properly sets up
PCMCIA sockets at system startup and loads the appropriate modules
for 16-bit PCMCIA devices if the kernel is modularized and the hotplug
subsystem is used.

Quota-tools
-----------

Support for 32 bit uid's and gid's is required if you want to use
the newer version 2 quota format.  Quota-tools version 3.07 and
newer has this support.  Use the recommended version or newer
from the table above.

Intel IA32 microcode
--------------------

A driver has been added to allow updating of Intel IA32 microcode,
accessible as a normal (misc) character device.  If you are not using
udev you may need to::

  mkdir /dev/cpu
  mknod /dev/cpu/microcode c 10 184
  chmod 0644 /dev/cpu/microcode

as root before you can use this.  You'll probably also want to
get the user-space microcode_ctl utility to use with this.

udev
----

``udev`` is a userspace application for populating ``/dev`` dynamically with
only entries for devices actually present. ``udev`` replaces the basic
functionality of devfs, while allowing persistent device naming for
devices.

FUSE
----

Needs libfuse 2.4.0 or later.  Absolute minimum is 2.3.0 but mount
options ``direct_io`` and ``kernel_cache`` won't work.

Networking
**********

General changes
---------------

If you have advanced network configuration needs, you should probably
consider using the network tools from ip-route2.

Packet Filter / NAT
-------------------
The packet filtering and NAT code uses the same tools like the previous 2.4.x
kernel series (iptables).  It still includes backwards-compatibility modules
for 2.2.x-style ipchains and 2.0.x-style ipfwadm.

PPP
---

The PPP driver has been restructured to support multilink and to
enable it to operate over diverse media layers.  If you use PPP,
upgrade pppd to at least 2.4.0.

If you are not using udev, you must have the device file /dev/ppp
which can be made by::

  mknod /dev/ppp c 108 0

as root.

NFS-utils
---------

In ancient (2.4 and earlier) kernels, the nfs server needed to know
about any client that expected to be able to access files via NFS.  This
information would be given to the kernel by ``mountd`` when the client
mounted the filesystem, or by ``exportfs`` at system startup.  exportfs
would take information about active clients from ``/var/lib/nfs/rmtab``.

This approach is quite fragile as it depends on rmtab being correct
which is not always easy, particularly when trying to implement
fail-over.  Even when the system is working well, ``rmtab`` suffers from
getting lots of old entries that never get removed.

With modern kernels we have the option of having the kernel tell mountd
when it gets a request from an unknown host, and mountd can give
appropriate export information to the kernel.  This removes the
dependency on ``rmtab`` and means that the kernel only needs to know about
currently active clients.

To enable this new functionality, you need to::

  mount -t nfsd nfsd /proc/fs/nfsd

before running exportfs or mountd.  It is recommended that all NFS
services be protected from the internet-at-large by a firewall where
that is possible.

mcelog
------

On x86 kernels the mcelog utility is needed to process and log machine check
events when ``CONFIG_X86_MCE`` is enabled. Machine check events are errors
reported by the CPU. Processing them is strongly encouraged.

Kernel documentation
********************

Sphinx
------

Please see :ref:`sphinx_install` in :ref:`Documentation/doc-guide/sphinx.rst <sphinxdoc>`
for details about Sphinx requirements.

rustdoc
-------

``rustdoc`` is used to generate the documentation for Rust code. Please see
Documentation/rust/general-information.rst for more information.

Getting updated software
========================

Kernel compilation
******************

gcc
---

- <ftp://ftp.gnu.org/gnu/gcc/>

Clang/LLVM
----------

- :ref:`Getting LLVM <getting_llvm>`.

Rust
----

- Documentation/rust/quick-start.rst.

bindgen
-------

- Documentation/rust/quick-start.rst.

Make
----

- <ftp://ftp.gnu.org/gnu/make/>

Bash
----

- <ftp://ftp.gnu.org/gnu/bash/>

Binutils
--------

- <https://www.kernel.org/pub/linux/devel/binutils/>

Flex
----

- <https://github.com/westes/flex/releases>

Bison
-----

- <ftp://ftp.gnu.org/gnu/bison/>

OpenSSL
-------

- <https://www.openssl.org/>

System utilities
****************

Util-linux
----------

- <https://www.kernel.org/pub/linux/utils/util-linux/>

Kmod
----

- <https://www.kernel.org/pub/linux/utils/kernel/kmod/>
- <https://git.kernel.org/pub/scm/utils/kernel/kmod/kmod.git>

Ksymoops
--------

- <https://www.kernel.org/pub/linux/utils/kernel/ksymoops/v2.4/>

Mkinitrd
--------

- <https://code.launchpad.net/initrd-tools/main>

E2fsprogs
---------

- <https://www.kernel.org/pub/linux/kernel/people/tytso/e2fsprogs/>
- <https://git.kernel.org/pub/scm/fs/ext2/e2fsprogs.git/>

JFSutils
--------

- <https://jfs.sourceforge.net/>

Reiserfsprogs
-------------

- <https://git.kernel.org/pub/scm/linux/kernel/git/jeffm/reiserfsprogs.git/>

Xfsprogs
--------

- <https://git.kernel.org/pub/scm/fs/xfs/xfsprogs-dev.git>
- <https://www.kernel.org/pub/linux/utils/fs/xfs/xfsprogs/>

Pcmciautils
-----------

- <https://www.kernel.org/pub/linux/utils/kernel/pcmcia/>

Quota-tools
-----------

- <https://sourceforge.net/projects/linuxquota/>


Intel P6 microcode
------------------

- <https://downloadcenter.intel.com/>

udev
----

- <https://www.freedesktop.org/software/systemd/man/udev.html>

FUSE
----

- <https://github.com/libfuse/libfuse/releases>

mcelog
------

- <https://www.mcelog.org/>

cpio
----

- <https://www.gnu.org/software/cpio/>

Networking
**********

PPP
---

- <https://download.samba.org/pub/ppp/>
- <https://git.ozlabs.org/?p=ppp.git>
- <https://github.com/paulusmack/ppp/>

NFS-utils
---------

- <https://sourceforge.net/project/showfiles.php?group_id=14>
- <https://nfs.sourceforge.net/>

Iptables
--------

- <https://netfilter.org/projects/iptables/index.html>

Ip-route2
---------

- <https://www.kernel.org/pub/linux/utils/net/iproute2/>

OProfile
--------

- <https://oprofile.sf.net/download/>

Kernel documentation
********************

Sphinx
------

- <https://www.sphinx-doc.org/>
