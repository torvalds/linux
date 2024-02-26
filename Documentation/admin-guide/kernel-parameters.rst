.. _kernelparameters:

The kernel's command-line parameters
====================================

The following is a consolidated list of the kernel parameters as implemented
by the __setup(), early_param(), core_param() and module_param() macros
and sorted into English Dictionary order (defined as ignoring all
punctuation and sorting digits before letters in a case insensitive
manner), and with descriptions where known.

The kernel parses parameters from the kernel command line up to "``--``";
if it doesn't recognize a parameter and it doesn't contain a '.', the
parameter gets passed to init: parameters with '=' go into init's
environment, others are passed as command line arguments to init.
Everything after "``--``" is passed as an argument to init.

Module parameters can be specified in two ways: via the kernel command
line with a module name prefix, or via modprobe, e.g.::

	(kernel command line) usbcore.blinkenlights=1
	(modprobe command line) modprobe usbcore blinkenlights=1

Parameters for modules which are built into the kernel need to be
specified on the kernel command line.  modprobe looks through the
kernel command line (/proc/cmdline) and collects module parameters
when it loads a module, so the kernel command line can be used for
loadable modules too.

Hyphens (dashes) and underscores are equivalent in parameter names, so::

	log_buf_len=1M print-fatal-signals=1

can also be entered as::

	log-buf-len=1M print_fatal_signals=1

Double-quotes can be used to protect spaces in values, e.g.::

	param="spaces in here"

cpu lists:
----------

Some kernel parameters take a list of CPUs as a value, e.g.  isolcpus,
nohz_full, irqaffinity, rcu_nocbs.  The format of this list is:

	<cpu number>,...,<cpu number>

or

	<cpu number>-<cpu number>
	(must be a positive range in ascending order)

or a mixture

<cpu number>,...,<cpu number>-<cpu number>

Note that for the special case of a range one can split the range into equal
sized groups and for each group use some amount from the beginning of that
group:

	<cpu number>-<cpu number>:<used size>/<group size>

For example one can add to the command line following parameter:

	isolcpus=1,2,10-20,100-2000:2/25

where the final item represents CPUs 100,101,125,126,150,151,...

The value "N" can be used to represent the numerically last CPU on the system,
i.e "foo_cpus=16-N" would be equivalent to "16-31" on a 32 core system.

Keep in mind that "N" is dynamic, so if system changes cause the bitmap width
to change, such as less cores in the CPU list, then N and any ranges using N
will also change.  Use the same on a small 4 core system, and "16-N" becomes
"16-3" and now the same boot input will be flagged as invalid (start > end).

The special case-tolerant group name "all" has a meaning of selecting all CPUs,
so that "nohz_full=all" is the equivalent of "nohz_full=0-N".

The semantics of "N" and "all" is supported on a level of bitmaps and holds for
all users of bitmap_parselist().

This document may not be entirely up to date and comprehensive. The command
"modinfo -p ${modulename}" shows a current list of all parameters of a loadable
module. Loadable modules, after being loaded into the running kernel, also
reveal their parameters in /sys/module/${modulename}/parameters/. Some of these
parameters may be changed at runtime by the command
``echo -n ${value} > /sys/module/${modulename}/parameters/${parm}``.

The parameters listed below are only valid if certain kernel build options
were enabled and if respective hardware is present. This list should be kept
in alphabetical order. The text in square brackets at the beginning
of each description states the restrictions within which a parameter
is applicable::

	ACPI	ACPI support is enabled.
	AGP	AGP (Accelerated Graphics Port) is enabled.
	ALSA	ALSA sound support is enabled.
	APIC	APIC support is enabled.
	APM	Advanced Power Management support is enabled.
	APPARMOR AppArmor support is enabled.
	ARM	ARM architecture is enabled.
	ARM64	ARM64 architecture is enabled.
	AX25	Appropriate AX.25 support is enabled.
	CLK	Common clock infrastructure is enabled.
	CMA	Contiguous Memory Area support is enabled.
	DRM	Direct Rendering Management support is enabled.
	DYNAMIC_DEBUG Build in debug messages and enable them at runtime
	EDD	BIOS Enhanced Disk Drive Services (EDD) is enabled
	EFI	EFI Partitioning (GPT) is enabled
	EVM	Extended Verification Module
	FB	The frame buffer device is enabled.
	FTRACE	Function tracing enabled.
	GCOV	GCOV profiling is enabled.
	HIBERNATION HIBERNATION is enabled.
	HW	Appropriate hardware is enabled.
	HYPER_V HYPERV support is enabled.
	IA-64	IA-64 architecture is enabled.
	IMA     Integrity measurement architecture is enabled.
	IP_PNP	IP DHCP, BOOTP, or RARP is enabled.
	IPV6	IPv6 support is enabled.
	ISAPNP	ISA PnP code is enabled.
	ISDN	Appropriate ISDN support is enabled.
	ISOL	CPU Isolation is enabled.
	JOY	Appropriate joystick support is enabled.
	KGDB	Kernel debugger support is enabled.
	KVM	Kernel Virtual Machine support is enabled.
	LIBATA  Libata driver is enabled
	LOONGARCH LoongArch architecture is enabled.
	LOOP	Loopback device support is enabled.
	LP	Printer support is enabled.
	M68k	M68k architecture is enabled.
			These options have more detailed description inside of
			Documentation/arch/m68k/kernel-options.rst.
	MDA	MDA console support is enabled.
	MIPS	MIPS architecture is enabled.
	MOUSE	Appropriate mouse support is enabled.
	MSI	Message Signaled Interrupts (PCI).
	MTD	MTD (Memory Technology Device) support is enabled.
	NET	Appropriate network support is enabled.
	NFS	Appropriate NFS support is enabled.
	NUMA	NUMA support is enabled.
	OF	Devicetree is enabled.
	PARISC	The PA-RISC architecture is enabled.
	PCI	PCI bus support is enabled.
	PCIE	PCI Express support is enabled.
	PCMCIA	The PCMCIA subsystem is enabled.
	PNP	Plug & Play support is enabled.
	PPC	PowerPC architecture is enabled.
	PPT	Parallel port support is enabled.
	PS2	Appropriate PS/2 support is enabled.
	PV_OPS	A paravirtualized kernel is enabled.
	RAM	RAM disk support is enabled.
	RDT	Intel Resource Director Technology.
	RISCV	RISCV architecture is enabled.
	S390	S390 architecture is enabled.
	SCSI	Appropriate SCSI support is enabled.
			A lot of drivers have their options described inside
			the Documentation/scsi/ sub-directory.
	SECURITY Different security models are enabled.
	SELINUX SELinux support is enabled.
	SERIAL	Serial support is enabled.
	SH	SuperH architecture is enabled.
	SMP	The kernel is an SMP kernel.
	SPARC	Sparc architecture is enabled.
	SUSPEND	System suspend states are enabled.
	SWSUSP	Software suspend (hibernation) is enabled.
	TPM	TPM drivers are enabled.
	UMS	USB Mass Storage support is enabled.
	USB	USB support is enabled.
	USBHID	USB Human Interface Device support is enabled.
	V4L	Video For Linux support is enabled.
	VGA	The VGA console has been enabled.
	VMMIO   Driver for memory mapped virtio devices is enabled.
	VT	Virtual terminal support is enabled.
	WDT	Watchdog support is enabled.
	X86-32	X86-32, aka i386 architecture is enabled.
	X86-64	X86-64 architecture is enabled.
			More X86-64 boot options can be found in
			Documentation/arch/x86/x86_64/boot-options.rst.
	X86	Either 32-bit or 64-bit x86 (same as X86-32+X86-64)
	X86_UV	SGI UV support is enabled.
	XEN	Xen support is enabled
	XTENSA	xtensa architecture is enabled.

In addition, the following text indicates that the option::

	BOOT	Is a boot loader parameter.
	BUGS=	Relates to possible processor bugs on the said processor.
	KNL	Is a kernel start-up parameter.

Parameters denoted with BOOT are actually interpreted by the boot
loader, and have no meaning to the kernel directly.
Do not modify the syntax of boot loader parameters without extreme
need or coordination with <Documentation/arch/x86/boot.rst>.

There are also arch-specific kernel-parameters not documented here.
See for example <Documentation/arch/x86/x86_64/boot-options.rst>.

Note that ALL kernel parameters listed below are CASE SENSITIVE, and that
a trailing = on the name of any parameter states that that parameter will
be entered as an environment variable, whereas its absence indicates that
it will appear as a kernel argument readable via /proc/cmdline by programs
running once the system is up.

The number of kernel parameters is not limited, but the length of the
complete command line (parameters including spaces etc.) is limited to
a fixed number of characters. This limit depends on the architecture
and is between 256 and 4096 characters. It is defined in the file
./include/uapi/asm-generic/setup.h as COMMAND_LINE_SIZE.

Finally, the [KMG] suffix is commonly described after a number of kernel
parameter values. These 'K', 'M', and 'G' letters represent the _binary_
multipliers 'Kilo', 'Mega', and 'Giga', equaling 2^10, 2^20, and 2^30
bytes respectively. Such letter suffixes can also be entirely omitted:

.. include:: kernel-parameters.txt
   :literal:
