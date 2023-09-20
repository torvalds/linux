.. SPDX-License-Identifier: (GPL-2.0+ OR CC-BY-4.0)
.. [see the bottom of this file for redistribution information]

===========================================
How to quickly build a trimmed Linux kernel
===========================================

This guide explains how to swiftly build Linux kernels that are ideal for
testing purposes, but perfectly fine for day-to-day use, too.

The essence of the process (aka 'TL;DR')
========================================

*[If you are new to compiling Linux, ignore this TLDR and head over to the next
section below: it contains a step-by-step guide, which is more detailed, but
still brief and easy to follow; that guide and its accompanying reference
section also mention alternatives, pitfalls, and additional aspects, all of
which might be relevant for you.]*

If your system uses techniques like Secure Boot, prepare it to permit starting
self-compiled Linux kernels; install compilers and everything else needed for
building Linux; make sure to have 12 Gigabyte free space in your home directory.
Now run the following commands to download fresh Linux mainline sources, which
you then use to configure, build and install your own kernel::

    git clone --depth 1 -b master \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git ~/linux/
    cd ~/linux/
    # Hint: if you want to apply patches, do it at this point. See below for details.
    # Hint: it's recommended to tag your build at this point. See below for details.
    yes "" | make localmodconfig
    # Hint: at this point you might want to adjust the build configuration; you'll
    #   have to, if you are running Debian. See below for details.
    make -j $(nproc --all)
    # Note: on many commodity distributions the next command suffices, but on Arch
    #   Linux, its derivatives, and some others it does not. See below for details.
    command -v installkernel && sudo make modules_install install
    reboot

If you later want to build a newer mainline snapshot, use these commands::

    cd ~/linux/
    git fetch --depth 1 origin
    # Note: the next command will discard any changes you did to the code:
    git checkout --force --detach origin/master
    # Reminder: if you want to (re)apply patches, do it at this point.
    # Reminder: you might want to add or modify a build tag at this point.
    make olddefconfig
    make -j $(nproc --all)
    # Reminder: the next command on some distributions does not suffice.
    command -v installkernel && sudo make modules_install install
    reboot

Step-by-step guide
==================

Compiling your own Linux kernel is easy in principle. There are various ways to
do it. Which of them actually work and is the best depends on the circumstances.

This guide describes a way perfectly suited for those who want to quickly
install Linux from sources without being bothered by complicated details; the
goal is to cover everything typically needed on mainstream Linux distributions
running on commodity PC or server hardware.

The described approach is great for testing purposes, for example to try a
proposed fix or to check if a problem was already fixed in the latest codebase.
Nonetheless, kernels built this way are also totally fine for day-to-day use
while at the same time being easy to keep up to date.

The following steps describe the important aspects of the process; a
comprehensive reference section later explains each of them in more detail. It
sometimes also describes alternative approaches, pitfalls, as well as errors
that might occur at a particular point -- and how to then get things rolling
again.

..
   Note: if you see this note, you are reading the text's source file. You
   might want to switch to a rendered version, as it makes it a lot easier to
   quickly look something up in the reference section and afterwards jump back
   to where you left off. Find a the latest rendered version here:
   https://docs.kernel.org/admin-guide/quickly-build-trimmed-linux.html

.. _backup_sbs:

 * Create a fresh backup and put system repair and restore tools at hand, just
   to be prepared for the unlikely case of something going sideways.

   [:ref:`details<backup>`]

.. _secureboot_sbs:

 * On platforms with 'Secure Boot' or similar techniques, prepare everything to
   ensure the system will permit your self-compiled kernel to boot later. The
   quickest and easiest way to achieve this on commodity x86 systems is to
   disable such techniques in the BIOS setup utility; alternatively, remove
   their restrictions through a process initiated by
   ``mokutil --disable-validation``.

   [:ref:`details<secureboot>`]

.. _buildrequires_sbs:

 * Install all software required to build a Linux kernel. Often you will need:
   'bc', 'binutils' ('ld' et al.), 'bison', 'flex', 'gcc', 'git', 'openssl',
   'pahole', 'perl', and the development headers for 'libelf' and 'openssl'. The
   reference section shows how to quickly install those on various popular Linux
   distributions.

   [:ref:`details<buildrequires>`]

.. _diskspace_sbs:

 * Ensure to have enough free space for building and installing Linux. For the
   latter 150 Megabyte in /lib/ and 100 in /boot/ are a safe bet. For storing
   sources and build artifacts 12 Gigabyte in your home directory should
   typically suffice. If you have less available, be sure to check the reference
   section for the step that explains adjusting your kernels build
   configuration: it mentions a trick that reduce the amount of required space
   in /home/ to around 4 Gigabyte.

   [:ref:`details<diskspace>`]

