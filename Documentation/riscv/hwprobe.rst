.. SPDX-License-Identifier: GPL-2.0

RISC-V Hardware Probing Interface
---------------------------------

The RISC-V hardware probing interface is based around a single syscall, which
is defined in <asm/hwprobe.h>::

    struct riscv_hwprobe {
        __s64 key;
        __u64 value;
    };

    long sys_riscv_hwprobe(struct riscv_hwprobe *pairs, size_t pair_count,
                           size_t cpu_count, cpu_set_t *cpus,
                           unsigned int flags);

The arguments are split into three groups: an array of key-value pairs, a CPU
set, and some flags. The key-value pairs are supplied with a count. Userspace
must prepopulate the key field for each element, and the kernel will fill in the
value if the key is recognized. If a key is unknown to the kernel, its key field
will be cleared to -1, and its value set to 0. The CPU set is defined by
CPU_SET(3). For value-like keys (eg. vendor/arch/impl), the returned value will
be only be valid if all CPUs in the given set have the same value. Otherwise -1
will be returned. For boolean-like keys, the value returned will be a logical
AND of the values for the specified CPUs. Usermode can supply NULL for cpus and
0 for cpu_count as a shortcut for all online CPUs. There are currently no flags,
this value must be zero for future compatibility.

On success 0 is returned, on failure a negative error code is returned.

The following keys are defined:

* :c:macro:`RISCV_HWPROBE_KEY_MVENDORID`: Contains the value of ``mvendorid``,
  as defined by the RISC-V privileged architecture specification.

* :c:macro:`RISCV_HWPROBE_KEY_MARCHID`: Contains the value of ``marchid``, as
  defined by the RISC-V privileged architecture specification.

* :c:macro:`RISCV_HWPROBE_KEY_MIMPLID`: Contains the value of ``mimplid``, as
  defined by the RISC-V privileged architecture specification.

* :c:macro:`RISCV_HWPROBE_KEY_BASE_BEHAVIOR`: A bitmask containing the base
  user-visible behavior that this kernel supports.  The following base user ABIs
  are defined:

  * :c:macro:`RISCV_HWPROBE_BASE_BEHAVIOR_IMA`: Support for rv32ima or
    rv64ima, as defined by version 2.2 of the user ISA and version 1.10 of the
    privileged ISA, with the following known exceptions (more exceptions may be
    added, but only if it can be demonstrated that the user ABI is not broken):

    * The ``fence.i`` instruction cannot be directly executed by userspace
      programs (it may still be executed in userspace via a
      kernel-controlled mechanism such as the vDSO).

* :c:macro:`RISCV_HWPROBE_KEY_IMA_EXT_0`: A bitmask containing the extensions
  that are compatible with the :c:macro:`RISCV_HWPROBE_BASE_BEHAVIOR_IMA`:
  base system behavior.

  * :c:macro:`RISCV_HWPROBE_IMA_FD`: The F and D extensions are supported, as
    defined by commit cd20cee ("FMIN/FMAX now implement
    minimumNumber/maximumNumber, not minNum/maxNum") of the RISC-V ISA manual.

  * :c:macro:`RISCV_HWPROBE_IMA_C`: The C extension is supported, as defined
    by version 2.2 of the RISC-V ISA manual.

  * :c:macro:`RISCV_HWPROBE_IMA_V`: The V extension is supported, as defined by
    version 1.0 of the RISC-V Vector extension manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZBA`: The Zba address generation extension is
       supported, as defined in version 1.0 of the Bit-Manipulation ISA
       extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZBB`: The Zbb extension is supported, as defined
       in version 1.0 of the Bit-Manipulation ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZBS`: The Zbs extension is supported, as defined
       in version 1.0 of the Bit-Manipulation ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZICBOZ`: The Zicboz extension is supported, as
       ratified in commit 3dd606f ("Create cmobase-v1.0.pdf") of riscv-CMOs.

* :c:macro:`RISCV_HWPROBE_KEY_CPUPERF_0`: A bitmask that contains performance
  information about the selected set of processors.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_UNKNOWN`: The performance of misaligned
    accesses is unknown.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_EMULATED`: Misaligned accesses are
    emulated via software, either in or below the kernel.  These accesses are
    always extremely slow.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_SLOW`: Misaligned accesses are slower
    than equivalent byte accesses.  Misaligned accesses may be supported
    directly in hardware, or trapped and emulated by software.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_FAST`: Misaligned accesses are faster
    than equivalent byte accesses.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_UNSUPPORTED`: Misaligned accesses are
    not supported at all and will generate a misaligned address fault.

* :c:macro:`RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE`: An unsigned int which
  represents the size of the Zicboz block in bytes.
