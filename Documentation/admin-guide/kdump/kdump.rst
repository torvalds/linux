================================================================
Documentation for Kdump - The kexec-based Crash Dumping Solution
================================================================

This document includes overview, setup, installation, and analysis
information.

Overview
========

Kdump uses kexec to quickly boot to a dump-capture kernel whenever a
dump of the system kernel's memory needs to be taken (for example, when
the system panics). The system kernel's memory image is preserved across
the reboot and is accessible to the dump-capture kernel.

You can use common commands, such as cp, scp or makedumpfile to copy
the memory image to a dump file on the local disk, or across the network
to a remote system.

Kdump and kexec are currently supported on the x86, x86_64, ppc64,
s390x, arm and arm64 architectures.

When the system kernel boots, it reserves a small section of memory for
the dump-capture kernel. This ensures that ongoing Direct Memory Access
(DMA) from the system kernel does not corrupt the dump-capture kernel.
The kexec -p command loads the dump-capture kernel into this reserved
memory.

On x86 machines, the first 640 KB of physical memory is needed for boot,
regardless of where the kernel loads. For simpler handling, the whole
low 1M is reserved to avoid any later kernel or device driver writing
data into this area. Like this, the low 1M can be reused as system RAM
by kdump kernel without extra handling.

On PPC64 machines first 32KB of physical memory is needed for booting
regardless of where the kernel is loaded and to support 64K page size
kexec backs up the first 64KB memory.

For s390x, when kdump is triggered, the crashkernel region is exchanged
with the region [0, crashkernel region size] and then the kdump kernel
runs in [0, crashkernel region size]. Therefore no relocatable kernel is
needed for s390x.

All of the necessary information about the system kernel's core image is
encoded in the ELF format, and stored in a reserved area of memory
before a crash. The physical address of the start of the ELF header is
passed to the dump-capture kernel through the elfcorehdr= boot
parameter. Optionally the size of the ELF header can also be passed
when using the elfcorehdr=[size[KMG]@]offset[KMG] syntax.

With the dump-capture kernel, you can access the memory image through
/proc/vmcore. This exports the dump as an ELF-format file that you can
write out using file copy commands such as cp or scp. You can also use
makedumpfile utility to analyze and write out filtered contents with
options, e.g with '-d 31' it will only write out kernel data. Further,
you can use analysis tools such as the GNU Debugger (GDB) and the Crash
tool to debug the dump file. This method ensures that the dump pages are
correctly ordered.

Setup and Installation
======================

Install kexec-tools
-------------------

1) Login as the root user.

2) Download the kexec-tools user-space package from the following URL:

http://kernel.org/pub/linux/utils/kernel/kexec/kexec-tools.tar.gz

This is a symlink to the latest version.

The latest kexec-tools git tree is available at:

- git://git.kernel.org/pub/scm/utils/kernel/kexec/kexec-tools.git
- http://www.kernel.org/pub/scm/utils/kernel/kexec/kexec-tools.git

There is also a gitweb interface available at
http://www.kernel.org/git/?p=utils/kernel/kexec/kexec-tools.git

More information about kexec-tools can be found at
http://horms.net/projects/kexec/

3) Unpack the tarball with the tar command, as follows::

	tar xvpzf kexec-tools.tar.gz

4) Change to the kexec-tools directory, as follows::

	cd kexec-tools-VERSION

5) Configure the package, as follows::

	./configure

6) Compile the package, as follows::

	make

7) Install the package, as follows::

	make install


Build the system and dump-capture kernels
-----------------------------------------
There are two possible methods of using Kdump.

1) Build a separate custom dump-capture kernel for capturing the
   kernel core dump.

2) Or use the system kernel binary itself as dump-capture kernel and there is
   no need to build a separate dump-capture kernel. This is possible
   only with the architectures which support a relocatable kernel. As
   of today, i386, x86_64, ppc64, arm and arm64 architectures support
   relocatable kernel.

Building a relocatable kernel is advantageous from the point of view that
one does not have to build a second kernel for capturing the dump. But
at the same time one might want to build a custom dump capture kernel
suitable to his needs.

Following are the configuration setting required for system and
dump-capture kernels for enabling kdump support.

System kernel config options
----------------------------

1) Enable "kexec system call" or "kexec file based system call" in
   "Processor type and features."::

	CONFIG_KEXEC=y or CONFIG_KEXEC_FILE=y

   And both of them will select KEXEC_CORE::

	CONFIG_KEXEC_CORE=y