.. _sources_sbs:

 * Retrieve the sources of the Linux version you intend to build; then change
   into the directory holding them, as all further commands in this guide are
   meant to be executed from there.

   *[Note: the following paragraphs describe how to retrieve the sources by
   partially cloning the Linux stable git repository. This is called a shallow
   clone. The reference section explains two alternatives:* :ref:`packaged
   archives<sources_archive>` *and* :ref:`a full git clone<sources_full>` *;
   prefer the latter, if downloading a lot of data does not bother you, as that
   will avoid some* :ref:`peculiar characteristics of shallow clones the
   reference section explains<sources_shallow>` *.]*

   First, execute the following command to retrieve a fresh mainline codebase::

     git clone --no-checkout --depth 1 -b master \
       https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git ~/linux/
     cd ~/linux/

   If you want to access recent mainline releases and pre-releases, deepen you
   clone's history to the oldest mainline version you are interested in::

     git fetch --shallow-exclude=v6.0 origin

   In case you want to access a stable/longterm release (say v6.1.5), simply add
   the branch holding that series; afterwards fetch the history at least up to
   the mainline version that started the series (v6.1)::

     git remote set-branches --add origin linux-6.1.y
     git fetch --shallow-exclude=v6.0 origin

   Now checkout the code you are interested in. If you just performed the
   initial clone, you will be able to check out a fresh mainline codebase, which
   is ideal for checking whether developers already fixed an issue::

      git checkout --detach origin/master

   If you deepened your clone, you instead of ``origin/master`` can specify the
   version you deepened to (``v6.0`` above); later releases like ``v6.1`` and
   pre-release like ``v6.2-rc1`` will work, too. Stable or longterm versions
   like ``v6.1.5`` work just the same, if you added the appropriate
   stable/longterm branch as described.

   [:ref:`details<sources>`]

.. _patching_sbs:

 * In case you want to apply a kernel patch, do so now. Often a command like
   this will do the trick::

     patch -p1 < ../proposed-fix.patch

   If the ``-p1`` is actually needed, depends on how the patch was created; in
   case it does not apply thus try without it.

   If you cloned the sources with git and anything goes sideways, run ``git
   reset --hard`` to undo any changes to the sources.

   [:ref:`details<patching>`]

.. _tagging_sbs:

 * If you patched your kernel or have one of the same version installed already,
   better add a unique tag to the one you are about to build::

     echo "-proposed_fix" > localversion

   Running ``uname -r`` under your kernel later will then print something like
   '6.1-rc4-proposed_fix'.

   [:ref:`details<tagging>`]

 .. _configuration_sbs:

 * Create the build configuration for your kernel based on an existing
   configuration.

   If you already prepared such a '.config' file yourself, copy it to
   ~/linux/ and run ``make olddefconfig``.

   Use the same command, if your distribution or somebody else already tailored
   your running kernel to your or your hardware's needs: the make target
   'olddefconfig' will then try to use that kernel's .config as base.

   Using this make target is fine for everybody else, too -- but you often can
   save a lot of time by using this command instead::

     yes "" | make localmodconfig

   This will try to pick your distribution's kernel as base, but then disable
   modules for any features apparently superfluous for your setup. This will
   reduce the compile time enormously, especially if you are running an
   universal kernel from a commodity Linux distribution.

   There is a catch: 'localmodconfig' is likely to disable kernel features you
   did not use since you booted your Linux -- like drivers for currently
   disconnected peripherals or a virtualization software not haven't used yet.
   You can reduce or nearly eliminate that risk with tricks the reference
   section outlines; but when building a kernel just for quick testing purposes
   it is often negligible if such features are missing. But you should keep that
   aspect in mind when using a kernel built with this make target, as it might
   be the reason why something you only use occasionally stopped working.

   [:ref:`details<configuration>`]

.. _configmods_sbs:

 * Check if you might want to or have to adjust some kernel configuration
   options:

  * Evaluate how you want to handle debug symbols. Enable them, if you later
    might need to decode a stack trace found for example in a 'panic', 'Oops',
    'warning', or 'BUG'; on the other hand disable them, if you are short on
    storage space or prefer a smaller kernel binary. See the reference section
    for details on how to do either. If neither applies, it will likely be fine
    to simply not bother with this. [:ref:`details<configmods_debugsymbols>`]

  * Are you running Debian? Then to avoid known problems by performing
    additional adjustments explained in the reference section.
    [:ref:`details<configmods_distros>`].

  * If you want to influence the other aspects of the configuration, do so now
    by using make targets like 'menuconfig' or 'xconfig'.
    [:ref:`details<configmods_individual>`].

.. _build_sbs:

 * Build the image and the modules of your kernel::

     make -j $(nproc --all)

   If you want your kernel packaged up as deb, rpm, or tar file, see the
   reference section for alternatives.

   [:ref:`details<build>`]

.. _install_sbs:

 * Now install your kernel::

     command -v installkernel && sudo make modules_install install

   Often all left for you to do afterwards is a ``reboot``, as many commodity
   Linux distributions will then create an initramfs (also known as initrd) and
   an entry for your kernel in your bootloader's configuration; but on some
   distributions you have to take care of these two steps manually for reasons
   the reference section explains.

   On a few distributions like Arch Linux and its derivatives the above command
   does nothing at all; in that case you have to manually install your kernel,
   as outlined in the reference section.

   If you are running a immutable Linux distribution, check its documentation
   and the web to find out how to install your own kernel there.

   [:ref:`details<install>`]

.. _another_sbs:

 * To later build another kernel you need similar steps, but sometimes slightly
   different commands.

   First, switch back into the sources tree::

      cd ~/linux/

   In case you want to build a version from a stable/longterm series you have
   not used yet (say 6.2.y), tell git to track it::

      git remote set-branches --add origin linux-6.2.y

   Now fetch the latest upstream changes; you again need to specify the earliest
   version you care about, as git otherwise might retrieve the entire commit
   history::

     git fetch --shallow-exclude=v6.0 origin

   Now switch to the version you are interested in -- but be aware the command
   used here will discard any modifications you performed, as they would
   conflict with the sources you want to checkout::

     git checkout --force --detach origin/master

   At this point you might want to patch the sources again or set/modify a build
   tag, as explained earlier. Afterwards adjust the build configuration to the
   new codebase using olddefconfig, which will now adjust the configuration file
   you prepared earlier using localmodconfig  (~/linux/.config) for your next
   kernel::

     # reminder: if you want to apply patches, do it at this point
     # reminder: you might want to update your build tag at this point
     make olddefconfig

   Now build your kernel::

     make -j $(nproc --all)

   Afterwards install the kernel as outlined above::

     command -v installkernel && sudo make modules_install install

   [:ref:`details<another>`]

.. _uninstall_sbs:

 * Your kernel is easy to remove later, as its parts are only stored in two
   places and clearly identifiable by the kernel's release name. Just ensure to
   not delete the kernel you are running, as that might render your system
   unbootable.

   Start by deleting the directory holding your kernel's modules, which is named
   after its release name -- '6.0.1-foobar' in the following example::

     sudo rm -rf /lib/modules/6.0.1-foobar

   Now try the following command, which on some distributions will delete all
   other kernel files installed while also removing the kernel's entry from the
   bootloader configuration::

     command -v kernel-install && sudo kernel-install -v remove 6.0.1-foobar

   If that command does not output anything or fails, see the reference section;
   do the same if any files named '*6.0.1-foobar*' remain in /boot/.

   [:ref:`details<uninstall>`]

.. _submit_improvements:

Did you run into trouble following any of the above steps that is not cleared up
by the reference section below? Or do you have ideas how to improve the text?
Then please take a moment of your time and let the maintainer of this document
know by email (Thorsten Leemhuis <linux@leemhuis.info>), ideally while CCing the
Linux docs mailing list (linux-doc@vger.kernel.org). Such feedback is vital to
improve this document further, which is in everybody's interest, as it will
enable more people to master the task described here.

Reference section for the step-by-step guide
============================================

This section holds additional information for each of the steps in the above
guide.

.. _backup:

Prepare for emergencies
-----------------------

   *Create a fresh backup and put system repair and restore tools at hand*
   [:ref:`... <backup_sbs>`]

Remember, you are dealing with computers, which sometimes do unexpected things
-- especially if you fiddle with crucial parts like the kernel of an operating
system. That's what you are about to do in this process. Hence, better prepare
for something going sideways, even if that should not happen.

[:ref:`back to step-by-step guide <backup_sbs>`]

.. _secureboot:

Dealing with techniques like Secure Boot
----------------------------------------

   *On platforms with 'Secure Boot' or similar techniques, prepare everything to
   ensure the system will permit your self-compiled kernel to boot later.*
   [:ref:`... <secureboot_sbs>`]

Many modern systems allow only certain operating systems to start; they thus by
default will reject booting self-compiled kernels.

You ideally deal with this by making your platform trust your self-built kernels
with the help of a certificate and signing. How to do that is not described
here, as it requires various steps that would take the text too far away from
its purpose; 'Documentation/admin-guide/module-signing.rst' and various web
sides already explain this in more detail.

Temporarily disabling solutions like Secure Boot is another way to make your own
Linux boot. On commodity x86 systems it is possible to do this in the BIOS Setup
utility; the steps to do so are not described here, as they greatly vary between
machines.

On mainstream x86 Linux distributions there is a third and universal option:
disable all Secure Boot restrictions for your Linux environment. You can
initiate this process by running ``mokutil --disable-validation``; this will
tell you to create a one-time password, which is safe to write down. Now
restart; right after your BIOS performed all self-tests the bootloader Shim will
show a blue box with a message 'Press any key to perform MOK management'. Hit
some key before the countdown exposes. This will open a menu and choose 'Change
Secure Boot state' there. Shim's 'MokManager' will now ask you to enter three
randomly chosen characters from the one-time password specified earlier. Once
you provided them, confirm that you really want to disable the validation.
Afterwards, permit MokManager to reboot the machine.

[:ref:`back to step-by-step guide <secureboot_sbs>`]

.. _buildrequires:

Install build requirements
--------------------------

   *Install all software required to build a Linux kernel.*
   [:ref:`...<buildrequires_sbs>`]

The kernel is pretty stand-alone, but besides tools like the compiler you will
sometimes need a few libraries to build one. How to install everything needed
depends on your Linux distribution and the configuration of the kernel you are
about to build.

Here are a few examples what you typically need on some mainstream
distributions:

 * Debian, Ubuntu, and derivatives::

     sudo apt install bc binutils bison dwarves flex gcc git make openssl \
       pahole perl-base libssl-dev libelf-dev

 * Fedora and derivatives::

     sudo dnf install binutils /usr/include/{libelf.h,openssl/pkcs7.h} \
       /usr/bin/{bc,bison,flex,gcc,git,openssl,make,perl,pahole}

 * openSUSE and derivatives::

     sudo zypper install bc binutils bison dwarves flex gcc git make perl-base \
       openssl openssl-devel libelf-dev

In case you wonder why these lists include openssl and its development headers:
they are needed for the Secure Boot support, which many distributions enable in
their kernel configuration for x86 machines.

Sometimes you will need tools for compression formats like bzip2, gzip, lz4,
lzma, lzo, xz, or zstd as well.

You might need additional libraries and their development headers in case you
perform tasks not covered in this guide. For example, zlib will be needed when
building kernel tools from the tools/ directory; adjusting the build
configuration with make targets like 'menuconfig' or 'xconfig' will require
development headers for ncurses or Qt5.

[:ref:`back to step-by-step guide <buildrequires_sbs>`]

.. _diskspace:

Space requirements
------------------

   *Ensure to have enough free space for building and installing Linux.*
   [:ref:`... <diskspace_sbs>`]

The numbers mentioned are rough estimates with a big extra charge to be on the
safe side, so often you will need less.

If you have space constraints, remember to read the reference section when you
reach the :ref:`section about configuration adjustments' <configmods>`, as
ensuring debug symbols are disabled will reduce the consumed disk space by quite
a few gigabytes.

[:ref:`back to step-by-step guide <diskspace_sbs>`]


.. _sources:

Download the sources
--------------------

  *Retrieve the sources of the Linux version you intend to build.*
  [:ref:`...<sources_sbs>`]

The step-by-step guide outlines how to retrieve Linux' sources using a shallow
git clone. There is :ref:`more to tell about this method<sources_shallow>` and
two alternate ways worth describing: :ref:`packaged archives<sources_archive>`
and :ref:`a full git clone<sources_full>`. And the aspects ':ref:`wouldn't it
be wiser to use a proper pre-release than the latest mainline code
<sources_snapshot>`' and ':ref:`how to get an even fresher mainline codebase
<sources_fresher>`' need elaboration, too.

Note, to keep things simple the commands used in this guide store the build
artifacts in the source tree. If you prefer to separate them, simply add
something like ``O=~/linux-builddir/`` to all make calls; also adjust the path
in all commands that add files or modify any generated (like your '.config').

[:ref:`back to step-by-step guide <sources_sbs>`]

.. _sources_shallow:

Noteworthy characteristics of shallow clones
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The step-by-step guide uses a shallow clone, as it is the best solution for most
of this document's target audience. There are a few aspects of this approach
worth mentioning:

 * This document in most places uses ``git fetch`` with ``--shallow-exclude=``
   to specify the earliest version you care about (or to be precise: its git
   tag). You alternatively can use the parameter ``--shallow-since=`` to specify
   an absolute (say ``'2023-07-15'``) or relative (``'12 months'``) date to
   define the depth of the history you want to download. As a second
   alternative, you can also specify a certain depth explicitly with a parameter
   like ``--depth=1``, unless you add branches for stable/longterm kernels.

 * When running ``git fetch``, remember to always specify the oldest version,
   the time you care about, or an explicit depth as shown in the step-by-step
   guide. Otherwise you will risk downloading nearly the entire git history,
   which will consume quite a bit of time and bandwidth while also stressing the
   servers.

   Note, you do not have to use the same version or date all the time. But when
   you change it over time, git will deepen or flatten the history to the
   specified point. That allows you to retrieve versions you initially thought
   you did not need -- or it will discard the sources of older versions, for
   example in case you want to free up some disk space. The latter will happen
   automatically when using ``--shallow-since=`` or
   ``--depth=``.

 * Be warned, when deepening your clone you might encounter an error like
   'fatal: error in object: unshallow cafecaca0c0dacafecaca0c0dacafecaca0c0da'.
   In that case run ``git repack -d`` and try again``

 * In case you want to revert changes from a certain version (say Linux 6.3) or
   perform a bisection (v6.2..v6.3), better tell ``git fetch`` to retrieve
   objects up to three versions earlier (e.g. 6.0): ``git describe`` will then
   be able to describe most commits just like it would in a full git clone.

[:ref:`back to step-by-step guide <sources_sbs>`] [:ref:`back to section intro <sources>`]

.. _sources_archive:

Downloading the sources using a packages archive
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

People new to compiling Linux often assume downloading an archive via the
front-page of https://kernel.org is the best approach to retrieve Linux'
sources. It actually can be, if you are certain to build just one particular
kernel version without changing any code. Thing is: you might be sure this will
be the case, but in practice it often will turn out to be a wrong assumption.

That's because when reporting or debugging an issue developers will often ask to
give another version a try. They also might suggest temporarily undoing a commit
with ``git revert`` or might provide various patches to try. Sometimes reporters
will also be asked to use ``git bisect`` to find the change causing a problem.
These things rely on git or are a lot easier and quicker to handle with it.

A shallow clone also does not add any significant overhead. For example, when
you use ``git clone --depth=1`` to create a shallow clone of the latest mainline
codebase git will only retrieve a little more data than downloading the latest
mainline pre-release (aka 'rc') via the front-page of kernel.org would.

A shallow clone therefore is often the better choice. If you nevertheless want
to use a packaged source archive, download one via kernel.org; afterwards
extract its content to some directory and change to the subdirectory created
during extraction. The rest of the step-by-step guide will work just fine, apart
from things that rely on git -- but this mainly concerns the section on
successive builds of other versions.

[:ref:`back to step-by-step guide <sources_sbs>`] [:ref:`back to section intro <sources>`]

.. _sources_full:

Downloading the sources using a full git clone
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If downloading and storing a lot of data (~4,4 Gigabyte as of early 2023) is
nothing that bothers you, instead of a shallow clone perform a full git clone
instead. You then will avoid the specialties mentioned above and will have all
versions and individual commits at hand at any time::

    curl -L \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git/clone.bundle \
      -o linux-stable.git.bundle
    git clone linux-stable.git.bundle ~/linux/
    rm linux-stable.git.bundle
    cd ~/linux/
    git remote set-url origin \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
    git fetch origin
    git checkout --detach origin/master

[:ref:`back to step-by-step guide <sources_sbs>`] [:ref:`back to section intro <sources>`]

.. _sources_snapshot:

Proper pre-releases (RCs) vs. latest mainline
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

When cloning the sources using git and checking out origin/master, you often
will retrieve a codebase that is somewhere between the latest and the next
release or pre-release. This almost always is the code you want when giving
mainline a shot: pre-releases like v6.1-rc5 are in no way special, as they do
not get any significant extra testing before being published.

There is one exception: you might want to stick to the latest mainline release
(say v6.1) before its successor's first pre-release (v6.2-rc1) is out. That is
because compiler errors and other problems are more likely to occur during this
time, as mainline then is in its 'merge window': a usually two week long phase,
in which the bulk of the changes for the next release is merged.

[:ref:`back to step-by-step guide <sources_sbs>`] [:ref:`back to section intro <sources>`]

.. _sources_fresher:

Avoiding the mainline lag
~~~~~~~~~~~~~~~~~~~~~~~~~

The explanations for both the shallow clone and the full clone both retrieve the
code from the Linux stable git repository. That makes things simpler for this
document's audience, as it allows easy access to both mainline and
stable/longterm releases. This approach has just one downside:

Changes merged into the mainline repository are only synced to the master branch
of the Linux stable repository  every few hours. This lag most of the time is
not something to worry about; but in case you really need the latest code, just
add the mainline repo as additional remote and checkout the code from there::

    git remote add mainline \
      https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
    git fetch mainline
    git checkout --detach mainline/master

When doing this with a shallow clone, remember to call ``git fetch`` with one
of the parameters described earlier to limit the depth.

[:ref:`back to step-by-step guide <sources_sbs>`] [:ref:`back to section intro <sources>`]

.. _patching:

Patch the sources (optional)
----------------------------

  *In case you want to apply a kernel patch, do so now.*
  [:ref:`...<patching_sbs>`]

This is the point where you might want to patch your kernel -- for example when
a developer proposed a fix and asked you to check if it helps. The step-by-step
guide already explains everything crucial here.

[:ref:`back to step-by-step guide <patching_sbs>`]

.. _tagging:

Tagging this kernel build (optional, often wise)
------------------------------------------------

  *If you patched your kernel or already have that kernel version installed,
  better tag your kernel by extending its release name:*
  [:ref:`...<tagging_sbs>`]

Tagging your kernel will help avoid confusion later, especially when you patched
your kernel. Adding an individual tag will also ensure the kernel's image and
its modules are installed in parallel to any existing kernels.

There are various ways to add such a tag. The step-by-step guide realizes one by
creating a 'localversion' file in your build directory from which the kernel
build scripts will automatically pick up the tag. You can later change that file
to use a different tag in subsequent builds or simply remove that file to dump
the tag.

[:ref:`back to step-by-step guide <tagging_sbs>`]

.. _configuration:

Define the build configuration for your kernel
----------------------------------------------

  *Create the build configuration for your kernel based on an existing
  configuration.* [:ref:`... <configuration_sbs>`]

There are various aspects for this steps that require a more careful
explanation:

Pitfalls when using another configuration file as base
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Make targets like localmodconfig and olddefconfig share a few common snares you
want to be aware of:

 * These targets will reuse a kernel build configuration in your build directory
   (e.g. '~/linux/.config'), if one exists. In case you want to start from
   scratch you thus need to delete it.

 * The make targets try to find the configuration for your running kernel
   automatically, but might choose poorly. A line like '# using defaults found
   in /boot/config-6.0.7-250.fc36.x86_64' or 'using config:
   '/boot/config-6.0.7-250.fc36.x86_64' tells you which file they picked. If
   that is not the intended one, simply store it as '~/linux/.config'
   before using these make targets.

 * Unexpected things might happen if you try to use a config file prepared for
   one kernel (say v6.0) on an older generation (say v5.15). In that case you
   might want to use a configuration as base which your distribution utilized
   when they used that or an slightly older kernel version.

Influencing the configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The make target olddefconfig and the ``yes "" |`` used when utilizing
localmodconfig will set any undefined build options to their default value. This
among others will disable many kernel features that were introduced after your
base kernel was released.

If you want to set these configurations options manually, use ``oldconfig``
instead of ``olddefconfig`` or omit the ``yes "" |`` when utilizing
localmodconfig. Then for each undefined configuration option you will be asked
how to proceed. In case you are unsure what to answer, simply hit 'enter' to
apply the default value.

Big pitfall when using localmodconfig
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

As explained briefly in the step-by-step guide already: with localmodconfig it
can easily happen that your self-built kernel will lack modules for tasks you
did not perform before utilizing this make target. That's because those tasks
require kernel modules that are normally autoloaded when you perform that task
for the first time; if you didn't perform that task at least once before using
localmodonfig, the latter will thus assume these modules are superfluous and
disable them.

You can try to avoid this by performing typical tasks that often will autoload
additional kernel modules: start a VM, establish VPN connections, loop-mount a
CD/DVD ISO, mount network shares (CIFS, NFS, ...), and connect all external
devices (2FA keys, headsets, webcams, ...) as well as storage devices with file
systems you otherwise do not utilize (btrfs, ext4, FAT, NTFS, XFS, ...). But it
is hard to think of everything that might be needed -- even kernel developers
often forget one thing or another at this point.

Do not let that risk bother you, especially when compiling a kernel only for
testing purposes: everything typically crucial will be there. And if you forget
something important you can turn on a missing feature later and quickly run the
commands to compile and install a better kernel.

But if you plan to build and use self-built kernels regularly, you might want to
reduce the risk by recording which modules your system loads over the course of
a few weeks. You can automate this with `modprobed-db
<https://github.com/graysky2/modprobed-db>`_. Afterwards use ``LSMOD=<path>`` to
point localmodconfig to the list of modules modprobed-db noticed being used::

    yes "" | make LSMOD="${HOME}"/.config/modprobed.db localmodconfig

Remote building with localmodconfig
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

If you want to use localmodconfig to build a kernel for another machine, run
``lsmod > lsmod_foo-machine`` on it and transfer that file to your build host.
Now point the build scripts to the file like this: ``yes "" | make
LSMOD=~/lsmod_foo-machine localmodconfig``. Note, in this case
you likely want to copy a base kernel configuration from the other machine over
as well and place it as .config in your build directory.

[:ref:`back to step-by-step guide <configuration_sbs>`]

.. _configmods:

Adjust build configuration
--------------------------

   *Check if you might want to or have to adjust some kernel configuration
   options:*

Depending on your needs you at this point might want or have to adjust some
kernel configuration options.

.. _configmods_debugsymbols:

Debug symbols
~~~~~~~~~~~~~

   *Evaluate how you want to handle debug symbols.*
   [:ref:`...<configmods_sbs>`]

Most users do not need to care about this, it's often fine to leave everything
as it is; but you should take a closer look at this, if you might need to decode
a stack trace or want to reduce space consumption.

Having debug symbols available can be important when your kernel throws a
'panic', 'Oops', 'warning', or 'BUG' later when running, as then you will be
able to find the exact place where the problem occurred in the code. But
collecting and embedding the needed debug information takes time and consumes
quite a bit of space: in late 2022 the build artifacts for a typical x86 kernel
configured with localmodconfig consumed around 5 Gigabyte of space with debug
symbols, but less than 1 when they were disabled. The resulting kernel image and
the modules are bigger as well, which increases load times.

Hence, if you want a small kernel and are unlikely to decode a stack trace
later, you might want to disable debug symbols to avoid above downsides::

    ./scripts/config --file .config -d DEBUG_INFO \
      -d DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT -d DEBUG_INFO_DWARF4 \
      -d DEBUG_INFO_DWARF5 -e CONFIG_DEBUG_INFO_NONE
    make olddefconfig

You on the other hand definitely want to enable them, if there is a decent
chance that you need to decode a stack trace later (as explained by 'Decode
failure messages' in Documentation/admin-guide/tainted-kernels.rst in more
detail)::

    ./scripts/config --file .config -d DEBUG_INFO_NONE -e DEBUG_KERNEL
      -e DEBUG_INFO -e DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT -e KALLSYMS -e KALLSYMS_ALL
    make olddefconfig

Note, many mainstream distributions enable debug symbols in their kernel
configurations -- make targets like localmodconfig and olddefconfig thus will
often pick that setting up.

[:ref:`back to step-by-step guide <configmods_sbs>`]

.. _configmods_distros:

Distro specific adjustments
~~~~~~~~~~~~~~~~~~~~~~~~~~~

   *Are you running* [:ref:`... <configmods_sbs>`]

The following sections help you to avoid build problems that are known to occur
when following this guide on a few commodity distributions.

**Debian:**

 * Remove a stale reference to a certificate file that would cause your build to
   fail::

    ./scripts/config --file .config --set-str SYSTEM_TRUSTED_KEYS ''

   Alternatively, download the needed certificate and make that configuration
   option point to it, as `the Debian handbook explains in more detail
   <https://debian-handbook.info/browse/stable/sect.kernel-compilation.html>`_
   -- or generate your own, as explained in
   Documentation/admin-guide/module-signing.rst.

[:ref:`back to step-by-step guide <configmods_sbs>`]

.. _configmods_individual:

Individual adjustments
~~~~~~~~~~~~~~~~~~~~~~

   *If you want to influence the other aspects of the configuration, do so
   now* [:ref:`... <configmods_sbs>`]

You at this point can use a command like ``make menuconfig`` to enable or
disable certain features using a text-based user interface; to use a graphical
configuration utilize, use the make target ``xconfig`` or ``gconfig`` instead.
All of them require development libraries from toolkits they are based on
(ncurses, Qt5, Gtk2); an error message will tell you if something required is
missing.

[:ref:`back to step-by-step guide <configmods_sbs>`]

.. _build:

Build your kernel
-----------------

  *Build the image and the modules of your kernel* [:ref:`... <build_sbs>`]

A lot can go wrong at this stage, but the instructions below will help you help
yourself. Another subsection explains how to directly package your kernel up as
deb, rpm or tar file.

Dealing with build errors
~~~~~~~~~~~~~~~~~~~~~~~~~

When a build error occurs, it might be caused by some aspect of your machine's
setup that often can be fixed quickly; other times though the problem lies in
the code and can only be fixed by a developer. A close examination of the
failure messages coupled with some research on the internet will often tell you
which of the two it is. To perform such a investigation, restart the build
process like this::

    make V=1

The ``V=1`` activates verbose output, which might be needed to see the actual
error. To make it easier to spot, this command also omits the ``-j $(nproc
--all)`` used earlier to utilize every CPU core in the system for the job -- but
this parallelism also results in some clutter when failures occur.

After a few seconds the build process should run into the error again. Now try
to find the most crucial line describing the problem. Then search the internet
for the most important and non-generic section of that line (say 4 to 8 words);
avoid or remove anything that looks remotely system-specific, like your username
or local path names like ``/home/username/linux/``. First try your regular
internet search engine with that string, afterwards search Linux kernel mailing
lists via `lore.kernel.org/all/ <https://lore.kernel.org/all/>`_.

This most of the time will find something that will explain what is wrong; quite
often one of the hits will provide a solution for your problem, too. If you
do not find anything that matches your problem, try again from a different angle
by modifying your search terms or using another line from the error messages.

In the end, most trouble you are to run into has likely been encountered and
reported by others already. That includes issues where the cause is not your
system, but lies the code. If you run into one of those, you might thus find a
solution (e.g. a patch) or workaround for your problem, too.

Package your kernel up
~~~~~~~~~~~~~~~~~~~~~~

The step-by-step guide uses the default make targets (e.g. 'bzImage' and
'modules' on x86) to build the image and the modules of your kernel, which later
steps of the guide then install. You instead can also directly build everything
and directly package it up by using one of the following targets:

 * ``make -j $(nproc --all) bindeb-pkg`` to generate a deb package

 * ``make -j $(nproc --all) binrpm-pkg`` to generate a rpm package

 * ``make -j $(nproc --all) tarbz2-pkg`` to generate a bz2 compressed tarball

This is just a selection of available make targets for this purpose, see
``make help`` for others. You can also use these targets after running
``make -j $(nproc --all)``, as they will pick up everything already built.

If you employ the targets to generate deb or rpm packages, ignore the
step-by-step guide's instructions on installing and removing your kernel;
instead install and remove the packages using the package utility for the format
(e.g. dpkg and rpm) or a package management utility build on top of them (apt,
aptitude, dnf/yum, zypper, ...). Be aware that the packages generated using
these two make targets are designed to work on various distributions utilizing
those formats, they thus will sometimes behave differently than your
distribution's kernel packages.

[:ref:`back to step-by-step guide <build_sbs>`]

.. _install:

Install your kernel
-------------------

  *Now install your kernel* [:ref:`... <install_sbs>`]

What you need to do after executing the command in the step-by-step guide
depends on the existence and the implementation of an ``installkernel``
executable. Many commodity Linux distributions ship such a kernel installer in
``/sbin/`` that does everything needed, hence there is nothing left for you
except rebooting. But some distributions contain an installkernel that does
only part of the job -- and a few lack it completely and leave all the work to
you.

If ``installkernel`` is found, the kernel's build system will delegate the
actual installation of your kernel's image and related files to this executable.
On almost all Linux distributions it will store the image as '/boot/vmlinuz-
<your kernel's release name>' and put a 'System.map-<your kernel's release
name>' alongside it. Your kernel will thus be installed in parallel to any
existing ones, unless you already have one with exactly the same release name.

Installkernel on many distributions will afterwards generate an 'initramfs'
(often also called 'initrd'), which commodity distributions rely on for booting;
hence be sure to keep the order of the two make targets used in the step-by-step
guide, as things will go sideways if you install your kernel's image before its
modules. Often installkernel will then add your kernel to the bootloader
configuration, too. You have to take care of one or both of these tasks
yourself, if your distributions installkernel doesn't handle them.

A few distributions like Arch Linux and its derivatives totally lack an
installkernel executable. On those just install the modules using the kernel's
build system and then install the image and the System.map file manually::

     sudo make modules_install
     sudo install -m 0600 $(make -s image_name) /boot/vmlinuz-$(make -s kernelrelease)
     sudo install -m 0600 System.map /boot/System.map-$(make -s kernelrelease)

If your distribution boots with the help of an initramfs, now generate one for
your kernel using the tools your distribution provides for this process.
Afterwards add your kernel to your bootloader configuration and reboot.

[:ref:`back to step-by-step guide <install_sbs>`]

.. _another:

Another round later
-------------------

  *To later build another kernel you need similar, but sometimes slightly
  different commands* [:ref:`... <another_sbs>`]

The process to build later kernels is similar, but at some points slightly
different. You for example do not want to use 'localmodconfig' for succeeding
kernel builds, as you already created a trimmed down configuration you want to
use from now on. Hence instead just use ``oldconfig`` or ``olddefconfig`` to
adjust your build configurations to the needs of the kernel version you are
about to build.

If you created a shallow-clone with git, remember what the :ref:`section that
explained the setup described in more detail <sources>`: you need to use a
slightly different ``git fetch`` command and when switching to another series
need to add an additional remote branch.

[:ref:`back to step-by-step guide <another_sbs>`]

.. _uninstall:

Uninstall the kernel later
--------------------------

  *All parts of your installed kernel are identifiable by its release name and
  thus easy to remove later.* [:ref:`... <uninstall_sbs>`]

Do not worry installing your kernel manually and thus bypassing your
distribution's packaging system will totally mess up your machine: all parts of
your kernel are easy to remove later, as files are stored in two places only and
normally identifiable by the kernel's release name.

One of the two places is a directory in /lib/modules/, which holds the modules
for each installed kernel. This directory is named after the kernel's release
name; hence, to remove all modules for one of your kernels, simply remove its
modules directory in /lib/modules/.

The other place is /boot/, where typically one to five files will be placed
during installation of a kernel. All of them usually contain the release name in
their file name, but how many files and their name depends somewhat on your
distribution's installkernel executable (:ref:`see above <install>`) and its
initramfs generator. On some distributions the ``kernel-install`` command
mentioned in the step-by-step guide will remove all of these files for you --
and the entry for your kernel in the bootloader configuration at the same time,
too. On others you have to take care of these steps yourself. The following
command should interactively remove the two main files of a kernel with the
release name '6.0.1-foobar'::

    rm -i /boot/{System.map,vmlinuz}-6.0.1-foobar

Now remove the belonging initramfs, which often will be called something like
``/boot/initramfs-6.0.1-foobar.img`` or ``/boot/initrd.img-6.0.1-foobar``.
Afterwards check for other files in /boot/ that have '6.0.1-foobar' in their
name and delete them as well. Now remove the kernel from your bootloader's
configuration.

Note, be very careful with wildcards like '*' when deleting files or directories
for kernels manually: you might accidentally remove files of a 6.0.11 kernel
when all you want is to remove 6.0 or 6.0.1.

[:ref:`back to step-by-step guide <uninstall_sbs>`]

.. _faq:

FAQ
===

Why does this 'how-to' not work on my system?
---------------------------------------------

As initially stated, this guide is 'designed to cover everything typically
needed [to build a kernel] on mainstream Linux distributions running on
commodity PC or server hardware'. The outlined approach despite this should work
on many other setups as well. But trying to cover every possible use-case in one
guide would defeat its purpose, as without such a focus you would need dozens or
hundreds of constructs along the lines of 'in case you are having <insert
machine or distro>, you at this point have to do <this and that>
<instead|additionally>'. Each of which would make the text longer, more
complicated, and harder to follow.

That being said: this of course is a balancing act. Hence, if you think an
additional use-case is worth describing, suggest it to the maintainers of this
document, as :ref:`described above <submit_improvements>`.


..
   end-of-content
..
   This document is maintained by Thorsten Leemhuis <linux@leemhuis.info>. If
   you spot a typo or small mistake, feel free to let him know directly and
   he'll fix it. You are free to do the same in a mostly informal way if you
   want to contribute changes to the text -- but for copyright reasons please CC
   linux-doc@vger.kernel.org and 'sign-off' your contribution as
   Documentation/process/submitting-patches.rst explains in the section 'Sign
   your work - the Developer's Certificate of Origin'.
..
   This text is available under GPL-2.0+ or CC-BY-4.0, as stated at the top
   of the file. If you want to distribute this text under CC-BY-4.0 only,
   please use 'The Linux kernel development community' for author attribution
   and link this as source:
   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/Documentation/admin-guide/quickly-build-trimmed-linux.rst
..
   Note: Only the content of this RST file as found in the Linux kernel sources
   is available under CC-BY-4.0, as versions of this text that were processed
   (for example by the kernel's build system) might contain content taken from
   files which use a more restrictive license.

