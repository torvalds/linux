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