2) Enable "sysfs file system support" in "Filesystem" -> "Pseudo
   filesystems." This is usually enabled by default::

	CONFIG_SYSFS=y

   Note that "sysfs file system support" might not appear in the "Pseudo
   filesystems" menu if "Configure standard kernel features (expert users)"
   is not enabled in "General Setup." In this case, check the .config file
   itself to ensure that sysfs is turned on, as follows::

	grep 'CONFIG_SYSFS' .config

3) Enable "Compile the kernel with debug info" in "Kernel hacking."::

	CONFIG_DEBUG_INFO=Y

   This causes the kernel to be built with debug symbols. The dump
   analysis tools require a vmlinux with debug symbols in order to read
   and analyze a dump file.

Dump-capture kernel config options (Arch Independent)
-----------------------------------------------------

1) Enable "kernel crash dumps" support under "Processor type and
   features"::

	CONFIG_CRASH_DUMP=y

   And this will select VMCORE_INFO and CRASH_RESERVE::
	CONFIG_VMCORE_INFO=y
	CONFIG_CRASH_RESERVE=y

2) Enable "/proc/vmcore support" under "Filesystems" -> "Pseudo filesystems"::

	CONFIG_PROC_VMCORE=y

   (CONFIG_PROC_VMCORE is set by default when CONFIG_CRASH_DUMP is selected.)

Dump-capture kernel config options (Arch Dependent, i386 and x86_64)
--------------------------------------------------------------------

1) On i386, enable high memory support under "Processor type and
   features"::

	CONFIG_HIGHMEM4G

2) With CONFIG_SMP=y, usually nr_cpus=1 need specified on the kernel
   command line when loading the dump-capture kernel because one
   CPU is enough for kdump kernel to dump vmcore on most of systems.

   However, you can also specify nr_cpus=X to enable multiple processors
   in kdump kernel.

   With CONFIG_SMP=n, the above things are not related.

3) A relocatable kernel is suggested to be built by default. If not yet,
   enable "Build a relocatable kernel" support under "Processor type and
   features"::

	CONFIG_RELOCATABLE=y

4) Use a suitable value for "Physical address where the kernel is
   loaded" (under "Processor type and features"). This only appears when
   "kernel crash dumps" is enabled. A suitable value depends upon
   whether kernel is relocatable or not.

   If you are using a relocatable kernel use CONFIG_PHYSICAL_START=0x100000
   This will compile the kernel for physical address 1MB, but given the fact
   kernel is relocatable, it can be run from any physical address hence
   kexec boot loader will load it in memory region reserved for dump-capture
   kernel.

   Otherwise it should be the start of memory region reserved for
   second kernel using boot parameter "crashkernel=Y@X". Here X is
   start of memory region reserved for dump-capture kernel.
   Generally X is 16MB (0x1000000). So you can set
   CONFIG_PHYSICAL_START=0x1000000

5) Make and install the kernel and its modules. DO NOT add this kernel
   to the boot loader configuration files.

Dump-capture kernel config options (Arch Dependent, ppc64)
----------------------------------------------------------

1) Enable "Build a kdump crash kernel" support under "Kernel" options::

	CONFIG_CRASH_DUMP=y

2)   Enable "Build a relocatable kernel" support::

	CONFIG_RELOCATABLE=y

   Make and install the kernel and its modules.

Dump-capture kernel config options (Arch Dependent, arm)
----------------------------------------------------------

-   To use a relocatable kernel,
    Enable "AUTO_ZRELADDR" support under "Boot" options::

	AUTO_ZRELADDR=y

Dump-capture kernel config options (Arch Dependent, arm64)
----------------------------------------------------------

- Please note that kvm of the dump-capture kernel will not be enabled
  on non-VHE systems even if it is configured. This is because the CPU
  will not be reset to EL2 on panic.

crashkernel syntax
===========================
1) crashkernel=size@offset

   Here 'size' specifies how much memory to reserve for the dump-capture kernel
   and 'offset' specifies the beginning of this reserved memory. For example,
   "crashkernel=64M@16M" tells the system kernel to reserve 64 MB of memory
   starting at physical address 0x01000000 (16MB) for the dump-capture kernel.

   The crashkernel region can be automatically placed by the system
   kernel at run time. This is done by specifying the base address as 0,
   or omitting it all together::

         crashkernel=256M@0

   or::

         crashkernel=256M

   If the start address is specified, note that the start address of the
   kernel will be aligned to a value (which is Arch dependent), so if the
   start address is not then any space below the alignment point will be
   wasted.

