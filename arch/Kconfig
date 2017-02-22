#
# General architecture dependent options
#

config KEXEC_CORE
	bool

config HAVE_IMA_KEXEC
	bool

config OPROFILE
	tristate "OProfile system profiling"
	depends on PROFILING
	depends on HAVE_OPROFILE
	select RING_BUFFER
	select RING_BUFFER_ALLOW_SWAP
	help
	  OProfile is a profiling system capable of profiling the
	  whole system, include the kernel, kernel modules, libraries,
	  and applications.

	  If unsure, say N.

config OPROFILE_EVENT_MULTIPLEX
	bool "OProfile multiplexing support (EXPERIMENTAL)"
	default n
	depends on OPROFILE && X86
	help
	  The number of hardware counters is limited. The multiplexing
	  feature enables OProfile to gather more events than counters
	  are provided by the hardware. This is realized by switching
	  between events at an user specified time interval.

	  If unsure, say N.

config HAVE_OPROFILE
	bool

config OPROFILE_NMI_TIMER
	def_bool y
	depends on PERF_EVENTS && HAVE_PERF_EVENTS_NMI && !PPC64

config KPROBES
	bool "Kprobes"
	depends on MODULES
	depends on HAVE_KPROBES
	select KALLSYMS
	help
	  Kprobes allows you to trap at almost any kernel address and
	  execute a callback function.  register_kprobe() establishes
	  a probepoint and specifies the callback.  Kprobes is useful
	  for kernel debugging, non-intrusive instrumentation and testing.
	  If in doubt, say "N".

config JUMP_LABEL
       bool "Optimize very unlikely/likely branches"
       depends on HAVE_ARCH_JUMP_LABEL
       help
         This option enables a transparent branch optimization that
	 makes certain almost-always-true or almost-always-false branch
	 conditions even cheaper to execute within the kernel.

	 Certain performance-sensitive kernel code, such as trace points,
	 scheduler functionality, networking code and KVM have such
	 branches and include support for this optimization technique.

         If it is detected that the compiler has support for "asm goto",
	 the kernel will compile such branches with just a nop
	 instruction. When the condition flag is toggled to true, the
	 nop will be converted to a jump instruction to execute the
	 conditional block of instructions.

	 This technique lowers overhead and stress on the branch prediction
	 of the processor and generally makes the kernel faster. The update
	 of the condition is slower, but those are always very rare.

	 ( On 32-bit x86, the necessary options added to the compiler
	   flags may increase the size of the kernel slightly. )

config STATIC_KEYS_SELFTEST
	bool "Static key selftest"
	depends on JUMP_LABEL
	help
	  Boot time self-test of the branch patching code.

config OPTPROBES
	def_bool y
	depends on KPROBES && HAVE_OPTPROBES
	depends on !PREEMPT

config KPROBES_ON_FTRACE
	def_bool y
	depends on KPROBES && HAVE_KPROBES_ON_FTRACE
	depends on DYNAMIC_FTRACE_WITH_REGS
	help
	 If function tracer is enabled and the arch supports full
	 passing of pt_regs to function tracing, then kprobes can
	 optimize on top of function tracing.

config UPROBES
	def_bool n
	depends on ARCH_SUPPORTS_UPROBES
	help
	  Uprobes is the user-space counterpart to kprobes: they
	  enable instrumentation applications (such as 'perf probe')
	  to establish unintrusive probes in user-space binaries and
	  libraries, by executing handler functions when the probes
	  are hit by user-space applications.

	  ( These probes come in the form of single-byte breakpoints,
	    managed by the kernel and kept transparent to the probed
	    application. )

config HAVE_64BIT_ALIGNED_ACCESS
	def_bool 64BIT && !HAVE_EFFICIENT_UNALIGNED_ACCESS
	help
	  Some architectures require 64 bit accesses to be 64 bit
	  aligned, which also requires structs containing 64 bit values
	  to be 64 bit aligned too. This includes some 32 bit
	  architectures which can do 64 bit accesses, as well as 64 bit
	  architectures without unaligned access.

	  This symbol should be selected by an architecture if 64 bit
	  accesses are required to be 64 bit aligned in this way even
	  though it is not a 64 bit architecture.

	  See Documentation/unaligned-memory-access.txt for more
	  information on the topic of unaligned memory accesses.

config HAVE_EFFICIENT_UNALIGNED_ACCESS
	bool
	help
	  Some architectures are unable to perform unaligned accesses
	  without the use of get_unaligned/put_unaligned. Others are
	  unable to perform such accesses efficiently (e.g. trap on
	  unaligned access and require fixing it up in the exception
	  handler.)

	  This symbol should be selected by an architecture if it can
	  perform unaligned accesses efficiently to allow different
	  code paths to be selected for these cases. Some network
	  drivers, for example, could opt to not fix up alignment
	  problems with received packets if doing so would not help
	  much.

	  See Documentation/unaligned-memory-access.txt for more
	  information on the topic of unaligned memory accesses.

config ARCH_USE_BUILTIN_BSWAP
       bool
       help
	 Modern versions of GCC (since 4.4) have builtin functions
	 for handling byte-swapping. Using these, instead of the old
	 inline assembler that the architecture code provides in the
	 __arch_bswapXX() macros, allows the compiler to see what's
	 happening and offers more opportunity for optimisation. In
	 particular, the compiler will be able to combine the byteswap
	 with a nearby load or store and use load-and-swap or
	 store-and-swap instructions if the architecture has them. It
	 should almost *never* result in code which is worse than the
	 hand-coded assembler in <asm/swab.h>.  But just in case it
	 does, the use of the builtins is optional.

	 Any architecture with load-and-swap or store-and-swap
	 instructions should set this. And it shouldn't hurt to set it
	 on architectures that don't have such instructions.

config KRETPROBES
	def_bool y
	depends on KPROBES && HAVE_KRETPROBES

config USER_RETURN_NOTIFIER
	bool
	depends on HAVE_USER_RETURN_NOTIFIER
	help
	  Provide a kernel-internal notification when a cpu is about to
	  switch to user mode.

config HAVE_IOREMAP_PROT
	bool

config HAVE_KPROBES
	bool

config HAVE_KRETPROBES
	bool

config HAVE_OPTPROBES
	bool

config HAVE_KPROBES_ON_FTRACE
	bool

config HAVE_NMI
	bool

config HAVE_NMI_WATCHDOG
	depends on HAVE_NMI
	bool
#
# An arch should select this if it provides all these things:
#
#	task_pt_regs()		in asm/processor.h or asm/ptrace.h
#	arch_has_single_step()	if there is hardware single-step support
#	arch_has_block_step()	if there is hardware block-step support
#	asm/syscall.h		supplying asm-generic/syscall.h interface
#	linux/regset.h		user_regset interfaces
#	CORE_DUMP_USE_REGSET	#define'd in linux/elf.h
#	TIF_SYSCALL_TRACE	calls tracehook_report_syscall_{entry,exit}
#	TIF_NOTIFY_RESUME	calls tracehook_notify_resume()
#	signal delivery		calls tracehook_signal_handler()
#
config HAVE_ARCH_TRACEHOOK
	bool

config HAVE_DMA_CONTIGUOUS
	bool

config GENERIC_SMP_IDLE_THREAD
       bool

config GENERIC_IDLE_POLL_SETUP
       bool

# Select if arch init_task initializer is different to init/init_task.c
config ARCH_INIT_TASK
       bool

# Select if arch has its private alloc_task_struct() function
config ARCH_TASK_STRUCT_ALLOCATOR
	bool

# Select if arch has its private alloc_thread_stack() function
config ARCH_THREAD_STACK_ALLOCATOR
	bool

# Select if arch wants to size task_struct dynamically via arch_task_struct_size:
config ARCH_WANTS_DYNAMIC_TASK_STRUCT
	bool

config HAVE_REGS_AND_STACK_ACCESS_API
	bool
	help
	  This symbol should be selected by an architecure if it supports
	  the API needed to access registers and stack entries from pt_regs,
	  declared in asm/ptrace.h
	  For example the kprobes-based event tracer needs this API.

config HAVE_CLK
	bool
	help
	  The <linux/clk.h> calls support software clock gating and
	  thus are a key power management tool on many systems.

config HAVE_DMA_API_DEBUG
	bool

config HAVE_HW_BREAKPOINT
	bool
	depends on PERF_EVENTS

config HAVE_MIXED_BREAKPOINTS_REGS
	bool
	depends on HAVE_HW_BREAKPOINT
	help
	  Depending on the arch implementation of hardware breakpoints,
	  some of them have separate registers for data and instruction
	  breakpoints addresses, others have mixed registers to store
	  them but define the access type in a control register.
	  Select this option if your arch implements breakpoints under the
	  latter fashion.

config HAVE_USER_RETURN_NOTIFIER
	bool

config HAVE_PERF_EVENTS_NMI
	bool
	help
	  System hardware can generate an NMI using the perf event
	  subsystem.  Also has support for calculating CPU cycle events
	  to determine how many clock cycles in a given period.

config HAVE_PERF_REGS
	bool
	help
	  Support selective register dumps for perf events. This includes
	  bit-mapping of each registers and a unique architecture id.

config HAVE_PERF_USER_STACK_DUMP
	bool
	help
	  Support user stack dumps for perf event samples. This needs
	  access to the user stack pointer which is not unified across
	  architectures.

config HAVE_ARCH_JUMP_LABEL
	bool

config HAVE_RCU_TABLE_FREE
	bool

config ARCH_HAVE_NMI_SAFE_CMPXCHG
	bool

config HAVE_ALIGNED_STRUCT_PAGE
	bool
	help
	  This makes sure that struct pages are double word aligned and that
	  e.g. the SLUB allocator can perform double word atomic operations
	  on a struct page for better performance. However selecting this
	  might increase the size of a struct page by a word.

config HAVE_CMPXCHG_LOCAL
	bool

config HAVE_CMPXCHG_DOUBLE
	bool

config ARCH_WANT_IPC_PARSE_VERSION
	bool

config ARCH_WANT_COMPAT_IPC_PARSE_VERSION
	bool

config ARCH_WANT_OLD_COMPAT_IPC
	select ARCH_WANT_COMPAT_IPC_PARSE_VERSION
	bool

config HAVE_ARCH_SECCOMP_FILTER
	bool
	help
	  An arch should select this symbol if it provides all of these things:
	  - syscall_get_arch()
	  - syscall_get_arguments()
	  - syscall_rollback()
	  - syscall_set_return_value()
	  - SIGSYS siginfo_t support
	  - secure_computing is called from a ptrace_event()-safe context
	  - secure_computing return value is checked and a return value of -1
	    results in the system call being skipped immediately.
	  - seccomp syscall wired up

config SECCOMP_FILTER
	def_bool y
	depends on HAVE_ARCH_SECCOMP_FILTER && SECCOMP && NET
	help
	  Enable tasks to build secure computing environments defined
	  in terms of Berkeley Packet Filter programs which implement
	  task-defined system call filtering polices.

	  See Documentation/prctl/seccomp_filter.txt for details.

config HAVE_GCC_PLUGINS
	bool
	help
	  An arch should select this symbol if it supports building with
	  GCC plugins.

menuconfig GCC_PLUGINS
	bool "GCC plugins"
	depends on HAVE_GCC_PLUGINS
	depends on !COMPILE_TEST
	help
	  GCC plugins are loadable modules that provide extra features to the
	  compiler. They are useful for runtime instrumentation and static analysis.

	  See Documentation/gcc-plugins.txt for details.

config GCC_PLUGIN_CYC_COMPLEXITY
	bool "Compute the cyclomatic complexity of a function" if EXPERT
	depends on GCC_PLUGINS
	depends on !COMPILE_TEST
	help
	  The complexity M of a function's control flow graph is defined as:
	   M = E - N + 2P
	  where

	  E = the number of edges
	  N = the number of nodes
	  P = the number of connected components (exit nodes).

	  Enabling this plugin reports the complexity to stderr during the
	  build. It mainly serves as a simple example of how to create a
	  gcc plugin for the kernel.

config GCC_PLUGIN_SANCOV
	bool
	depends on GCC_PLUGINS
	help
	  This plugin inserts a __sanitizer_cov_trace_pc() call at the start of
	  basic blocks. It supports all gcc versions with plugin support (from
	  gcc-4.5 on). It is based on the commit "Add fuzzing coverage support"
	  by Dmitry Vyukov <dvyukov@google.com>.

config GCC_PLUGIN_LATENT_ENTROPY
	bool "Generate some entropy during boot and runtime"
	depends on GCC_PLUGINS
	help
	  By saying Y here the kernel will instrument some kernel code to
	  extract some entropy from both original and artificially created
	  program state.  This will help especially embedded systems where
	  there is little 'natural' source of entropy normally.  The cost
	  is some slowdown of the boot process (about 0.5%) and fork and
	  irq processing.

	  Note that entropy extracted this way is not cryptographically
	  secure!

	  This plugin was ported from grsecurity/PaX. More information at:
	   * https://grsecurity.net/
	   * https://pax.grsecurity.net/

config HAVE_CC_STACKPROTECTOR
	bool
	help
	  An arch should select this symbol if:
	  - its compiler supports the -fstack-protector option
	  - it has implemented a stack canary (e.g. __stack_chk_guard)

config CC_STACKPROTECTOR
	def_bool n
	help
	  Set when a stack-protector mode is enabled, so that the build
	  can enable kernel-side support for the GCC feature.

choice
	prompt "Stack Protector buffer overflow detection"
	depends on HAVE_CC_STACKPROTECTOR
	default CC_STACKPROTECTOR_NONE
	help
	  This option turns on the "stack-protector" GCC feature. This
	  feature puts, at the beginning of functions, a canary value on
	  the stack just before the return address, and validates
	  the value just before actually returning.  Stack based buffer
	  overflows (that need to overwrite this return address) now also
	  overwrite the canary, which gets detected and the attack is then
	  neutralized via a kernel panic.

config CC_STACKPROTECTOR_NONE
	bool "None"
	help
	  Disable "stack-protector" GCC feature.

config CC_STACKPROTECTOR_REGULAR
	bool "Regular"
	select CC_STACKPROTECTOR
	help
	  Functions will have the stack-protector canary logic added if they
	  have an 8-byte or larger character array on the stack.

	  This feature requires gcc version 4.2 or above, or a distribution
	  gcc with the feature backported ("-fstack-protector").

	  On an x86 "defconfig" build, this feature adds canary checks to
	  about 3% of all kernel functions, which increases kernel code size
	  by about 0.3%.

config CC_STACKPROTECTOR_STRONG
	bool "Strong"
	select CC_STACKPROTECTOR
	help
	  Functions will have the stack-protector canary logic added in any
	  of the following conditions:

	  - local variable's address used as part of the right hand side of an
	    assignment or function argument
	  - local variable is an array (or union containing an array),
	    regardless of array type or length
	  - uses register local variables

	  This feature requires gcc version 4.9 or above, or a distribution
	  gcc with the feature backported ("-fstack-protector-strong").

	  On an x86 "defconfig" build, this feature adds canary checks to
	  about 20% of all kernel functions, which increases the kernel code
	  size by about 2%.

endchoice

config THIN_ARCHIVES
	bool
	help
	  Select this if the architecture wants to use thin archives
	  instead of ld -r to create the built-in.o files.

config LD_DEAD_CODE_DATA_ELIMINATION
	bool
	help
	  Select this if the architecture wants to do dead code and
	  data elimination with the linker by compiling with
	  -ffunction-sections -fdata-sections and linking with
	  --gc-sections.

	  This requires that the arch annotates or otherwise protects
	  its external entry points from being discarded. Linker scripts
	  must also merge .text.*, .data.*, and .bss.* correctly into
	  output sections. Care must be taken not to pull in unrelated
	  sections (e.g., '.text.init'). Typically '.' in section names
	  is used to distinguish them from label names / C identifiers.

config HAVE_ARCH_WITHIN_STACK_FRAMES
	bool
	help
	  An architecture should select this if it can walk the kernel stack
	  frames to determine if an object is part of either the arguments
	  or local variables (i.e. that it excludes saved return addresses,
	  and similar) by implementing an inline arch_within_stack_frames(),
	  which is used by CONFIG_HARDENED_USERCOPY.

config HAVE_CONTEXT_TRACKING
	bool
	help
	  Provide kernel/user boundaries probes necessary for subsystems
	  that need it, such as userspace RCU extended quiescent state.
	  Syscalls need to be wrapped inside user_exit()-user_enter() through
	  the slow path using TIF_NOHZ flag. Exceptions handlers must be
	  wrapped as well. Irqs are already protected inside
	  rcu_irq_enter/rcu_irq_exit() but preemption or signal handling on
	  irq exit still need to be protected.

config HAVE_VIRT_CPU_ACCOUNTING
	bool

config ARCH_HAS_SCALED_CPUTIME
	bool

config HAVE_VIRT_CPU_ACCOUNTING_GEN
	bool
	default y if 64BIT
	help
	  With VIRT_CPU_ACCOUNTING_GEN, cputime_t becomes 64-bit.
	  Before enabling this option, arch code must be audited
	  to ensure there are no races in concurrent read/write of
	  cputime_t. For example, reading/writing 64-bit cputime_t on
	  some 32-bit arches may require multiple accesses, so proper
	  locking is needed to protect against concurrent accesses.


config HAVE_IRQ_TIME_ACCOUNTING
	bool
	help
	  Archs need to ensure they use a high enough resolution clock to
	  support irq time accounting and then call enable_sched_clock_irqtime().

config HAVE_ARCH_TRANSPARENT_HUGEPAGE
	bool

config HAVE_ARCH_HUGE_VMAP
	bool

config HAVE_ARCH_SOFT_DIRTY
	bool

config HAVE_MOD_ARCH_SPECIFIC
	bool
	help
	  The arch uses struct mod_arch_specific to store data.  Many arches
	  just need a simple module loader without arch specific data - those
	  should not enable this.

config MODULES_USE_ELF_RELA
	bool
	help
	  Modules only use ELF RELA relocations.  Modules with ELF REL
	  relocations will give an error.

config MODULES_USE_ELF_REL
	bool
	help
	  Modules only use ELF REL relocations.  Modules with ELF RELA
	  relocations will give an error.

config HAVE_UNDERSCORE_SYMBOL_PREFIX
	bool
	help
	  Some architectures generate an _ in front of C symbols; things like
	  module loading and assembly files need to know about this.

config HAVE_IRQ_EXIT_ON_IRQ_STACK
	bool
	help
	  Architecture doesn't only execute the irq handler on the irq stack
	  but also irq_exit(). This way we can process softirqs on this irq
	  stack instead of switching to a new one when we call __do_softirq()
	  in the end of an hardirq.
	  This spares a stack switch and improves cache usage on softirq
	  processing.

config PGTABLE_LEVELS
	int
	default 2

config ARCH_HAS_ELF_RANDOMIZE
	bool
	help
	  An architecture supports choosing randomized locations for
	  stack, mmap, brk, and ET_DYN. Defined functions:
	  - arch_mmap_rnd()
	  - arch_randomize_brk()

config HAVE_ARCH_MMAP_RND_BITS
	bool
	help
	  An arch should select this symbol if it supports setting a variable
	  number of bits for use in establishing the base address for mmap
	  allocations, has MMU enabled and provides values for both:
	  - ARCH_MMAP_RND_BITS_MIN
	  - ARCH_MMAP_RND_BITS_MAX

config HAVE_EXIT_THREAD
	bool
	help
	  An architecture implements exit_thread.

config ARCH_MMAP_RND_BITS_MIN
	int

config ARCH_MMAP_RND_BITS_MAX
	int

config ARCH_MMAP_RND_BITS_DEFAULT
	int

config ARCH_MMAP_RND_BITS
	int "Number of bits to use for ASLR of mmap base address" if EXPERT
	range ARCH_MMAP_RND_BITS_MIN ARCH_MMAP_RND_BITS_MAX
	default ARCH_MMAP_RND_BITS_DEFAULT if ARCH_MMAP_RND_BITS_DEFAULT
	default ARCH_MMAP_RND_BITS_MIN
	depends on HAVE_ARCH_MMAP_RND_BITS
	help
	  This value can be used to select the number of bits to use to
	  determine the random offset to the base address of vma regions
	  resulting from mmap allocations. This value will be bounded
	  by the architecture's minimum and maximum supported values.

	  This value can be changed after boot using the
	  /proc/sys/vm/mmap_rnd_bits tunable

config HAVE_ARCH_MMAP_RND_COMPAT_BITS
	bool
	help
	  An arch should select this symbol if it supports running applications
	  in compatibility mode, supports setting a variable number of bits for
	  use in establishing the base address for mmap allocations, has MMU
	  enabled and provides values for both:
	  - ARCH_MMAP_RND_COMPAT_BITS_MIN
	  - ARCH_MMAP_RND_COMPAT_BITS_MAX

config ARCH_MMAP_RND_COMPAT_BITS_MIN
	int

config ARCH_MMAP_RND_COMPAT_BITS_MAX
	int

config ARCH_MMAP_RND_COMPAT_BITS_DEFAULT
	int

config ARCH_MMAP_RND_COMPAT_BITS
	int "Number of bits to use for ASLR of mmap base address for compatible applications" if EXPERT
	range ARCH_MMAP_RND_COMPAT_BITS_MIN ARCH_MMAP_RND_COMPAT_BITS_MAX
	default ARCH_MMAP_RND_COMPAT_BITS_DEFAULT if ARCH_MMAP_RND_COMPAT_BITS_DEFAULT
	default ARCH_MMAP_RND_COMPAT_BITS_MIN
	depends on HAVE_ARCH_MMAP_RND_COMPAT_BITS
	help
	  This value can be used to select the number of bits to use to
	  determine the random offset to the base address of vma regions
	  resulting from mmap allocations for compatible applications This
	  value will be bounded by the architecture's minimum and maximum
	  supported values.

	  This value can be changed after boot using the
	  /proc/sys/vm/mmap_rnd_compat_bits tunable

config HAVE_COPY_THREAD_TLS
	bool
	help
	  Architecture provides copy_thread_tls to accept tls argument via
	  normal C parameter passing, rather than extracting the syscall
	  argument from pt_regs.

config HAVE_STACK_VALIDATION
	bool
	help
	  Architecture supports the 'objtool check' host tool command, which
	  performs compile-time stack metadata validation.

config HAVE_ARCH_HASH
	bool
	default n
	help
	  If this is set, the architecture provides an <asm/hash.h>
	  file which provides platform-specific implementations of some
	  functions in <linux/hash.h> or fs/namei.c.

config ISA_BUS_API
	def_bool ISA

#
# ABI hall of shame
#
config CLONE_BACKWARDS
	bool
	help
	  Architecture has tls passed as the 4th argument of clone(2),
	  not the 5th one.

config CLONE_BACKWARDS2
	bool
	help
	  Architecture has the first two arguments of clone(2) swapped.

config CLONE_BACKWARDS3
	bool
	help
	  Architecture has tls passed as the 3rd argument of clone(2),
	  not the 5th one.

config ODD_RT_SIGACTION
	bool
	help
	  Architecture has unusual rt_sigaction(2) arguments

config OLD_SIGSUSPEND
	bool
	help
	  Architecture has old sigsuspend(2) syscall, of one-argument variety

config OLD_SIGSUSPEND3
	bool
	help
	  Even weirder antique ABI - three-argument sigsuspend(2)

config OLD_SIGACTION
	bool
	help
	  Architecture has old sigaction(2) syscall.  Nope, not the same
	  as OLD_SIGSUSPEND | OLD_SIGSUSPEND3 - alpha has sigsuspend(2),
	  but fairly different variant of sigaction(2), thanks to OSF/1
	  compatibility...

config COMPAT_OLD_SIGACTION
	bool

config ARCH_NO_COHERENT_DMA_MMAP
	bool

config CPU_NO_EFFICIENT_FFS
	def_bool n

config HAVE_ARCH_VMAP_STACK
	def_bool n
	help
	  An arch should select this symbol if it can support kernel stacks
	  in vmalloc space.  This means:

	  - vmalloc space must be large enough to hold many kernel stacks.
	    This may rule out many 32-bit architectures.

	  - Stacks in vmalloc space need to work reliably.  For example, if
	    vmap page tables are created on demand, either this mechanism
	    needs to work while the stack points to a virtual address with
	    unpopulated page tables or arch code (switch_to() and switch_mm(),
	    most likely) needs to ensure that the stack's page table entries
	    are populated before running on a possibly unpopulated stack.

	  - If the stack overflows into a guard page, something reasonable
	    should happen.  The definition of "reasonable" is flexible, but
	    instantly rebooting without logging anything would be unfriendly.

config VMAP_STACK
	default y
	bool "Use a virtually-mapped stack"
	depends on HAVE_ARCH_VMAP_STACK && !KASAN
	---help---
	  Enable this if you want the use virtually-mapped kernel stacks
	  with guard pages.  This causes kernel stack overflows to be
	  caught immediately rather than causing difficult-to-diagnose
	  corruption.

	  This is presently incompatible with KASAN because KASAN expects
	  the stack to map directly to the KASAN shadow map using a formula
	  that is incorrect if the stack is in vmalloc space.

source "kernel/gcov/Kconfig"
