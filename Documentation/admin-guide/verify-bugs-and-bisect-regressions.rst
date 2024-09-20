.. SPDX-License-Identifier: (GPL-2.0+ OR CC-BY-4.0)
.. [see the bottom of this file for redistribution information]

=========================================
How to verify bugs and bisect regressions
=========================================

This document describes how to check if some Linux kernel problem occurs in code
currently supported by developers -- to then explain how to locate the change
causing the issue, if it is a regression (e.g. did not happen with earlier
versions).

The text aims at people running kernels from mainstream Linux distributions on
commodity hardware who want to report a kernel bug to the upstream Linux
developers. Despite this intent, the instructions work just as well for users
who are already familiar with building their own kernels: they help avoid
mistakes occasionally made even by experienced developers.

..
   Note: if you see this note, you are reading the text's source file. You
   might want to switch to a rendered version: it makes it a lot easier to
   read and navigate this document -- especially when you want to look something
   up in the reference section, then jump back to where you left off.
..
   Find the latest rendered version of this text here:
   https://docs.kernel.org/admin-guide/verify-bugs-and-bisect-regressions.html

The essence of the process (aka 'TL;DR')
========================================

*[If you are new to building or bisecting Linux, ignore this section and head
over to the* ':ref:`step-by-step guide <introguide_bissbs>`' *below. It utilizes
the same commands as this section while describing them in brief fashion. The
steps are nevertheless easy to follow and together with accompanying entries
in a reference section mention many alternatives, pitfalls, and additional
aspects, all of which might be essential in your present case.]*

**In case you want to check if a bug is present in code currently supported by
developers**, execute just the *preparations* and *segment 1*; while doing so,
consider the newest Linux kernel you regularly use to be the 'working' kernel.
In the following example that's assumed to be 6.0, which is why its sources
will be used to prepare the .config file.

**In case you face a regression**, follow the steps at least till the end of
*segment 2*. Then you can submit a preliminary report -- or continue with
*segment 3*, which describes how to perform a bisection needed for a
full-fledged regression report. In the following example 6.0.13 is assumed to be
the 'working' kernel and 6.1.5 to be the first 'broken', which is why 6.0
will be considered the 'good' release and used to prepare the .config file.

* **Preparations**: set up everything to build your own kernels::

    # * Remove any software that depends on externally maintained kernel modules
    #   or builds any automatically during bootup.
    # * Ensure Secure Boot permits booting self-compiled Linux kernels.
    # * If you are not already running the 'working' kernel, reboot into it.
    # * Install compilers and everything else needed for building Linux.
    # * Ensure to have 15 Gigabyte free space in your home directory.
    git clone -o mainline --no-checkout \
      https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git ~/linux/
    cd ~/linux/
    git remote add -t master stable \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git
    git switch --detach v6.0
    # * Hint: if you used an existing clone, ensure no stale .config is around.
    make olddefconfig
    # * Ensure the former command picked the .config of the 'working' kernel.
    # * Connect external hardware (USB keys, tokens, ...), start a VM, bring up
    #   VPNs, mount network shares, and briefly try the feature that is broken.
    yes '' | make localmodconfig
    ./scripts/config --set-str CONFIG_LOCALVERSION '-local'
    ./scripts/config -e CONFIG_LOCALVERSION_AUTO
    # * Note, when short on storage space, check the guide for an alternative:
    ./scripts/config -d DEBUG_INFO_NONE -e KALLSYMS_ALL -e DEBUG_KERNEL \
      -e DEBUG_INFO -e DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT -e KALLSYMS
    # * Hint: at this point you might want to adjust the build configuration;
    #   you'll have to, if you are running Debian.
    make olddefconfig
    cp .config ~/kernel-config-working

* **Segment 1**: build a kernel from the latest mainline codebase.

  This among others checks if the problem was fixed already and which developers
  later need to be told about the problem; in case of a regression, this rules
  out a .config change as root of the problem.

  a) Checking out latest mainline code::

       cd ~/linux/
       git switch --discard-changes --detach mainline/master

  b) Build, install, and boot a kernel::

       cp ~/kernel-config-working .config
       make olddefconfig
       make -j $(nproc --all)
       # * Make sure there is enough disk space to hold another kernel:
       df -h /boot/ /lib/modules/
       # * Note: on Arch Linux, its derivatives and a few other distributions
       #   the following commands will do nothing at all or only part of the
       #   job. See the step-by-step guide for further details.
       sudo make modules_install
       command -v installkernel && sudo make install
       # * Check how much space your self-built kernel actually needs, which
       #   enables you to make better estimates later:
       du -ch /boot/*$(make -s kernelrelease)* | tail -n 1
       du -sh /lib/modules/$(make -s kernelrelease)/
       # * Hint: the output of the following command will help you pick the
       #   right kernel from the boot menu:
       make -s kernelrelease | tee -a ~/kernels-built
       reboot
       # * Once booted, ensure you are running the kernel you just built by
       #   checking if the output of the next two commands matches:
       tail -n 1 ~/kernels-built
       uname -r
       cat /proc/sys/kernel/tainted

  c) Check if the problem occurs with this kernel as well.

* **Segment 2**: ensure the 'good' kernel is also a 'working' kernel.

  This among others verifies the trimmed .config file actually works well, as
  bisecting with it otherwise would be a waste of time:

  a) Start by checking out the sources of the 'good' version::

       cd ~/linux/
       git switch --discard-changes --detach v6.0

  b) Build, install, and boot a kernel as described earlier in *segment 1,
     section b* -- just feel free to skip the 'du' commands, as you have a rough
     estimate already.

  c) Ensure the feature that regressed with the 'broken' kernel actually works
     with this one.

* **Segment 3**: perform and validate the bisection.

  a) Retrieve the sources for your 'bad' version::

       git remote set-branches --add stable linux-6.1.y
       git fetch stable

  b) Initialize the bisection::

       cd ~/linux/
       git bisect start
       git bisect good v6.0
       git bisect bad v6.1.5

  c) Build, install, and boot a kernel as described earlier in *segment 1,
     section b*.

     In case building or booting the kernel fails for unrelated reasons, run
     ``git bisect skip``. In all other outcomes, check if the regressed feature
     works with the newly built kernel. If it does, tell Git by executing
     ``git bisect good``; if it does not, run ``git bisect bad`` instead.

     All three commands will make Git check out another commit; then re-execute
     this step (e.g. build, install, boot, and test a kernel to then tell Git
     the outcome). Do so again and again until Git shows which commit broke
     things. If you run short of disk space during this process, check the
     section 'Complementary tasks: cleanup during and after the process'
     below.

  d) Once your finished the bisection, put a few things away::

       cd ~/linux/
       git bisect log > ~/bisect-log
       cp .config ~/bisection-config-culprit
       git bisect reset

  e) Try to verify the bisection result::

       git switch --discard-changes --detach mainline/master
       git revert --no-edit cafec0cacaca0
       cp ~/kernel-config-working .config
       ./scripts/config --set-str CONFIG_LOCALVERSION '-local-cafec0cacaca0-reverted'

    This is optional, as some commits are impossible to revert. But if the
    second command worked flawlessly, build, install, and boot one more kernel
    kernel; just this time skip the first command copying the base .config file
    over, as that already has been taken care off.

* **Complementary tasks**: cleanup during and after the process.

  a) To avoid running out of disk space during a bisection, you might need to
     remove some kernels you built earlier. You most likely want to keep those
     you built during segment 1 and 2 around for a while, but you will most
     likely no longer need kernels tested during the actual bisection
     (Segment 3 c). You can list them in build order using::

       ls -ltr /lib/modules/*-local*

    To then for example erase a kernel that identifies itself as
    '6.0-rc1-local-gcafec0cacaca0', use this::

       sudo rm -rf /lib/modules/6.0-rc1-local-gcafec0cacaca0
       sudo kernel-install -v remove 6.0-rc1-local-gcafec0cacaca0
       # * Note, on some distributions kernel-install is missing
       #   or does only part of the job.

  b) If you performed a bisection and successfully validated the result, feel
     free to remove all kernels built during the actual bisection (Segment 3 c);
     the kernels you built earlier and later you might want to keep around for
     a week or two.

* **Optional task**: test a debug patch or a proposed fix later::

    git fetch mainline
    git switch --discard-changes --detach mainline/master
    git apply /tmp/foobars-proposed-fix-v1.patch
    cp ~/kernel-config-working .config
    ./scripts/config --set-str CONFIG_LOCALVERSION '-local-foobars-fix-v1'

  Build, install, and boot a kernel as described in *segment 1, section b* --
  but this time omit the first command copying the build configuration over,
  as that has been taken care of already.

.. _introguide_bissbs:

Step-by-step guide on how to verify bugs and bisect regressions
===============================================================

This guide describes how to set up your own Linux kernels for investigating bugs
or regressions you intend to report. How far you want to follow the instructions
depends on your issue:

Execute all steps till the end of *segment 1* to **verify if your kernel problem
is present in code supported by Linux kernel developers**. If it is, you are all
set to report the bug -- unless it did not happen with earlier kernel versions,
as then your want to at least continue with *segment 2* to **check if the issue
qualifies as regression** which receive priority treatment. Depending on the
outcome you then are ready to report a bug or submit a preliminary regression
report; instead of the latter your could also head straight on and follow
*segment 3* to **perform a bisection** for a full-fledged regression report
developers are obliged to act upon.

 :ref:`Preparations: set up everything to build your own kernels <introprep_bissbs>`.

 :ref:`Segment 1: try to reproduce the problem with the latest codebase <introlatestcheck_bissbs>`.

 :ref:`Segment 2: check if the kernels you build work fine <introworkingcheck_bissbs>`.

 :ref:`Segment 3: perform a bisection and validate the result <introbisect_bissbs>`.

 :ref:`Complementary tasks: cleanup during and after following this guide <introclosure_bissbs>`.

 :ref:`Optional tasks: test reverts, patches, or later versions <introoptional_bissbs>`.

The steps in each segment illustrate the important aspects of the process, while
a comprehensive reference section holds additional details for almost all of the
steps. The reference section sometimes also outlines alternative approaches,
pitfalls, as well as problems that might occur at the particular step -- and how
to get things rolling again.

For further details on how to report Linux kernel issues or regressions check
out Documentation/admin-guide/reporting-issues.rst, which works in conjunction
with this document. It among others explains why you need to verify bugs with
the latest 'mainline' kernel (e.g. versions like 6.0, 6.1-rc1, or 6.1-rc6),
even if you face a problem with a kernel from a 'stable/longterm' series
(say 6.0.13).

For users facing a regression that document also explains why sending a
preliminary report after segment 2 might be wise, as the regression and its
culprit might be known already. For further details on what actually qualifies
as a regression check out Documentation/admin-guide/reporting-regressions.rst.

If you run into any problems while following this guide or have ideas how to
improve it, :ref:`please let the kernel developers know <submit_improvements>`.

.. _introprep_bissbs:

Preparations: set up everything to build your own kernels
---------------------------------------------------------

The following steps lay the groundwork for all further tasks.

Note: the instructions assume you are building and testing on the same
machine; if you want to compile the kernel on another system, check
:ref:`Build kernels on a different machine <buildhost_bis>` below.

.. _backup_bissbs:

* Create a fresh backup and put system repair and restore tools at hand, just
  to be prepared for the unlikely case of something going sideways.

  [:ref:`details <backup_bisref>`]

.. _vanilla_bissbs:

* Remove all software that depends on externally developed kernel drivers or
  builds them automatically. That includes but is not limited to DKMS, openZFS,
  VirtualBox, and Nvidia's graphics drivers (including the GPLed kernel module).

  [:ref:`details <vanilla_bisref>`]

.. _secureboot_bissbs:

* On platforms with 'Secure Boot' or similar solutions, prepare everything to
  ensure the system will permit your self-compiled kernel to boot. The
  quickest and easiest way to achieve this on commodity x86 systems is to
  disable such techniques in the BIOS setup utility; alternatively, remove
  their restrictions through a process initiated by
  ``mokutil --disable-validation``.

  [:ref:`details <secureboot_bisref>`]

.. _rangecheck_bissbs:

* Determine the kernel versions considered 'good' and 'bad' throughout this
  guide:

  * Do you follow this guide to verify if a bug is present in the code the
    primary developers care for? Then consider the version of the newest kernel
    you regularly use currently as 'good' (e.g. 6.0, 6.0.13, or 6.1-rc2).

  * Do you face a regression, e.g. something broke or works worse after
    switching to a newer kernel version? In that case it depends on the version
    range during which the problem appeared:

    * Something regressed when updating from a stable/longterm release
      (say 6.0.13) to a newer mainline series (like 6.1-rc7 or 6.1) or a
      stable/longterm version based on one (say 6.1.5)? Then consider the
      mainline release your working kernel is based on to be the 'good'
      version (e.g. 6.0) and the first version to be broken as the 'bad' one
      (e.g. 6.1-rc7, 6.1, or 6.1.5). Note, at this point it is merely assumed
      that 6.0 is fine; this hypothesis will be checked in segment 2.

    * Something regressed when switching from one mainline version (say 6.0) to
      a later one (like 6.1-rc1) or a stable/longterm release based on it
      (say 6.1.5)? Then regard the last working version (e.g. 6.0) as 'good' and
      the first broken (e.g. 6.1-rc1 or 6.1.5) as 'bad'.

    * Something regressed when updating within a stable/longterm series (say
      from 6.0.13 to 6.0.15)? Then consider those versions as 'good' and 'bad'
      (e.g. 6.0.13 and 6.0.15), as you need to bisect within that series.

  *Note, do not confuse 'good' version with 'working' kernel; the latter term
  throughout this guide will refer to the last kernel that has been working
  fine.*

  [:ref:`details <rangecheck_bisref>`]

.. _bootworking_bissbs:

* Boot into the 'working' kernel and briefly use the apparently broken feature.

  [:ref:`details <bootworking_bisref>`]

.. _diskspace_bissbs:

* Ensure to have enough free space for building Linux. 15 Gigabyte in your home
  directory should typically suffice. If you have less available, be sure to pay
  attention to later steps about retrieving the Linux sources and handling of
  debug symbols: both explain approaches reducing the amount of space, which
  should allow you to master these tasks with about 4 Gigabytes free space.

  [:ref:`details <diskspace_bisref>`]

.. _buildrequires_bissbs:

* Install all software required to build a Linux kernel. Often you will need:
  'bc', 'binutils' ('ld' et al.), 'bison', 'flex', 'gcc', 'git', 'openssl',
  'pahole', 'perl', and the development headers for 'libelf' and 'openssl'. The
  reference section shows how to quickly install those on various popular Linux
  distributions.

  [:ref:`details <buildrequires_bisref>`]

.. _sources_bissbs:

* Retrieve the mainline Linux sources; then change into the directory holding
  them, as all further commands in this guide are meant to be executed from
  there.

  *Note, the following describe how to retrieve the sources using a full
  mainline clone, which downloads about 2,75 GByte as of early 2024. The*
  :ref:`reference section describes two alternatives <sources_bisref>` *:
  one downloads less than 500 MByte, the other works better with unreliable
  internet connections.*

  Execute the following command to retrieve a fresh mainline codebase while
  preparing things to add branches for stable/longterm series later::

    git clone -o mainline --no-checkout \
      https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git ~/linux/
    cd ~/linux/
    git remote add -t master stable \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

  [:ref:`details <sources_bisref>`]

.. _stablesources_bissbs:

* Is one of the versions you earlier established as 'good' or 'bad' a stable or
  longterm release (say 6.1.5)? Then download the code for the series it belongs
  to ('linux-6.1.y' in this example)::

    git remote set-branches --add stable linux-6.1.y
    git fetch stable

.. _oldconfig_bissbs:

* Start preparing a kernel build configuration (the '.config' file).

  Before doing so, ensure you are still running the 'working' kernel an earlier
  step told you to boot; if you are unsure, check the current kernelrelease
  identifier using ``uname -r``.

  Afterwards check out the source code for the version earlier established as
  'good'. In the following example command this is assumed to be 6.0; note that
  the version number in this and all later Git commands needs to be prefixed
  with a 'v'::

    git switch --discard-changes --detach v6.0

  Now create a build configuration file::

    make olddefconfig

  The kernel build scripts then will try to locate the build configuration file
  for the running kernel and then adjust it for the needs of the kernel sources
  you checked out. While doing so, it will print a few lines you need to check.

  Look out for a line starting with '# using defaults found in'. It should be
  followed by a path to a file in '/boot/' that contains the release identifier
  of your currently working kernel. If the line instead continues with something
  like 'arch/x86/configs/x86_64_defconfig', then the build infra failed to find
  the .config file for your running kernel -- in which case you have to put one
  there manually, as explained in the reference section.

  In case you can not find such a line, look for one containing '# configuration
  written to .config'. If that's the case you have a stale build configuration
  lying around. Unless you intend to use it, delete it; afterwards run
  'make olddefconfig' again and check if it now picked up the right config file
  as base.

  [:ref:`details <oldconfig_bisref>`]

.. _localmodconfig_bissbs:

* Disable any kernel modules apparently superfluous for your setup. This is
  optional, but especially wise for bisections, as it speeds up the build
  process enormously -- at least unless the .config file picked up in the
  previous step was already tailored to your and your hardware needs, in which
  case you should skip this step.

  To prepare the trimming, connect external hardware you occasionally use (USB
  keys, tokens, ...), quickly start a VM, and bring up VPNs. And if you rebooted
  since you started that guide, ensure that you tried using the feature causing
  trouble since you started the system. Only then trim your .config::

     yes '' | make localmodconfig

  There is a catch to this, as the 'apparently' in initial sentence of this step
  and the preparation instructions already hinted at:

  The 'localmodconfig' target easily disables kernel modules for features only
  used occasionally -- like modules for external peripherals not yet connected
  since booting, virtualization software not yet utilized, VPN tunnels, and a
  few other things. That's because some tasks rely on kernel modules Linux only
  loads when you execute tasks like the aforementioned ones for the first time.

  This drawback of localmodconfig is nothing you should lose sleep over, but
  something to keep in mind: if something is misbehaving with the kernels built
  during this guide, this is most likely the reason. You can reduce or nearly
  eliminate the risk with tricks outlined in the reference section; but when
  building a kernel just for quick testing purposes this is usually not worth
  spending much effort on, as long as it boots and allows to properly test the
  feature that causes trouble.

  [:ref:`details <localmodconfig_bisref>`]

.. _tagging_bissbs:

* Ensure all the kernels you will build are clearly identifiable using a special
  tag and a unique version number::

    ./scripts/config --set-str CONFIG_LOCALVERSION '-local'
    ./scripts/config -e CONFIG_LOCALVERSION_AUTO

  [:ref:`details <tagging_bisref>`]

.. _debugsymbols_bissbs:

* Decide how to handle debug symbols.

  In the context of this document it is often wise to enable them, as there is a
  decent chance you will need to decode a stack trace from a 'panic', 'Oops',
  'warning', or 'BUG'::

    ./scripts/config -d DEBUG_INFO_NONE -e KALLSYMS_ALL -e DEBUG_KERNEL \
      -e DEBUG_INFO -e DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT -e KALLSYMS

  But if you are extremely short on storage space, you might want to disable
  debug symbols instead::

    ./scripts/config -d DEBUG_INFO -d DEBUG_INFO_DWARF_TOOLCHAIN_DEFAULT \
      -d DEBUG_INFO_DWARF4 -d DEBUG_INFO_DWARF5 -e CONFIG_DEBUG_INFO_NONE

  [:ref:`details <debugsymbols_bisref>`]

.. _configmods_bissbs:

* Check if you may want or need to adjust some other kernel configuration
  options:

  * Are you running Debian? Then you want to avoid known problems by performing
    additional adjustments explained in the reference section.

    [:ref:`details <configmods_distros_bisref>`].

  * If you want to influence other aspects of the configuration, do so now using
    your preferred tool. Note, to use make targets like 'menuconfig' or
    'nconfig', you will need to install the development files of ncurses; for
    'xconfig' you likewise need the Qt5 or Qt6 headers.

    [:ref:`details <configmods_individual_bisref>`].

.. _saveconfig_bissbs:

* Reprocess the .config after the latest adjustments and store it in a safe
  place::

     make olddefconfig
     cp .config ~/kernel-config-working

  [:ref:`details <saveconfig_bisref>`]

.. _introlatestcheck_bissbs:

Segment 1: try to reproduce the problem with the latest codebase
----------------------------------------------------------------

The following steps verify if the problem occurs with the code currently
supported by developers. In case you face a regression, it also checks that the
problem is not caused by some .config change, as reporting the issue then would
be a waste of time. [:ref:`details <introlatestcheck_bisref>`]

.. _checkoutmaster_bissbs:

* Check out the latest Linux codebase.

  * Are your 'good' and 'bad' versions from the same stable or longterm series?
    Then check the `front page of kernel.org <https://kernel.org/>`_: if it
    lists a release from that series without an '[EOL]' tag, checkout the series
    latest version ('linux-6.1.y' in the following example)::

      cd ~/linux/
      git switch --discard-changes --detach stable/linux-6.1.y

    Your series is unsupported, if is not listed or carrying a 'end of life'
    tag. In that case you might want to check if a successor series (say
    linux-6.2.y) or mainline (see next point) fix the bug.

  * In all other cases, run::

      cd ~/linux/
      git switch --discard-changes --detach mainline/master

  [:ref:`details <checkoutmaster_bisref>`]

.. _build_bissbs:

* Build the image and the modules of your first kernel using the config file you
  prepared::

    cp ~/kernel-config-working .config
    make olddefconfig
    make -j $(nproc --all)

  If you want your kernel packaged up as deb, rpm, or tar file, see the
  reference section for alternatives, which obviously will require other
  steps to install as well.

  [:ref:`details <build_bisref>`]

.. _install_bissbs:

* Install your newly built kernel.

  Before doing so, consider checking if there is still enough space for it::

    df -h /boot/ /lib/modules/

  For now assume 150 MByte in /boot/ and 200 in /lib/modules/ will suffice; how
  much your kernels actually require will be determined later during this guide.

  Now install the kernel's modules and its image, which will be stored in
  parallel to the your Linux distribution's kernels::

    sudo make modules_install
    command -v installkernel && sudo make install

  The second command ideally will take care of three steps required at this
  point: copying the kernel's image to /boot/, generating an initramfs, and
  adding an entry for both to the boot loader's configuration.

  Sadly some distributions (among them Arch Linux, its derivatives, and many
  immutable Linux distributions) will perform none or only some of those tasks.
  You therefore want to check if all of them were taken care of and manually
  perform those that were not. The reference section provides further details on
  that; your distribution's documentation might help, too.

  Once you figured out the steps needed at this point, consider writing them
  down: if you will build more kernels as described in segment 2 and 3, you will
  have to perform those again after executing ``command -v installkernel [...]``.

  [:ref:`details <install_bisref>`]

.. _storagespace_bissbs:

* In case you plan to follow this guide further, check how much storage space
  the kernel, its modules, and other related files like the initramfs consume::

    du -ch /boot/*$(make -s kernelrelease)* | tail -n 1
    du -sh /lib/modules/$(make -s kernelrelease)/

  Write down or remember those two values for later: they enable you to prevent
  running out of disk space accidentally during a bisection.

  [:ref:`details <storagespace_bisref>`]

.. _kernelrelease_bissbs:

* Show and store the kernelrelease identifier of the kernel you just built::

    make -s kernelrelease | tee -a ~/kernels-built

  Remember the identifier momentarily, as it will help you pick the right kernel
  from the boot menu upon restarting.

* Reboot into your newly built kernel. To ensure your actually started the one
  you just built, you might want to verify if the output of these commands
  matches::

    tail -n 1 ~/kernels-built
    uname -r

.. _tainted_bissbs:

* Check if the kernel marked itself as 'tainted'::

    cat /proc/sys/kernel/tainted

  If that command does not return '0', check the reference section, as the cause
  for this might interfere with your testing.

  [:ref:`details <tainted_bisref>`]

.. _recheckbroken_bissbs:

* Verify if your bug occurs with the newly built kernel. If it does not, check
  out the instructions in the reference section to ensure nothing went sideways
  during your tests.

  [:ref:`details <recheckbroken_bisref>`]

.. _recheckstablebroken_bissbs:

* Did you just built a stable or longterm kernel? And were you able to reproduce
  the regression with it? Then you should test the latest mainline codebase as
  well, because the result determines which developers the bug must be submitted
  to.

  To prepare that test, check out current mainline::

    cd ~/linux/
    git switch --discard-changes --detach mainline/master

  Now use the checked out code to build and install another kernel using the
  commands the earlier steps already described in more detail::

    cp ~/kernel-config-working .config
    make olddefconfig
    make -j $(nproc --all)
    # * Check if the free space suffices holding another kernel:
    df -h /boot/ /lib/modules/
    sudo make modules_install
    command -v installkernel && sudo make install
    make -s kernelrelease | tee -a ~/kernels-built
    reboot

  Confirm you booted the kernel you intended to start and check its tainted
  status::

    tail -n 1 ~/kernels-built
    uname -r
    cat /proc/sys/kernel/tainted

  Now verify if this kernel is showing the problem. If it does, then you need
  to report the bug to the primary developers; if it does not, report it to the
  stable team. See Documentation/admin-guide/reporting-issues.rst for details.

  [:ref:`details <recheckstablebroken_bisref>`]

Do you follow this guide to verify if a problem is present in the code
currently supported by Linux kernel developers? Then you are done at this
point. If you later want to remove the kernel you just built, check out
:ref:`Complementary tasks: cleanup during and after following this guide <introclosure_bissbs>`.

In case you face a regression, move on and execute at least the next segment
as well.

.. _introworkingcheck_bissbs:

Segment 2: check if the kernels you build work fine
---------------------------------------------------

In case of a regression, you now want to ensure the trimmed configuration file
you created earlier works as expected; a bisection with the .config file
otherwise would be a waste of time. [:ref:`details <introworkingcheck_bisref>`]

.. _recheckworking_bissbs:

* Build your own variant of the 'working' kernel and check if the feature that
  regressed works as expected with it.

  Start by checking out the sources for the version earlier established as
  'good' (once again assumed to be 6.0 here)::

    cd ~/linux/
    git switch --discard-changes --detach v6.0

  Now use the checked out code to configure, build, and install another kernel
  using the commands the previous subsection explained in more detail::

    cp ~/kernel-config-working .config
    make olddefconfig
    make -j $(nproc --all)
    # * Check if the free space suffices holding another kernel:
    df -h /boot/ /lib/modules/
    sudo make modules_install
    command -v installkernel && sudo make install
    make -s kernelrelease | tee -a ~/kernels-built
    reboot

  When the system booted, you may want to verify once again that the
  kernel you started is the one you just built::

    tail -n 1 ~/kernels-built
    uname -r

  Now check if this kernel works as expected; if not, consult the reference
  section for further instructions.

  [:ref:`details <recheckworking_bisref>`]

.. _introbisect_bissbs:

Segment 3: perform the bisection and validate the result
--------------------------------------------------------

With all the preparations and precaution builds taken care of, you are now ready
to begin the bisection. This will make you build quite a few kernels -- usually
about 15 in case you encountered a regression when updating to a newer series
(say from 6.0.13 to 6.1.5). But do not worry, due to the trimmed build
configuration created earlier this works a lot faster than many people assume:
overall on average it will often just take about 10 to 15 minutes to compile
each kernel on commodity x86 machines.

.. _bisectstart_bissbs:

* Start the bisection and tell Git about the versions earlier established as
  'good' (6.0 in the following example command) and 'bad' (6.1.5)::

    cd ~/linux/
    git bisect start
    git bisect good v6.0
    git bisect bad v6.1.5

  [:ref:`details <bisectstart_bisref>`]

.. _bisectbuild_bissbs:

* Now use the code Git checked out to build, install, and boot a kernel using
  the commands introduced earlier::

    cp ~/kernel-config-working .config
    make olddefconfig
    make -j $(nproc --all)
    # * Check if the free space suffices holding another kernel:
    df -h /boot/ /lib/modules/
    sudo make modules_install
    command -v installkernel && sudo make install
    make -s kernelrelease | tee -a ~/kernels-built
    reboot

  If compilation fails for some reason, run ``git bisect skip`` and restart
  executing the stack of commands from the beginning.

  In case you skipped the 'test latest codebase' step in the guide, check its
  description as for why the 'df [...]' and 'make -s kernelrelease [...]'
  commands are here.

  Important note: the latter command from this point on will print release
  identifiers that might look odd or wrong to you -- which they are not, as it's
  totally normal to see release identifiers like '6.0-rc1-local-gcafec0cacaca0'
  if you bisect between versions 6.1 and 6.2 for example.

  [:ref:`details <bisectbuild_bisref>`]

.. _bisecttest_bissbs:

* Now check if the feature that regressed works in the kernel you just built.

  You again might want to start by making sure the kernel you booted is the one
  you just built::

    cd ~/linux/
    tail -n 1 ~/kernels-built
    uname -r

  Now verify if the feature that regressed works at this kernel bisection point.
  If it does, run this::

    git bisect good

  If it does not, run this::

    git bisect bad

  Be sure about what you tell Git, as getting this wrong just once will send the
  rest of the bisection totally off course.

  While the bisection is ongoing, Git will use the information you provided to
  find and check out another bisection point for you to test. While doing so, it
  will print something like 'Bisecting: 675 revisions left to test after this
  (roughly 10 steps)' to indicate how many further changes it expects to be
  tested. Now build and install another kernel using the instructions from the
  previous step; afterwards follow the instructions in this step again.

  Repeat this again and again until you finish the bisection -- that's the case
  when Git after tagging a change as 'good' or 'bad' prints something like
  'cafecaca0c0dacafecaca0c0dacafecaca0c0da is the first bad commit'; right
  afterwards it will show some details about the culprit including the patch
  description of the change. The latter might fill your terminal screen, so you
  might need to scroll up to see the message mentioning the culprit;
  alternatively, run ``git bisect log > ~/bisection-log``.

  [:ref:`details <bisecttest_bisref>`]

.. _bisectlog_bissbs:

* Store Git's bisection log and the current .config file in a safe place before
  telling Git to reset the sources to the state before the bisection::

    cd ~/linux/
    git bisect log > ~/bisection-log
    cp .config ~/bisection-config-culprit
    git bisect reset

  [:ref:`details <bisectlog_bisref>`]

.. _revert_bissbs:

* Try reverting the culprit on top of latest mainline to see if this fixes your
  regression.

  This is optional, as it might be impossible or hard to realize. The former is
  the case, if the bisection determined a merge commit as the culprit; the
  latter happens if other changes depend on the culprit. But if the revert
  succeeds, it is worth building another kernel, as it validates the result of
  a bisection, which can easily deroute; it furthermore will let kernel
  developers know, if they can resolve the regression with a quick revert.

  Begin by checking out the latest codebase depending on the range you bisected:

  * Did you face a regression within a stable/longterm series (say between
    6.0.13 and 6.0.15) that does not happen in mainline? Then check out the
    latest codebase for the affected series like this::

      git fetch stable
      git switch --discard-changes --detach linux-6.0.y

  * In all other cases check out latest mainline::

      git fetch mainline
      git switch --discard-changes --detach mainline/master

    If you bisected a regression within a stable/longterm series that also
    happens in mainline, there is one more thing to do: look up the mainline
    commit-id. To do so, use a command like ``git show abcdcafecabcd`` to
    view the patch description of the culprit. There will be a line near
    the top which looks like 'commit cafec0cacaca0 upstream.' or
    'Upstream commit cafec0cacaca0'; use that commit-id in the next command
    and not the one the bisection blamed.

  Now try reverting the culprit by specifying its commit id::

    git revert --no-edit cafec0cacaca0

  If that fails, give up trying and move on to the next step; if it works,
  adjust the tag to facilitate the identification and prevent accidentally
  overwriting another kernel::

    cp ~/kernel-config-working .config
    ./scripts/config --set-str CONFIG_LOCALVERSION '-local-cafec0cacaca0-reverted'

  Build a kernel using the familiar command sequence, just without copying the
  the base .config over::

    make olddefconfig &&
    make -j $(nproc --all)
    # * Check if the free space suffices holding another kernel:
    df -h /boot/ /lib/modules/
    sudo make modules_install
    command -v installkernel && sudo make install
    make -s kernelrelease | tee -a ~/kernels-built
    reboot

  Now check one last time if the feature that made you perform a bisection works
  with that kernel: if everything went well, it should not show the regression.

  [:ref:`details <revert_bisref>`]

.. _introclosure_bissbs:

Complementary tasks: cleanup during and after the bisection
-----------------------------------------------------------

During and after following this guide you might want or need to remove some of
the kernels you installed: the boot menu otherwise will become confusing or
space might run out.

.. _makeroom_bissbs:

* To remove one of the kernels you installed, look up its 'kernelrelease'
  identifier. This guide stores them in '~/kernels-built', but the following
  command will print them as well::

    ls -ltr /lib/modules/*-local*

  You in most situations want to remove the oldest kernels built during the
  actual bisection (e.g. segment 3 of this guide). The two ones you created
  beforehand (e.g. to test the latest codebase and the version considered
  'good') might become handy to verify something later -- thus better keep them
  around, unless you are really short on storage space.

  To remove the modules of a kernel with the kernelrelease identifier
  '*6.0-rc1-local-gcafec0cacaca0*', start by removing the directory holding its
  modules::

    sudo rm -rf /lib/modules/6.0-rc1-local-gcafec0cacaca0

  Afterwards try the following command::

    sudo kernel-install -v remove 6.0-rc1-local-gcafec0cacaca0

  On quite a few distributions this will delete all other kernel files installed
  while also removing the kernel's entry from the boot menu. But on some
  distributions kernel-install does not exist or leaves boot-loader entries or
  kernel image and related files behind; in that case remove them as described
  in the reference section.

  [:ref:`details <makeroom_bisref>`]

.. _finishingtouch_bissbs:

* Once you have finished the bisection, do not immediately remove anything you
  set up, as you might need a few things again. What is safe to remove depends
  on the outcome of the bisection:

  * Could you initially reproduce the regression with the latest codebase and
    after the bisection were able to fix the problem by reverting the culprit on
    top of the latest codebase? Then you want to keep those two kernels around
    for a while, but safely remove all others with a '-local' in the release
    identifier.

  * Did the bisection end on a merge-commit or seems questionable for other
    reasons? Then you want to keep as many kernels as possible around for a few
    days: it's pretty likely that you will be asked to recheck something.

  * In other cases it likely is a good idea to keep the following kernels around
    for some time: the one built from the latest codebase, the one created from
    the version considered 'good', and the last three or four you compiled
    during the actual bisection process.

  [:ref:`details <finishingtouch_bisref>`]

.. _introoptional_bissbs:

Optional: test reverts, patches, or later versions
--------------------------------------------------

While or after reporting a bug, you might want or potentially will be asked to
test reverts, debug patches, proposed fixes, or other versions. In that case
follow these instructions.

* Update your Git clone and check out the latest code.

  * In case you want to test mainline, fetch its latest changes before checking
    its code out::

      git fetch mainline
      git switch --discard-changes --detach mainline/master

  * In case you want to test a stable or longterm kernel, first add the branch
    holding the series you are interested in (6.2 in the example), unless you
    already did so earlier::

      git remote set-branches --add stable linux-6.2.y

    Then fetch the latest changes and check out the latest version from the
    series::

      git fetch stable
      git switch --discard-changes --detach stable/linux-6.2.y

* Copy your kernel build configuration over::

    cp ~/kernel-config-working .config

* Your next step depends on what you want to do:

  * In case you just want to test the latest codebase, head to the next step,
    you are already all set.

  * In case you want to test if a revert fixes an issue, revert one or multiple
    changes by specifying their commit ids::

      git revert --no-edit cafec0cacaca0

    Now give that kernel a special tag to facilitates its identification and
    prevent accidentally overwriting another kernel::

      ./scripts/config --set-str CONFIG_LOCALVERSION '-local-cafec0cacaca0-reverted'

  * In case you want to test a patch, store the patch in a file like
    '/tmp/foobars-proposed-fix-v1.patch' and apply it like this::

      git apply /tmp/foobars-proposed-fix-v1.patch

    In case of multiple patches, repeat this step with the others.

    Now give that kernel a special tag to facilitates its identification and
    prevent accidentally overwriting another kernel::

    ./scripts/config --set-str CONFIG_LOCALVERSION '-local-foobars-fix-v1'

* Build a kernel using the familiar commands, just without copying the kernel
  build configuration over, as that has been taken care of already::

    make olddefconfig &&
    make -j $(nproc --all)
    # * Check if the free space suffices holding another kernel:
    df -h /boot/ /lib/modules/
    sudo make modules_install
    command -v installkernel && sudo make install
    make -s kernelrelease | tee -a ~/kernels-built
    reboot

* Now verify you booted the newly built kernel and check it.

[:ref:`details <introoptional_bisref>`]

.. _submit_improvements:

Conclusion
----------

You have reached the end of the step-by-step guide.

Did you run into trouble following any of the above steps not cleared up by the
reference section below? Did you spot errors? Or do you have ideas how to
improve the guide?

If any of that applies, please take a moment and let the maintainer of this
document know by email (Thorsten Leemhuis <linux@leemhuis.info>), ideally while
CCing the Linux docs mailing list (linux-doc@vger.kernel.org). Such feedback is
vital to improve this text further, which is in everybody's interest, as it
will enable more people to master the task described here -- and hopefully also
improve similar guides inspired by this one.


Reference section for the step-by-step guide
============================================

This section holds additional information for almost all the items in the above
step-by-step guide.

Preparations for building your own kernels
------------------------------------------

  *The steps in this section lay the groundwork for all further tests.*
  [:ref:`... <introprep_bissbs>`]

The steps in all later sections of this guide depend on those described here.

[:ref:`back to step-by-step guide <introprep_bissbs>`].

.. _backup_bisref:

Prepare for emergencies
~~~~~~~~~~~~~~~~~~~~~~~

  *Create a fresh backup and put system repair and restore tools at hand.*
  [:ref:`... <backup_bissbs>`]

Remember, you are dealing with computers, which sometimes do unexpected things
-- especially if you fiddle with crucial parts like the kernel of an operating
system. That's what you are about to do in this process. Hence, better prepare
for something going sideways, even if that should not happen.

[:ref:`back to step-by-step guide <backup_bissbs>`]

.. _vanilla_bisref:

Remove anything related to externally maintained kernel modules
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Remove all software that depends on externally developed kernel drivers or
  builds them automatically.* [:ref:`...<vanilla_bissbs>`]

Externally developed kernel modules can easily cause trouble during a bisection.

But there is a more important reason why this guide contains this step: most
kernel developers will not care about reports about regressions occurring with
kernels that utilize such modules. That's because such kernels are not
considered 'vanilla' anymore, as Documentation/admin-guide/reporting-issues.rst
explains in more detail.

[:ref:`back to step-by-step guide <vanilla_bissbs>`]

.. _secureboot_bisref:

Deal with techniques like Secure Boot
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *On platforms with 'Secure Boot' or similar techniques, prepare everything to
  ensure the system will permit your self-compiled kernel to boot later.*
  [:ref:`... <secureboot_bissbs>`]

Many modern systems allow only certain operating systems to start; that's why
they reject booting self-compiled kernels by default.

You ideally deal with this by making your platform trust your self-built kernels
with the help of a certificate. How to do that is not described
here, as it requires various steps that would take the text too far away from
its purpose; 'Documentation/admin-guide/module-signing.rst' and various web
sides already explain everything needed in more detail.

Temporarily disabling solutions like Secure Boot is another way to make your own
Linux boot. On commodity x86 systems it is possible to do this in the BIOS Setup
utility; the required steps vary a lot between machines and therefore cannot be
described here.

On mainstream x86 Linux distributions there is a third and universal option:
disable all Secure Boot restrictions for your Linux environment. You can
initiate this process by running ``mokutil --disable-validation``; this will
tell you to create a one-time password, which is safe to write down. Now
restart; right after your BIOS performed all self-tests the bootloader Shim will
show a blue box with a message 'Press any key to perform MOK management'. Hit
some key before the countdown exposes, which will open a menu. Choose 'Change
Secure Boot state'. Shim's 'MokManager' will now ask you to enter three
randomly chosen characters from the one-time password specified earlier. Once
you provided them, confirm you really want to disable the validation.
Afterwards, permit MokManager to reboot the machine.

[:ref:`back to step-by-step guide <secureboot_bissbs>`]

.. _bootworking_bisref:

Boot the last kernel that was working
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Boot into the last working kernel and briefly recheck if the feature that
  regressed really works.* [:ref:`...<bootworking_bissbs>`]

This will make later steps that cover creating and trimming the configuration do
the right thing.

[:ref:`back to step-by-step guide <bootworking_bissbs>`]

.. _diskspace_bisref:

Space requirements
~~~~~~~~~~~~~~~~~~

  *Ensure to have enough free space for building Linux.*
  [:ref:`... <diskspace_bissbs>`]

The numbers mentioned are rough estimates with a big extra charge to be on the
safe side, so often you will need less.

If you have space constraints, be sure to hay attention to the :ref:`step about
debug symbols' <debugsymbols_bissbs>` and its :ref:`accompanying reference
section' <debugsymbols_bisref>`, as disabling then will reduce the consumed disk
space by quite a few gigabytes.

[:ref:`back to step-by-step guide <diskspace_bissbs>`]

.. _rangecheck_bisref:

Bisection range
~~~~~~~~~~~~~~~

  *Determine the kernel versions considered 'good' and 'bad' throughout this
  guide.* [:ref:`...<rangecheck_bissbs>`]

Establishing the range of commits to be checked is mostly straightforward,
except when a regression occurred when switching from a release of one stable
series to a release of a later series (e.g. from 6.0.13 to 6.1.5). In that case
Git will need some hand holding, as there is no straight line of descent.

That's because with the release of 6.0 mainline carried on to 6.1 while the
stable series 6.0.y branched to the side. It's therefore theoretically possible
that the issue you face with 6.1.5 only worked in 6.0.13, as it was fixed by a
commit that went into one of the 6.0.y releases, but never hit mainline or the
6.1.y series. Thankfully that normally should not happen due to the way the
stable/longterm maintainers maintain the code. It's thus pretty safe to assume
6.0 as a 'good' kernel. That assumption will be tested anyway, as that kernel
will be built and tested in the segment '2' of this guide; Git would force you
to do this as well, if you tried bisecting between 6.0.13 and 6.1.15.

[:ref:`back to step-by-step guide <rangecheck_bissbs>`]

.. _buildrequires_bisref:

Install build requirements
~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Install all software required to build a Linux kernel.*
  [:ref:`...<buildrequires_bissbs>`]

The kernel is pretty stand-alone, but besides tools like the compiler you will
sometimes need a few libraries to build one. How to install everything needed
depends on your Linux distribution and the configuration of the kernel you are
about to build.

Here are a few examples what you typically need on some mainstream
distributions:

* Arch Linux and derivatives::

    sudo pacman --needed -S bc binutils bison flex gcc git kmod libelf openssl \
      pahole perl zlib ncurses qt6-base

* Debian, Ubuntu, and derivatives::

    sudo apt install bc binutils bison dwarves flex gcc git kmod libelf-dev \
      libssl-dev make openssl pahole perl-base pkg-config zlib1g-dev \
      libncurses-dev qt6-base-dev g++

* Fedora and derivatives::

    sudo dnf install binutils \
      /usr/bin/{bc,bison,flex,gcc,git,openssl,make,perl,pahole,rpmbuild} \
      /usr/include/{libelf.h,openssl/pkcs7.h,zlib.h,ncurses.h,qt6/QtGui/QAction}

* openSUSE and derivatives::

    sudo zypper install bc binutils bison dwarves flex gcc git \
      kernel-install-tools libelf-devel make modutils openssl openssl-devel \
      perl-base zlib-devel rpm-build ncurses-devel qt6-base-devel

These commands install a few packages that are often, but not always needed. You
for example might want to skip installing the development headers for ncurses,
which you will only need in case you later might want to adjust the kernel build
configuration using make the targets 'menuconfig' or 'nconfig'; likewise omit
the headers of Qt6 if you do not plan to adjust the .config using 'xconfig'.

You furthermore might need additional libraries and their development headers
for tasks not covered in this guide -- for example when building utilities from
the kernel's tools/ directory.

[:ref:`back to step-by-step guide <buildrequires_bissbs>`]

.. _sources_bisref:

Download the sources using Git
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Retrieve the Linux mainline sources.*
  [:ref:`...<sources_bissbs>`]

The step-by-step guide outlines how to download the Linux sources using a full
Git clone of Linus' mainline repository. There is nothing more to say about
that -- but there are two alternatives ways to retrieve the sources that might
work better for you:

* If you have an unreliable internet connection, consider
  :ref:`using a 'Git bundle'<sources_bundle_bisref>`.

* If downloading the complete repository would take too long or requires too
  much storage space, consider :ref:`using a 'shallow
  clone'<sources_shallow_bisref>`.

.. _sources_bundle_bisref:

Downloading Linux mainline sources using a bundle
"""""""""""""""""""""""""""""""""""""""""""""""""

Use the following commands to retrieve the Linux mainline sources using a
bundle::

    wget -c \
      https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/clone.bundle
    git clone --no-checkout clone.bundle ~/linux/
    cd ~/linux/
    git remote remove origin
    git remote add mainline \
      https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git
    git fetch mainline
    git remote add -t master stable \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

In case the 'wget' command fails, just re-execute it, it will pick up where
it left off.

[:ref:`back to step-by-step guide <sources_bissbs>`]
[:ref:`back to section intro <sources_bisref>`]

.. _sources_shallow_bisref:

Downloading Linux mainline sources using a shallow clone
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

First, execute the following command to retrieve the latest mainline codebase::

    git clone -o mainline --no-checkout --depth 1 -b master \
      https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git ~/linux/
    cd ~/linux/
    git remote add -t master stable \
      https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git

Now deepen your clone's history to the second predecessor of the mainline
release of your 'good' version. In case the latter are 6.0 or 6.0.13, 5.19 would
be the first predecessor and 5.18 the second -- hence deepen the history up to
that version::

    git fetch --shallow-exclude=v5.18 mainline

Afterwards add the stable Git repository as remote and all required stable
branches as explained in the step-by-step guide.

Note, shallow clones have a few peculiar characteristics:

* For bisections the history needs to be deepened a few mainline versions
  farther than it seems necessary, as explained above already. That's because
  Git otherwise will be unable to revert or describe most of the commits within
  a range (say 6.1..6.2), as they are internally based on earlier kernels
  releases (like 6.0-rc2 or 5.19-rc3).

* This document in most places uses ``git fetch`` with ``--shallow-exclude=``
  to specify the earliest version you care about (or to be precise: its git
  tag). You alternatively can use the parameter ``--shallow-since=`` to specify
  an absolute (say ``'2023-07-15'``) or relative (``'12 months'``) date to
  define the depth of the history you want to download. When using them while
  bisecting mainline, ensure to deepen the history to at least 7 months before
  the release of the mainline release your 'good' kernel is based on.

* Be warned, when deepening your clone you might encounter an error like
  'fatal: error in object: unshallow cafecaca0c0dacafecaca0c0dacafecaca0c0da'.
  In that case run ``git repack -d`` and try again.

[:ref:`back to step-by-step guide <sources_bissbs>`]
[:ref:`back to section intro <sources_bisref>`]

.. _oldconfig_bisref:

Start defining the build configuration for your kernel
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Start preparing a kernel build configuration (the '.config' file).*
  [:ref:`... <oldconfig_bissbs>`]

*Note, this is the first of multiple steps in this guide that create or modify
build artifacts. The commands used in this guide store them right in the source
tree to keep things simple. In case you prefer storing the build artifacts
separately, create a directory like '~/linux-builddir/' and add the parameter
``O=~/linux-builddir/`` to all make calls used throughout this guide. You will
have to point other commands there as well -- among them the ``./scripts/config
[...]`` commands, which will require ``--file ~/linux-builddir/.config`` to
locate the right build configuration.*

Two things can easily go wrong when creating a .config file as advised:

* The oldconfig target will use a .config file from your build directory, if
  one is already present there (e.g. '~/linux/.config'). That's totally fine if
  that's what you intend (see next step), but in all other cases you want to
  delete it. This for example is important in case you followed this guide
  further, but due to problems come back here to redo the configuration from
  scratch.

* Sometimes olddefconfig is unable to locate the .config file for your running
  kernel and will use defaults, as briefly outlined in the guide. In that case
  check if your distribution ships the configuration somewhere and manually put
  it in the right place (e.g. '~/linux/.config') if it does. On distributions
  where /proc/config.gz exists this can be achieved using this command::

    zcat /proc/config.gz > .config

  Once you put it there, run ``make olddefconfig`` again to adjust it to the
  needs of the kernel about to be built.

Note, the olddefconfig target will set any undefined build options to their
default value. If you prefer to set such configuration options manually, use
``make oldconfig`` instead. Then for each undefined configuration option you
will be asked how to proceed; in case you are unsure what to answer, simply hit
'enter' to apply the default value. Note though that for bisections you normally
want to go with the defaults, as you otherwise might enable a new feature that
causes a problem looking like regressions (for example due to security
restrictions).

Occasionally odd things happen when trying to use a config file prepared for one
kernel (say 6.1) on an older mainline release -- especially if it is much older
(say 5.15). That's one of the reasons why the previous step in the guide told
you to boot the kernel where everything works. If you manually add a .config
file you thus want to ensure it's from the working kernel and not from a one
that shows the regression.

In case you want to build kernels for another machine, locate its kernel build
configuration; usually ``ls /boot/config-$(uname -r)`` will print its name. Copy
that file to the build machine and store it as ~/linux/.config; afterwards run
``make olddefconfig`` to adjust it.

[:ref:`back to step-by-step guide <oldconfig_bissbs>`]

.. _localmodconfig_bisref:

Trim the build configuration for your kernel
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Disable any kernel modules apparently superfluous for your setup.*
  [:ref:`... <localmodconfig_bissbs>`]

As explained briefly in the step-by-step guide already: with localmodconfig it
can easily happen that your self-built kernels will lack modules for tasks you
did not perform at least once before utilizing this make target. That happens
when a task requires kernel modules which are only autoloaded when you execute
it for the first time. So when you never performed that task since starting your
kernel the modules will not have been loaded -- and from localmodonfig's point
of view look superfluous, which thus disables them to reduce the amount of code
to be compiled.

You can try to avoid this by performing typical tasks that often will autoload
additional kernel modules: start a VM, establish VPN connections, loop-mount a
CD/DVD ISO, mount network shares (CIFS, NFS, ...), and connect all external
devices (2FA keys, headsets, webcams, ...) as well as storage devices with file
systems you otherwise do not utilize (btrfs, ext4, FAT, NTFS, XFS, ...). But it
is hard to think of everything that might be needed -- even kernel developers
often forget one thing or another at this point.

Do not let that risk bother you, especially when compiling a kernel only for
testing purposes: everything typically crucial will be there. And if you forget
something important you can turn on a missing feature manually later and quickly
run the commands again to compile and install a kernel that has everything you
need.

But if you plan to build and use self-built kernels regularly, you might want to
reduce the risk by recording which modules your system loads over the course of
a few weeks. You can automate this with `modprobed-db
<https://github.com/graysky2/modprobed-db>`_. Afterwards use ``LSMOD=<path>`` to
point localmodconfig to the list of modules modprobed-db noticed being used::

  yes '' | make LSMOD='${HOME}'/.config/modprobed.db localmodconfig

That parameter also allows you to build trimmed kernels for another machine in
case you copied a suitable .config over to use as base (see previous step). Just
run ``lsmod > lsmod_foo-machine`` on that system and copy the generated file to
your build's host home directory. Then run these commands instead of the one the
step-by-step guide mentions::

  yes '' | make LSMOD=~/lsmod_foo-machine localmodconfig

[:ref:`back to step-by-step guide <localmodconfig_bissbs>`]

.. _tagging_bisref:

Tag the kernels about to be build
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Ensure all the kernels you will build are clearly identifiable using a
  special tag and a unique version identifier.* [:ref:`... <tagging_bissbs>`]

This allows you to differentiate your distribution's kernels from those created
during this process, as the file or directories for the latter will contain
'-local' in the name; it also helps picking the right entry in the boot menu and
not lose track of you kernels, as their version numbers will look slightly
confusing during the bisection.

[:ref:`back to step-by-step guide <tagging_bissbs>`]

.. _debugsymbols_bisref:

Decide to enable or disable debug symbols
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Decide how to handle debug symbols.* [:ref:`... <debugsymbols_bissbs>`]

Having debug symbols available can be important when your kernel throws a
'panic', 'Oops', 'warning', or 'BUG' later when running, as then you will be
able to find the exact place where the problem occurred in the code. But
collecting and embedding the needed debug information takes time and consumes
quite a bit of space: in late 2022 the build artifacts for a typical x86 kernel
trimmed with localmodconfig consumed around 5 Gigabyte of space with debug
symbols, but less than 1 when they were disabled. The resulting kernel image and
modules are bigger as well, which increases storage requirements for /boot/ and
load times.

In case you want a small kernel and are unlikely to decode a stack trace later,
you thus might want to disable debug symbols to avoid those downsides. If it
later turns out that you need them, just enable them as shown and rebuild the
kernel.

You on the other hand definitely want to enable them for this process, if there
is a decent chance that you need to decode a stack trace later. The section
'Decode failure messages' in Documentation/admin-guide/reporting-issues.rst
explains this process in more detail.

[:ref:`back to step-by-step guide <debugsymbols_bissbs>`]

.. _configmods_bisref:

Adjust build configuration
~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Check if you may want or need to adjust some other kernel configuration
  options:*

Depending on your needs you at this point might want or have to adjust some
kernel configuration options.

.. _configmods_distros_bisref:

Distro specific adjustments
"""""""""""""""""""""""""""

  *Are you running* [:ref:`... <configmods_bissbs>`]

The following sections help you to avoid build problems that are known to occur
when following this guide on a few commodity distributions.

**Debian:**

* Remove a stale reference to a certificate file that would cause your build to
  fail::

   ./scripts/config --set-str SYSTEM_TRUSTED_KEYS ''

  Alternatively, download the needed certificate and make that configuration
  option point to it, as `the Debian handbook explains in more detail
  <https://debian-handbook.info/browse/stable/sect.kernel-compilation.html>`_
  -- or generate your own, as explained in
  Documentation/admin-guide/module-signing.rst.

[:ref:`back to step-by-step guide <configmods_bissbs>`]

.. _configmods_individual_bisref:

Individual adjustments
""""""""""""""""""""""

  *If you want to influence the other aspects of the configuration, do so
  now.* [:ref:`... <configmods_bissbs>`]

At this point you can use a command like ``make menuconfig`` or ``make nconfig``
to enable or disable certain features using a text-based user interface; to use
a graphical configuration utility, run ``make xconfig`` instead. Both of them
require development libraries from toolkits they are rely on (ncurses
respectively Qt5 or Qt6); an error message will tell you if something required
is missing.

[:ref:`back to step-by-step guide <configmods_bissbs>`]

.. _saveconfig_bisref:

Put the .config file aside
~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Reprocess the .config after the latest changes and store it in a safe place.*
  [:ref:`... <saveconfig_bissbs>`]

Put the .config you prepared aside, as you want to copy it back to the build
directory every time during this guide before you start building another
kernel. That's because going back and forth between different versions can alter
.config files in odd ways; those occasionally cause side effects that could
confuse testing or in some cases render the result of your bisection
meaningless.

[:ref:`back to step-by-step guide <saveconfig_bissbs>`]

.. _introlatestcheck_bisref:

Try to reproduce the problem with the latest codebase
-----------------------------------------------------

  *Verify the regression is not caused by some .config change and check if it
  still occurs with the latest codebase.* [:ref:`... <introlatestcheck_bissbs>`]

For some readers it might seem unnecessary to check the latest codebase at this
point, especially if you did that already with a kernel prepared by your
distributor or face a regression within a stable/longterm series. But it's
highly recommended for these reasons:

* You will run into any problems caused by your setup before you actually begin
  a bisection. That will make it a lot easier to differentiate between 'this
  most likely is some problem in my setup' and 'this change needs to be skipped
  during the bisection, as the kernel sources at that stage contain an unrelated
  problem that causes building or booting to fail'.

* These steps will rule out if your problem is caused by some change in the
  build configuration between the 'working' and the 'broken' kernel. This for
  example can happen when your distributor enabled an additional security
  feature in the newer kernel which was disabled or not yet supported by the
  older kernel. That security feature might get into the way of something you
  do -- in which case your problem from the perspective of the Linux kernel
  upstream developers is not a regression, as
  Documentation/admin-guide/reporting-regressions.rst explains in more detail.
  You thus would waste your time if you'd try to bisect this.

* If the cause for your regression was already fixed in the latest mainline
  codebase, you'd perform the bisection for nothing. This holds true for a
  regression you encountered with a stable/longterm release as well, as they are
  often caused by problems in mainline changes that were backported -- in which
  case the problem will have to be fixed in mainline first. Maybe it already was
  fixed there and the fix is already in the process of being backported.

* For regressions within a stable/longterm series it's furthermore crucial to
  know if the issue is specific to that series or also happens in the mainline
  kernel, as the report needs to be sent to different people:

  * Regressions specific to a stable/longterm series are the stable team's
    responsibility; mainline Linux developers might or might not care.

  * Regressions also happening in mainline are something the regular Linux
    developers and maintainers have to handle; the stable team does not care
    and does not need to be involved in the report, they just should be told
    to backport the fix once it's ready.

  Your report might be ignored if you send it to the wrong party -- and even
  when you get a reply there is a decent chance that developers tell you to
  evaluate which of the two cases it is before they take a closer look.

[:ref:`back to step-by-step guide <introlatestcheck_bissbs>`]

.. _checkoutmaster_bisref:

Check out the latest Linux codebase
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Check out the latest Linux codebase.*
  [:ref:`... <checkoutmaster_bissbs>`]

In case you later want to recheck if an ever newer codebase might fix the
problem, remember to run that ``git fetch --shallow-exclude [...]`` command
again mentioned earlier to update your local Git repository.

[:ref:`back to step-by-step guide <checkoutmaster_bissbs>`]

.. _build_bisref:

Build your kernel
~~~~~~~~~~~~~~~~~

  *Build the image and the modules of your first kernel using the config file
  you prepared.* [:ref:`... <build_bissbs>`]

A lot can go wrong at this stage, but the instructions below will help you help
yourself. Another subsection explains how to directly package your kernel up as
deb, rpm or tar file.

Dealing with build errors
"""""""""""""""""""""""""

When a build error occurs, it might be caused by some aspect of your machine's
setup that often can be fixed quickly; other times though the problem lies in
the code and can only be fixed by a developer. A close examination of the
failure messages coupled with some research on the internet will often tell you
which of the two it is. To perform such investigation, restart the build
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

In the end, most issues you run into have likely been encountered and
reported by others already. That includes issues where the cause is not your
system, but lies in the code. If you run into one of those, you might thus find
a solution (e.g. a patch) or workaround for your issue, too.

Package your kernel up
""""""""""""""""""""""

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

[:ref:`back to step-by-step guide <build_bissbs>`]

.. _install_bisref:

Put the kernel in place
~~~~~~~~~~~~~~~~~~~~~~~

  *Install the kernel you just built.* [:ref:`... <install_bissbs>`]

What you need to do after executing the command in the step-by-step guide
depends on the existence and the implementation of ``/sbin/installkernel``
executable on your distribution.

If installkernel is found, the kernel's build system will delegate the actual
installation of your kernel image to this executable, which then performs some
or all of these tasks:

* On almost all Linux distributions installkernel will store your kernel's
  image in /boot/, usually as '/boot/vmlinuz-<kernelrelease_id>'; often it will
  put a 'System.map-<kernelrelease_id>' alongside it.

* On most distributions installkernel will then generate an 'initramfs'
  (sometimes also called 'initrd'), which usually are stored as
  '/boot/initramfs-<kernelrelease_id>.img' or
  '/boot/initrd-<kernelrelease_id>'. Commodity distributions rely on this file
  for booting, hence ensure to execute the make target 'modules_install' first,
  as your distribution's initramfs generator otherwise will be unable to find
  the modules that go into the image.

* On some distributions installkernel will then add an entry for your kernel
  to your bootloader's configuration.

You have to take care of some or all of the tasks yourself, if your
distribution lacks a installkernel script or does only handle part of them.
Consult the distribution's documentation for details. If in doubt, install the
kernel manually::

   sudo install -m 0600 $(make -s image_name) /boot/vmlinuz-$(make -s kernelrelease)
   sudo install -m 0600 System.map /boot/System.map-$(make -s kernelrelease)

Now generate your initramfs using the tools your distribution provides for this
process. Afterwards add your kernel to your bootloader configuration and reboot.

[:ref:`back to step-by-step guide <install_bissbs>`]

.. _storagespace_bisref:

Storage requirements per kernel
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Check how much storage space the kernel, its modules, and other related files
  like the initramfs consume.* [:ref:`... <storagespace_bissbs>`]

The kernels built during a bisection consume quite a bit of space in /boot/ and
/lib/modules/, especially if you enabled debug symbols. That makes it easy to
fill up volumes during a bisection -- and due to that even kernels which used to
work earlier might fail to boot. To prevent that you will need to know how much
space each installed kernel typically requires.

Note, most of the time the pattern '/boot/*$(make -s kernelrelease)*' used in
the guide will match all files needed to boot your kernel -- but neither the
path nor the naming scheme are mandatory. On some distributions you thus will
need to look in different places.

[:ref:`back to step-by-step guide <storagespace_bissbs>`]

.. _tainted_bisref:

Check if your newly built kernel considers itself 'tainted'
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Check if the kernel marked itself as 'tainted'.*
  [:ref:`... <tainted_bissbs>`]

Linux marks itself as tainted when something happens that potentially leads to
follow-up errors that look totally unrelated. That is why developers might
ignore or react scantly to reports from tainted kernels -- unless of course the
kernel set the flag right when the reported bug occurred.

That's why you want check why a kernel is tainted as explained in
Documentation/admin-guide/tainted-kernels.rst; doing so is also in your own
interest, as your testing might be flawed otherwise.

[:ref:`back to step-by-step guide <tainted_bissbs>`]

.. _recheckbroken_bisref:

Check the kernel built from a recent mainline codebase
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Verify if your bug occurs with the newly built kernel.*
  [:ref:`... <recheckbroken_bissbs>`]

There are a couple of reasons why your bug or regression might not show up with
the kernel you built from the latest codebase. These are the most frequent:

* The bug was fixed meanwhile.

* What you suspected to be a regression was caused by a change in the build
  configuration the provider of your kernel carried out.

* Your problem might be a race condition that does not show up with your kernel;
  the trimmed build configuration, a different setting for debug symbols, the
  compiler used, and various other things can cause this.

* In case you encountered the regression with a stable/longterm kernel it might
  be a problem that is specific to that series; the next step in this guide will
  check this.

[:ref:`back to step-by-step guide <recheckbroken_bissbs>`]

.. _recheckstablebroken_bisref:

Check the kernel built from the latest stable/longterm codebase
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Are you facing a regression within a stable/longterm release, but failed to
  reproduce it with the kernel you just built using the latest mainline sources?
  Then check if the latest codebase for the particular series might already fix
  the problem.* [:ref:`... <recheckstablebroken_bissbs>`]

If this kernel does not show the regression either, there most likely is no need
for a bisection.

[:ref:`back to step-by-step guide <recheckstablebroken_bissbs>`]

.. _introworkingcheck_bisref:

Ensure the 'good' version is really working well
------------------------------------------------

  *Check if the kernels you build work fine.*
  [:ref:`... <introworkingcheck_bissbs>`]

This section will reestablish a known working base. Skipping it might be
appealing, but is usually a bad idea, as it does something important:

It will ensure the .config file you prepared earlier actually works as expected.
That is in your own interest, as trimming the configuration is not foolproof --
and you might be building and testing ten or more kernels for nothing before
starting to suspect something might be wrong with the build configuration.

That alone is reason enough to spend the time on this, but not the only reason.

Many readers of this guide normally run kernels that are patched, use add-on
modules, or both. Those kernels thus are not considered 'vanilla' -- therefore
it's possible that the thing that regressed might never have worked in vanilla
builds of the 'good' version in the first place.

There is a third reason for those that noticed a regression between
stable/longterm kernels of different series (e.g. 6.0.13..6.1.5): it will
ensure the kernel version you assumed to be 'good' earlier in the process (e.g.
6.0) actually is working.

[:ref:`back to step-by-step guide <introworkingcheck_bissbs>`]

.. _recheckworking_bisref:

Build your own version of the 'good' kernel
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Build your own variant of the working kernel and check if the feature that
  regressed works as expected with it.* [:ref:`... <recheckworking_bissbs>`]

In case the feature that broke with newer kernels does not work with your first
self-built kernel, find and resolve the cause before moving on. There are a
multitude of reasons why this might happen. Some ideas where to look:

* Check the taint status and the output of ``dmesg``, maybe something unrelated
  went wrong.

* Maybe localmodconfig did something odd and disabled the module required to
  test the feature? Then you might want to recreate a .config file based on the
  one from the last working kernel and skip trimming it down; manually disabling
  some features in the .config might work as well to reduce the build time.

* Maybe it's not a kernel regression and something that is caused by some fluke,
  a broken initramfs (also known as initrd), new firmware files, or an updated
  userland software?

* Maybe it was a feature added to your distributor's kernel which vanilla Linux
  at that point never supported?

Note, if you found and fixed problems with the .config file, you want to use it
to build another kernel from the latest codebase, as your earlier tests with
mainline and the latest version from an affected stable/longterm series were
most likely flawed.

[:ref:`back to step-by-step guide <recheckworking_bissbs>`]

Perform a bisection and validate the result
-------------------------------------------

  *With all the preparations and precaution builds taken care of, you are now
  ready to begin the bisection.* [:ref:`... <introbisect_bissbs>`]

The steps in this segment perform and validate the bisection.

[:ref:`back to step-by-step guide <introbisect_bissbs>`].

.. _bisectstart_bisref:

Start the bisection
~~~~~~~~~~~~~~~~~~~

  *Start the bisection and tell Git about the versions earlier established as
  'good' and 'bad'.* [:ref:`... <bisectstart_bissbs>`]

This will start the bisection process; the last of the commands will make Git
check out a commit round about half-way between the 'good' and the 'bad' changes
for you to test.

[:ref:`back to step-by-step guide <bisectstart_bissbs>`]

.. _bisectbuild_bisref:

Build a kernel from the bisection point
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Build, install, and boot a kernel from the code Git checked out using the
  same commands you used earlier.* [:ref:`... <bisectbuild_bissbs>`]

There are two things worth of note here:

* Occasionally building the kernel will fail or it might not boot due some
  problem in the code at the bisection point. In that case run this command::

    git bisect skip

  Git will then check out another commit nearby which with a bit of luck should
  work better. Afterwards restart executing this step.

* Those slightly odd looking version identifiers can happen during bisections,
  because the Linux kernel subsystems prepare their changes for a new mainline
  release (say 6.2) before its predecessor (e.g. 6.1) is finished. They thus
  base them on a somewhat earlier point like 6.1-rc1 or even 6.0 -- and then
  get merged for 6.2 without rebasing nor squashing them once 6.1 is out. This
  leads to those slightly odd looking version identifiers coming up during
  bisections.

[:ref:`back to step-by-step guide <bisectbuild_bissbs>`]

.. _bisecttest_bisref:

Bisection checkpoint
~~~~~~~~~~~~~~~~~~~~

  *Check if the feature that regressed works in the kernel you just built.*
  [:ref:`... <bisecttest_bissbs>`]

Ensure what you tell Git is accurate: getting it wrong just one time will bring
the rest of the bisection totally off course, hence all testing after that point
will be for nothing.

[:ref:`back to step-by-step guide <bisecttest_bissbs>`]

.. _bisectlog_bisref:

Put the bisection log away
~~~~~~~~~~~~~~~~~~~~~~~~~~

  *Store Git's bisection log and the current .config file in a safe place.*
  [:ref:`... <bisectlog_bissbs>`]

As indicated above: declaring just one kernel wrongly as 'good' or 'bad' will
render the end result of a bisection useless. In that case you'd normally have
to restart the bisection from scratch. The log can prevent that, as it might
allow someone to point out where a bisection likely went sideways -- and then
instead of testing ten or more kernels you might only have to build a few to
resolve things.

The .config file is put aside, as there is a decent chance that developers might
ask for it after you report the regression.

[:ref:`back to step-by-step guide <bisectlog_bissbs>`]

.. _revert_bisref:

Try reverting the culprit
~~~~~~~~~~~~~~~~~~~~~~~~~

  *Try reverting the culprit on top of the latest codebase to see if this fixes
  your regression.* [:ref:`... <revert_bissbs>`]

This is an optional step, but whenever possible one you should try: there is a
decent chance that developers will ask you to perform this step when you bring
the bisection result up. So give it a try, you are in the flow already, building
one more kernel shouldn't be a big deal at this point.

The step-by-step guide covers everything relevant already except one slightly
rare thing: did you bisected a regression that also happened with mainline using
a stable/longterm series, but Git failed to revert the commit in mainline? Then
try to revert the culprit in the affected stable/longterm series -- and if that
succeeds, test that kernel version instead.

[:ref:`back to step-by-step guide <revert_bissbs>`]

Cleanup steps during and after following this guide
---------------------------------------------------

  *During and after following this guide you might want or need to remove some
  of the kernels you installed.* [:ref:`... <introclosure_bissbs>`]

The steps in this section describe clean-up procedures.

[:ref:`back to step-by-step guide <introclosure_bissbs>`].

.. _makeroom_bisref:

Cleaning up during the bisection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

  *To remove one of the kernels you installed, look up its 'kernelrelease'
  identifier.* [:ref:`... <makeroom_bissbs>`]

The kernels you install during this process are easy to remove later, as its
parts are only stored in two places and clearly identifiable. You thus do not
need to worry to mess up your machine when you install a kernel manually (and
thus bypass your distribution's packaging system): all parts of your kernels are
relatively easy to remove later.

One of the two places is a directory in /lib/modules/, which holds the modules
for each installed kernel. This directory is named after the kernel's release
identifier; hence, to remove all modules for one of the kernels you built,
simply remove its modules directory in /lib/modules/.

The other place is /boot/, where typically two up to five files will be placed
during installation of a kernel. All of them usually contain the release name in
their file name, but how many files and their exact names depend somewhat on
your distribution's installkernel executable and its initramfs generator. On
some distributions the ``kernel-install remove...`` command mentioned in the
step-by-step guide will delete all of these files for you while also removing
the menu entry for the kernel from your bootloader configuration. On others you
have to take care of these two tasks yourself. The following command should
interactively remove the three main files of a kernel with the release name
'6.0-rc1-local-gcafec0cacaca0'::

  rm -i /boot/{System.map,vmlinuz,initr}-6.0-rc1-local-gcafec0cacaca0

Afterwards check for other files in /boot/ that have
'6.0-rc1-local-gcafec0cacaca0' in their name and consider deleting them as well.
Now remove the boot entry for the kernel from your bootloader's configuration;
the steps to do that vary quite a bit between Linux distributions.

Note, be careful with wildcards like '*' when deleting files or directories
for kernels manually: you might accidentally remove files of a 6.0.13 kernel
when all you want is to remove 6.0 or 6.0.1.

[:ref:`back to step-by-step guide <makeroom_bissbs>`]

Cleaning up after the bisection
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

.. _finishingtouch_bisref:

  *Once you have finished the bisection, do not immediately remove anything
  you set up, as you might need a few things again.*
  [:ref:`... <finishingtouch_bissbs>`]

When you are really short of storage space removing the kernels as described in
the step-by-step guide might not free as much space as you would like. In that
case consider running ``rm -rf ~/linux/*`` as well now. This will remove the
build artifacts and the Linux sources, but will leave the Git repository
(~/linux/.git/) behind -- a simple ``git reset --hard`` thus will bring the
sources back.

Removing the repository as well would likely be unwise at this point: there
is a decent chance developers will ask you to build another kernel to
perform additional tests -- like testing a debug patch or a proposed fix.
Details on how to perform those can be found in the section :ref:`Optional
tasks: test reverts, patches, or later versions <introoptional_bissbs>`.

Additional tests are also the reason why you want to keep the
~/kernel-config-working file around for a few weeks.

[:ref:`back to step-by-step guide <finishingtouch_bissbs>`]

.. _introoptional_bisref:

Test reverts, patches, or later versions
----------------------------------------

  *While or after reporting a bug, you might want or potentially will be asked
  to test reverts, patches, proposed fixes, or other versions.*
  [:ref:`... <introoptional_bissbs>`]

All the commands used in this section should be pretty straight forward, so
there is not much to add except one thing: when setting a kernel tag as
instructed, ensure it is not much longer than the one used in the example, as
problems will arise if the kernelrelease identifier exceeds 63 characters.

[:ref:`back to step-by-step guide <introoptional_bissbs>`].


Additional information
======================

.. _buildhost_bis:

Build kernels on a different machine
------------------------------------

To compile kernels on another system, slightly alter the step-by-step guide's
instructions:

* Start following the guide on the machine where you want to install and test
  the kernels later.

* After executing ':ref:`Boot into the working kernel and briefly use the
  apparently broken feature <bootworking_bissbs>`', save the list of loaded
  modules to a file using ``lsmod > ~/test-machine-lsmod``. Then locate the
  build configuration for the running kernel (see ':ref:`Start defining the
  build configuration for your kernel <oldconfig_bisref>`' for hints on where
  to find it) and store it as '~/test-machine-config-working'. Transfer both
  files to the home directory of your build host.

* Continue the guide on the build host (e.g. with ':ref:`Ensure to have enough
  free space for building [...] <diskspace_bissbs>`').

* When you reach ':ref:`Start preparing a kernel build configuration[...]
  <oldconfig_bissbs>`': before running ``make olddefconfig`` for the first time,
  execute the following command to base your configuration on the one from the
  test machine's 'working' kernel::

    cp ~/test-machine-config-working ~/linux/.config

* During the next step to ':ref:`disable any apparently superfluous kernel
  modules <localmodconfig_bissbs>`' use the following command instead::

    yes '' | make localmodconfig LSMOD=~/lsmod_foo-machine localmodconfig

* Continue the guide, but ignore the instructions outlining how to compile,
  install, and reboot into a kernel every time they come up. Instead build
  like this::

    cp ~/kernel-config-working .config
    make olddefconfig &&
    make -j $(nproc --all) targz-pkg

  This will generate a gzipped tar file whose name is printed in the last
  line shown; for example, a kernel with the kernelrelease identifier
  '6.0.0-rc1-local-g928a87efa423' built for x86 machines usually will
  be stored as '~/linux/linux-6.0.0-rc1-local-g928a87efa423-x86.tar.gz'.

  Copy that file to your test machine's home directory.

* Switch to the test machine to check if you have enough space to hold another
  kernel. Then extract the file you transferred::

    sudo tar -xvzf ~/linux-6.0.0-rc1-local-g928a87efa423-x86.tar.gz -C /

  Afterwards :ref:`generate the initramfs and add the kernel to your boot
  loader's configuration <install_bisref>`; on some distributions the following
  command will take care of both these tasks::

    sudo /sbin/installkernel 6.0.0-rc1-local-g928a87efa423 /boot/vmlinuz-6.0.0-rc1-local-g928a87efa423

  Now reboot and ensure you started the intended kernel.

This approach even works when building for another architecture: just install
cross-compilers and add the appropriate parameters to every invocation of make
(e.g. ``make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- [...]``).

Additional reading material
---------------------------

* The `man page for 'git bisect' <https://git-scm.com/docs/git-bisect>`_ and
  `fighting regressions with 'git bisect' <https://git-scm.com/docs/git-bisect-lk2009.html>`_
  in the Git documentation.
* `Working with git bisect <https://nathanchance.dev/posts/working-with-git-bisect/>`_
  from kernel developer Nathan Chancellor.
* `Using Git bisect to figure out when brokenness was introduced <http://webchick.net/node/99>`_.
* `Fully automated bisecting with 'git bisect run' <https://lwn.net/Articles/317154>`_.

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
   https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/plain/Documentation/admin-guide/verify-bugs-and-bisect-regressions.rst

..
   Note: Only the content of this RST file as found in the Linux kernel sources
   is available under CC-BY-4.0, as versions of this text that were processed
   (for example by the kernel's build system) might contain content taken from
   files which use a more restrictive license.