2) range1:size1[,range2:size2,...][@offset]

   While the "crashkernel=size[@offset]" syntax is sufficient for most
   configurations, sometimes it's handy to have the reserved memory dependent
   on the value of System RAM -- that's mostly for distributors that pre-setup
   the kernel command line to avoid a unbootable system after some memory has
   been removed from the machine.

   The syntax is::

       crashkernel=<range1>:<size1>[,<range2>:<size2>,...][@offset]
       range=start-[end]

   For example::

       crashkernel=512M-2G:64M,2G-:128M

   This would mean:

       1) if the RAM is smaller than 512M, then don't reserve anything
          (this is the "rescue" case)
       2) if the RAM size is between 512M and 2G (exclusive), then reserve 64M
       3) if the RAM size is larger than 2G, then reserve 128M

3) crashkernel=size,high and crashkernel=size,low

   If memory above 4G is preferred, crashkernel=size,high can be used to
   fulfill that. With it, physical memory is allowed to be allocated from top,
   so could be above 4G if system has more than 4G RAM installed. Otherwise,
   memory region will be allocated below 4G if available.

   When crashkernel=X,high is passed, kernel could allocate physical memory
   region above 4G, low memory under 4G is needed in this case. There are
   three ways to get low memory:

      1) Kernel will allocate at least 256M memory below 4G automatically
         if crashkernel=Y,low is not specified.
      2) Let user specify low memory size instead.
      3) Specified value 0 will disable low memory allocation::

            crashkernel=0,low

Boot into System Kernel
-----------------------
1) Update the boot loader (such as grub, yaboot, or lilo) configuration
   files as necessary.

2) Boot the system kernel with the boot parameter "crashkernel=Y@X".

   On x86 and x86_64, use "crashkernel=Y[@X]". Most of the time, the
   start address 'X' is not necessary, kernel will search a suitable
   area. Unless an explicit start address is expected.

   On ppc64, use "crashkernel=128M@32M".

   On s390x, typically use "crashkernel=xxM". The value of xx is dependent
   on the memory consumption of the kdump system. In general this is not
   dependent on the memory size of the production system.

   On arm, the use of "crashkernel=Y@X" is no longer necessary; the
   kernel will automatically locate the crash kernel image within the
   first 512MB of RAM if X is not given.

   On arm64, use "crashkernel=Y[@X]".  Note that the start address of
   the kernel, X if explicitly specified, must be aligned to 2MiB (0x200000).

Load the Dump-capture Kernel
============================

After booting to the system kernel, dump-capture kernel needs to be
loaded.

Based on the architecture and type of image (relocatable or not), one
can choose to load the uncompressed vmlinux or compressed bzImage/vmlinuz
of dump-capture kernel. Following is the summary.

For i386 and x86_64:

	- Use bzImage/vmlinuz if kernel is relocatable.
	- Use vmlinux if kernel is not relocatable.

For ppc64:

	- Use vmlinux

For s390x:

	- Use image or bzImage

For arm:

	- Use zImage

For arm64:

	- Use vmlinux or Image

If you are using an uncompressed vmlinux image then use following command
to load dump-capture kernel::

   kexec -p <dump-capture-kernel-vmlinux-image> \
   --initrd=<initrd-for-dump-capture-kernel> --args-linux \
   --append="root=<root-dev> <arch-specific-options>"

If you are using a compressed bzImage/vmlinuz, then use following command
to load dump-capture kernel::

   kexec -p <dump-capture-kernel-bzImage> \
   --initrd=<initrd-for-dump-capture-kernel> \
   --append="root=<root-dev> <arch-specific-options>"

If you are using a compressed zImage, then use following command
to load dump-capture kernel::

   kexec --type zImage -p <dump-capture-kernel-bzImage> \
   --initrd=<initrd-for-dump-capture-kernel> \
   --dtb=<dtb-for-dump-capture-kernel> \
   --append="root=<root-dev> <arch-specific-options>"

If you are using an uncompressed Image, then use following command
to load dump-capture kernel::

   kexec -p <dump-capture-kernel-Image> \
   --initrd=<initrd-for-dump-capture-kernel> \
   --append="root=<root-dev> <arch-specific-options>"

Following are the arch specific command line options to be used while
loading dump-capture kernel.

For i386 and x86_64:

	"1 irqpoll nr_cpus=1 reset_devices"

For ppc64:

	"1 maxcpus=1 noirqdistrib reset_devices"

For s390x:

	"1 nr_cpus=1 cgroup_disable=memory"

For arm:

	"1 maxcpus=1 reset_devices"

For arm64:

	"1 nr_cpus=1 reset_devices"

Notes on loading the dump-capture kernel:

* By default, the ELF headers are stored in ELF64 format to support
  systems with more than 4GB memory. On i386, kexec automatically checks if
  the physical RAM size exceeds the 4 GB limit and if not, uses ELF32.
  So, on non-PAE systems, ELF32 is always used.

  The --elf32-core-headers option can be used to force the generation of ELF32
  headers. This is necessary because GDB currently cannot open vmcore files
  with ELF64 headers on 32-bit systems.

* The "irqpoll" boot parameter reduces driver initialization failures
  due to shared interrupts in the dump-capture kernel.

* You must specify <root-dev> in the format corresponding to the root
  device name in the output of mount command.

* Boot parameter "1" boots the dump-capture kernel into single-user
  mode without networking. If you want networking, use "3".

* We generally don't have to bring up a SMP kernel just to capture the
  dump. Hence generally it is useful either to build a UP dump-capture
  kernel or specify maxcpus=1 option while loading dump-capture kernel.
  Note, though maxcpus always works, you had better replace it with
  nr_cpus to save memory if supported by the current ARCH, such as x86.

* You should enable multi-cpu support in dump-capture kernel if you intend
  to use multi-thread programs with it, such as parallel dump feature of
  makedumpfile. Otherwise, the multi-thread program may have a great
  performance degradation. To enable multi-cpu support, you should bring up an
  SMP dump-capture kernel and specify maxcpus/nr_cpus options while loading it.

* For s390x there are two kdump modes: If a ELF header is specified with
  the elfcorehdr= kernel parameter, it is used by the kdump kernel as it
  is done on all other architectures. If no elfcorehdr= kernel parameter is
  specified, the s390x kdump kernel dynamically creates the header. The
  second mode has the advantage that for CPU and memory hotplug, kdump has
  not to be reloaded with kexec_load().

* For s390x systems with many attached devices the "cio_ignore" kernel
  parameter should be used for the kdump kernel in order to prevent allocation
  of kernel memory for devices that are not relevant for kdump. The same
  applies to systems that use SCSI/FCP devices. In that case the
  "allow_lun_scan" zfcp module parameter should be set to zero before
  setting FCP devices online.

Kernel Panic
============

After successfully loading the dump-capture kernel as previously
described, the system will reboot into the dump-capture kernel if a
system crash is triggered.  Trigger points are located in panic(),
die(), die_nmi() and in the sysrq handler (ALT-SysRq-c).

The following conditions will execute a crash trigger point:

If a hard lockup is detected and "NMI watchdog" is configured, the system
will boot into the dump-capture kernel ( die_nmi() ).

If die() is called, and it happens to be a thread with pid 0 or 1, or die()
is called inside interrupt context or die() is called and panic_on_oops is set,
the system will boot into the dump-capture kernel.

On powerpc systems when a soft-reset is generated, die() is called by all cpus
and the system will boot into the dump-capture kernel.

For testing purposes, you can trigger a crash by using "ALT-SysRq-c",
"echo c > /proc/sysrq-trigger" or write a module to force the panic.

Write Out the Dump File
=======================

After the dump-capture kernel is booted, write out the dump file with
the following command::

   cp /proc/vmcore <dump-file>

or use scp to write out the dump file between hosts on a network, e.g::

   scp /proc/vmcore remote_username@remote_ip:<dump-file>

You can also use makedumpfile utility to write out the dump file
with specified options to filter out unwanted contents, e.g::

   makedumpfile -l --message-level 1 -d 31 /proc/vmcore <dump-file>

Analysis
========

Before analyzing the dump image, you should reboot into a stable kernel.

You can do limited analysis using GDB on the dump file copied out of
/proc/vmcore. Use the debug vmlinux built with -g and run the following
command::

   gdb vmlinux <dump-file>

Stack trace for the task on processor 0, register display, and memory
display work fine.

Note: GDB cannot analyze core files generated in ELF64 format for x86.
On systems with a maximum of 4GB of memory, you can generate
ELF32-format headers using the --elf32-core-headers kernel option on the
dump kernel.

You can also use the Crash utility to analyze dump files in Kdump
format. Crash is available at the following URL:

   https://github.com/crash-utility/crash

Crash document can be found at:
   https://crash-utility.github.io/

Trigger Kdump on WARN()
=======================

The kernel parameter, panic_on_warn, calls panic() in all WARN() paths.  This
will cause a kdump to occur at the panic() call.  In cases where a user wants
to specify this during runtime, /proc/sys/kernel/panic_on_warn can be set to 1
to achieve the same behaviour.

Trigger Kdump on add_taint()
============================

The kernel parameter panic_on_taint facilitates a conditional call to panic()
from within add_taint() whenever the value set in this bitmask matches with the
bit flag being set by add_taint().
This will cause a kdump to occur at the add_taint()->panic() call.

Contact
=======

- kexec@lists.infradead.org

GDB macros
==========

.. include:: gdbmacros.txt
   :literal:
