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
                           size_t cpusetsize, cpu_set_t *cpus,
                           unsigned int flags);

The arguments are split into three groups: an array of key-value pairs, a CPU
set, and some flags. The key-value pairs are supplied with a count. Userspace
must prepopulate the key field for each element, and the kernel will fill in the
value if the key is recognized. If a key is unknown to the kernel, its key field
will be cleared to -1, and its value set to 0. The CPU set is defined by
CPU_SET(3) with size ``cpusetsize`` bytes. For value-like keys (eg. vendor,
arch, impl), the returned value will only be valid if all CPUs in the given set
have the same value. Otherwise -1 will be returned. For boolean-like keys, the
value returned will be a logical AND of the values for the specified CPUs.
Usermode can supply NULL for ``cpus`` and 0 for ``cpusetsize`` as a shortcut for
all online CPUs. The currently supported flags are:

* :c:macro:`RISCV_HWPROBE_WHICH_CPUS`: This flag basically reverses the behavior
  of sys_riscv_hwprobe().  Instead of populating the values of keys for a given
  set of CPUs, the values of each key are given and the set of CPUs is reduced
  by sys_riscv_hwprobe() to only those which match each of the key-value pairs.
  How matching is done depends on the key type.  For value-like keys, matching
  means to be the exact same as the value.  For boolean-like keys, matching
  means the result of a logical AND of the pair's value with the CPU's value is
  exactly the same as the pair's value.  Additionally, when ``cpus`` is an empty
  set, then it is initialized to all online CPUs which fit within it, i.e. the
  CPU set returned is the reduction of all the online CPUs which can be
  represented with a CPU set of size ``cpusetsize``.

All other flags are reserved for future compatibility and must be zero.

On success 0 is returned, on failure a negative error code is returned.

The following keys are defined:

* :c:macro:`RISCV_HWPROBE_KEY_MVENDORID`: Contains the value of ``mvendorid``,
  as defined by the RISC-V privileged architecture specification.

* :c:macro:`RISCV_HWPROBE_KEY_MARCHID`: Contains the value of ``marchid``, as
  defined by the RISC-V privileged architecture specification.

* :c:macro:`RISCV_HWPROBE_KEY_MIMPID`: Contains the value of ``mimpid``, as
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

  * :c:macro:`RISCV_HWPROBE_EXT_ZBC` The Zbc extension is supported, as defined
       in version 1.0 of the Bit-Manipulation ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZBKB` The Zbkb extension is supported, as
       defined in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZBKC` The Zbkc extension is supported, as
       defined in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZBKX` The Zbkx extension is supported, as
       defined in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZKND` The Zknd extension is supported, as
       defined in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZKNE` The Zkne extension is supported, as
       defined in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZKNH` The Zknh extension is supported, as
       defined in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZKSED` The Zksed extension is supported, as
       defined in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZKSH` The Zksh extension is supported, as
       defined in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZKT` The Zkt extension is supported, as defined
       in version 1.0 of the Scalar Crypto ISA extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVBB`: The Zvbb extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVBC`: The Zvbc extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVKB`: The Zvkb extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVKG`: The Zvkg extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVKNED`: The Zvkned extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVKNHA`: The Zvknha extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVKNHB`: The Zvknhb extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVKSED`: The Zvksed extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVKSH`: The Zvksh extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVKT`: The Zvkt extension is supported as
       defined in version 1.0 of the RISC-V Cryptography Extensions Volume II.

  * :c:macro:`RISCV_HWPROBE_EXT_ZFH`: The Zfh extension version 1.0 is supported
       as defined in the RISC-V ISA manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZFHMIN`: The Zfhmin extension version 1.0 is
       supported as defined in the RISC-V ISA manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZIHINTNTL`: The Zihintntl extension version 1.0
       is supported as defined in the RISC-V ISA manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVFH`: The Zvfh extension is supported as
       defined in the RISC-V Vector manual starting from commit e2ccd0548d6c
       ("Remove draft warnings from Zvfh[min]").

  * :c:macro:`RISCV_HWPROBE_EXT_ZVFHMIN`: The Zvfhmin extension is supported as
       defined in the RISC-V Vector manual starting from commit e2ccd0548d6c
       ("Remove draft warnings from Zvfh[min]").

  * :c:macro:`RISCV_HWPROBE_EXT_ZFA`: The Zfa extension is supported as
       defined in the RISC-V ISA manual starting from commit 056b6ff467c7
       ("Zfa is ratified").

  * :c:macro:`RISCV_HWPROBE_EXT_ZTSO`: The Ztso extension is supported as
       defined in the RISC-V ISA manual starting from commit 5618fb5a216b
       ("Ztso is now ratified.")

  * :c:macro:`RISCV_HWPROBE_EXT_ZACAS`: The Zacas extension is supported as
       defined in the Atomic Compare-and-Swap (CAS) instructions manual starting
       from commit 5059e0ca641c ("update to ratified").

  * :c:macro:`RISCV_HWPROBE_EXT_ZICNTR`: The Zicntr extension version 2.0
       is supported as defined in the RISC-V ISA manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZICOND`: The Zicond extension is supported as
       defined in the RISC-V Integer Conditional (Zicond) operations extension
       manual starting from commit 95cf1f9 ("Add changes requested by Ved
       during signoff")

  * :c:macro:`RISCV_HWPROBE_EXT_ZIHINTPAUSE`: The Zihintpause extension is
       supported as defined in the RISC-V ISA manual starting from commit
       d8ab5c78c207 ("Zihintpause is ratified").

  * :c:macro:`RISCV_HWPROBE_EXT_ZIHPM`: The Zihpm extension version 2.0
       is supported as defined in the RISC-V ISA manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVE32X`: The Vector sub-extension Zve32x is
    supported, as defined by version 1.0 of the RISC-V Vector extension manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVE32F`: The Vector sub-extension Zve32f is
    supported, as defined by version 1.0 of the RISC-V Vector extension manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVE64X`: The Vector sub-extension Zve64x is
    supported, as defined by version 1.0 of the RISC-V Vector extension manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVE64F`: The Vector sub-extension Zve64f is
    supported, as defined by version 1.0 of the RISC-V Vector extension manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZVE64D`: The Vector sub-extension Zve64d is
    supported, as defined by version 1.0 of the RISC-V Vector extension manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZIMOP`: The Zimop May-Be-Operations extension is
       supported as defined in the RISC-V ISA manual starting from commit
       58220614a5f ("Zimop is ratified/1.0").

  * :c:macro:`RISCV_HWPROBE_EXT_ZCA`: The Zca extension part of Zc* standard
       extensions for code size reduction, as ratified in commit 8be3419c1c0
       ("Zcf doesn't exist on RV64 as it contains no instructions") of
       riscv-code-size-reduction.

  * :c:macro:`RISCV_HWPROBE_EXT_ZCB`: The Zcb extension part of Zc* standard
       extensions for code size reduction, as ratified in commit 8be3419c1c0
       ("Zcf doesn't exist on RV64 as it contains no instructions") of
       riscv-code-size-reduction.

  * :c:macro:`RISCV_HWPROBE_EXT_ZCD`: The Zcd extension part of Zc* standard
       extensions for code size reduction, as ratified in commit 8be3419c1c0
       ("Zcf doesn't exist on RV64 as it contains no instructions") of
       riscv-code-size-reduction.

  * :c:macro:`RISCV_HWPROBE_EXT_ZCF`: The Zcf extension part of Zc* standard
       extensions for code size reduction, as ratified in commit 8be3419c1c0
       ("Zcf doesn't exist on RV64 as it contains no instructions") of
       riscv-code-size-reduction.

  * :c:macro:`RISCV_HWPROBE_EXT_ZCMOP`: The Zcmop May-Be-Operations extension is
       supported as defined in the RISC-V ISA manual starting from commit
       c732a4f39a4 ("Zcmop is ratified/1.0").

  * :c:macro:`RISCV_HWPROBE_EXT_ZAWRS`: The Zawrs extension is supported as
       ratified in commit 98918c844281 ("Merge pull request #1217 from
       riscv/zawrs") of riscv-isa-manual.

  * :c:macro:`RISCV_HWPROBE_EXT_ZAAMO`: The Zaamo extension is supported as
       defined in the in the RISC-V ISA manual starting from commit e87412e621f1
       ("integrate Zaamo and Zalrsc text (#1304)").

  * :c:macro:`RISCV_HWPROBE_EXT_ZALRSC`: The Zalrsc extension is supported as
       defined in the in the RISC-V ISA manual starting from commit e87412e621f1
       ("integrate Zaamo and Zalrsc text (#1304)").

  * :c:macro:`RISCV_HWPROBE_EXT_SUPM`: The Supm extension is supported as
       defined in version 1.0 of the RISC-V Pointer Masking extensions.

  * :c:macro:`RISCV_HWPROBE_EXT_ZFBFMIN`: The Zfbfmin extension is supported as
       defined in the RISC-V ISA manual starting from commit 4dc23d6229de
       ("Added Chapter title to BF16").

  * :c:macro:`RISCV_HWPROBE_EXT_ZVFBFMIN`: The Zvfbfmin extension is supported as
       defined in the RISC-V ISA manual starting from commit 4dc23d6229de
       ("Added Chapter title to BF16").

  * :c:macro:`RISCV_HWPROBE_EXT_ZVFBFWMA`: The Zvfbfwma extension is supported as
       defined in the RISC-V ISA manual starting from commit 4dc23d6229de
       ("Added Chapter title to BF16").

  * :c:macro:`RISCV_HWPROBE_EXT_ZICBOM`: The Zicbom extension is supported, as
       ratified in commit 3dd606f ("Create cmobase-v1.0.pdf") of riscv-CMOs.

  * :c:macro:`RISCV_HWPROBE_EXT_ZABHA`: The Zabha extension is supported as
       ratified in commit 49f49c842ff9 ("Update to Rafified state") of
       riscv-zabha.

* :c:macro:`RISCV_HWPROBE_KEY_CPUPERF_0`: Deprecated.  Returns similar values to
     :c:macro:`RISCV_HWPROBE_KEY_MISALIGNED_SCALAR_PERF`, but the key was
     mistakenly classified as a bitmask rather than a value.

* :c:macro:`RISCV_HWPROBE_KEY_MISALIGNED_SCALAR_PERF`: An enum value describing
  the performance of misaligned scalar native word accesses on the selected set
  of processors.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_SCALAR_UNKNOWN`: The performance of
    misaligned scalar accesses is unknown.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_SCALAR_EMULATED`: Misaligned scalar
    accesses are emulated via software, either in or below the kernel.  These
    accesses are always extremely slow.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_SCALAR_SLOW`: Misaligned scalar native
    word sized accesses are slower than the equivalent quantity of byte
    accesses. Misaligned accesses may be supported directly in hardware, or
    trapped and emulated by software.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_SCALAR_FAST`: Misaligned scalar native
    word sized accesses are faster than the equivalent quantity of byte
    accesses.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_SCALAR_UNSUPPORTED`: Misaligned scalar
    accesses are not supported at all and will generate a misaligned address
    fault.

* :c:macro:`RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE`: An unsigned int which
  represents the size of the Zicboz block in bytes.

* :c:macro:`RISCV_HWPROBE_KEY_HIGHEST_VIRT_ADDRESS`: An unsigned long which
  represent the highest userspace virtual address usable.

* :c:macro:`RISCV_HWPROBE_KEY_TIME_CSR_FREQ`: Frequency (in Hz) of `time CSR`.

* :c:macro:`RISCV_HWPROBE_KEY_MISALIGNED_VECTOR_PERF`: An enum value describing the
     performance of misaligned vector accesses on the selected set of processors.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_VECTOR_UNKNOWN`: The performance of misaligned
    vector accesses is unknown.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_VECTOR_SLOW`: 32-bit misaligned accesses using vector
    registers are slower than the equivalent quantity of byte accesses via vector registers.
    Misaligned accesses may be supported directly in hardware, or trapped and emulated by software.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_VECTOR_FAST`: 32-bit misaligned accesses using vector
    registers are faster than the equivalent quantity of byte accesses via vector registers.

  * :c:macro:`RISCV_HWPROBE_MISALIGNED_VECTOR_UNSUPPORTED`: Misaligned vector accesses are
    not supported at all and will generate a misaligned address fault.

* :c:macro:`RISCV_HWPROBE_KEY_VENDOR_EXT_MIPS_0`: A bitmask containing the
  mips vendor extensions that are compatible with the
  :c:macro:`RISCV_HWPROBE_BASE_BEHAVIOR_IMA`: base system behavior.

  * MIPS

    * :c:macro:`RISCV_HWPROBE_VENDOR_EXT_XMIPSEXECTL`: The xmipsexectl vendor
        extension is supported in the MIPS ISA extensions spec.

* :c:macro:`RISCV_HWPROBE_KEY_VENDOR_EXT_THEAD_0`: A bitmask containing the
  thead vendor extensions that are compatible with the
  :c:macro:`RISCV_HWPROBE_BASE_BEHAVIOR_IMA`: base system behavior.

  * T-HEAD

    * :c:macro:`RISCV_HWPROBE_VENDOR_EXT_XTHEADVECTOR`: The xtheadvector vendor
        extension is supported in the T-Head ISA extensions spec starting from
	commit a18c801634 ("Add T-Head VECTOR vendor extension. ").

* :c:macro:`RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE`: An unsigned int which
  represents the size of the Zicbom block in bytes.

* :c:macro:`RISCV_HWPROBE_KEY_VENDOR_EXT_SIFIVE_0`: A bitmask containing the
  sifive vendor extensions that are compatible with the
  :c:macro:`RISCV_HWPROBE_BASE_BEHAVIOR_IMA`: base system behavior.

  * SIFIVE

    * :c:macro:`RISCV_HWPROBE_VENDOR_EXT_XSFVQMACCDOD`: The Xsfqmaccdod vendor
        extension is supported in version 1.1 of SiFive Int8 Matrix Multiplication
	Extensions Specification.

    * :c:macro:`RISCV_HWPROBE_VENDOR_EXT_XSFVQMACCQOQ`: The Xsfqmaccqoq vendor
        extension is supported in version 1.1 of SiFive Int8 Matrix Multiplication
	Instruction Extensions Specification.

    * :c:macro:`RISCV_HWPROBE_VENDOR_EXT_XSFVFNRCLIPXFQF`: The Xsfvfnrclipxfqf
        vendor extension is supported in version 1.0 of SiFive FP32-to-int8 Ranged
	Clip Instructions Extensions Specification.

    * :c:macro:`RISCV_HWPROBE_VENDOR_EXT_XSFVFWMACCQQQ`: The Xsfvfwmaccqqq
        vendor extension is supported in version 1.0 of Matrix Multiply Accumulate
	Instruction Extensions Specification.