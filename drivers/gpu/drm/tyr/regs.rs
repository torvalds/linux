// SPDX-License-Identifier: GPL-2.0 or MIT

//! # Definitions
//!
//! - **CEU**: Command Execution Unit - A hardware component that executes commands (instructions)
//!   from the command stream.
//! - **CS**: Command Stream - A sequence of instructions (commands) used to control a particular
//!   job or sequence of jobs. The instructions exist in one or more command buffers.
//! - **CSF**: Command Stream Frontend - The interface and implementation for job submission
//!   exposed to the host CPU driver. This includes the global interface, as well as CSG and CS
//!   interfaces.
//! - **CSG**: Command Stream Group - A group of related command streams. The CSF manages multiple
//!   CSGs, and each CSG contains multiple CSs.
//! - **CSHW**: Command Stream Hardware - The hardware interpreting command streams, including the
//!   iterator control aspects. Implements the CSF in conjunction with the MCU.
//! - **GLB**: Global - Prefix for global interface registers that control operations common to
//!   all CSs.
//! - **JASID**: Job Address Space ID - Identifies the address space for a job.
//! - **MCU**: Microcontroller Unit - Implements the CSF in conjunction with the command stream
//!   hardware.
//! - **MMU**: Memory Management Unit - Handles address translation and memory access protection.

// We don't expect that all the registers and fields will be used, even in the
// future.
//
// Nevertheless, it is useful to have most of them defined, like the C driver
// does.
#![allow(dead_code)]

/// Combine two 32-bit values into a single 64-bit value.
pub(crate) fn join_u64(lo: u32, hi: u32) -> u64 {
    (u64::from(lo)) | ((u64::from(hi)) << 32)
}

/// Read a logical 64-bit value from split 32-bit registers without tearing.
pub(crate) fn read_u64_no_tearing(lo_read: impl Fn() -> u32, hi_read: impl Fn() -> u32) -> u64 {
    loop {
        let hi1 = hi_read();
        let lo = lo_read();
        let hi2 = hi_read();

        if hi1 == hi2 {
            return join_u64(lo, hi1);
        }
    }
}

/// These registers correspond to the GPU_CONTROL register page.
/// They are involved in GPU configuration and control.
pub(crate) mod gpu_control {
    use core::convert::TryFrom;
    use kernel::{
        error::{
            code::EINVAL,
            Error, //
        },
        num::Bounded,
        register,
        uapi, //
    };
    use pin_init::Zeroable;

    register! {
        /// GPU identification register.
        pub(crate) GPU_ID(u32) @ 0x0 {
            /// Status of the GPU release.
            3:0     ver_status;
            /// Minor release version number.
            11:4    ver_minor;
            /// Major release version number.
            15:12   ver_major;
            /// Product identifier.
            19:16   prod_major;
            /// Architecture patch revision.
            23:20   arch_rev;
            /// Architecture minor revision.
            27:24   arch_minor;
            /// Architecture major revision.
            31:28   arch_major;
        }

        /// Level 2 cache features register.
        pub(crate) L2_FEATURES(u32) @ 0x4 {
            /// Cache line size.
            7:0     line_size;
            /// Cache associativity.
            15:8    associativity;
            /// Cache slice size.
            23:16   cache_size;
            /// External bus width.
            31:24   bus_width;
        }

        /// Shader core features.
        pub(crate) CORE_FEATURES(u32) @ 0x8 {
            /// Shader core variant.
            7:0     core_variant;
        }

        /// Tiler features.
        pub(crate) TILER_FEATURES(u32) @ 0xc {
            /// Log of the tiler's bin size.
            5:0     bin_size;
            /// Maximum number of active levels.
            11:8    max_levels;
        }

        /// Memory system features.
        pub(crate) MEM_FEATURES(u32) @ 0x10 {
            0:0     coherent_core_group => bool;
            1:1     coherent_super_group => bool;
            11:8    l2_slices;
        }

        /// Memory management unit features.
        pub(crate) MMU_FEATURES(u32) @ 0x14 {
            /// Number of bits supported in virtual addresses.
            7:0     va_bits;
            /// Number of bits supported in physical addresses.
            15:8    pa_bits;
        }

        /// Address spaces present.
        pub(crate) AS_PRESENT(u32) @ 0x18 {
            31:0    present;
        }

        /// CSF version information.
        pub(crate) CSF_ID(u32) @ 0x1c {
            /// MCU revision ID.
            3:0     mcu_rev;
            /// MCU minor revision number.
            9:4     mcu_minor;
            /// MCU major revision number.
            15:10   mcu_major;
            /// CSHW revision ID.
            19:16   cshw_rev;
            /// CSHW minor revision number.
            25:20   cshw_minor;
            /// CSHW major revision number.
            31:26   cshw_major;
        }

        /// IRQ sources raw status.
        /// Writing to this register forces bits on, but does not clear them.
        pub(crate) GPU_IRQ_RAWSTAT(u32) @ 0x20 {
            /// A GPU fault has occurred, a 1-bit boolean flag.
            0:0     gpu_fault => bool;
            /// A GPU fault has occurred, a 1-bit boolean flag.
            1:1     gpu_protected_fault => bool;
            /// Reset has completed, a 1-bit boolean flag.
            8:8     reset_completed => bool;
            /// Set when a single power domain has powered up or down, a 1-bit boolean flag.
            9:9     power_changed_single => bool;
            /// Set when the all pending power domain changes are completed, a 1-bit boolean flag.
            10:10   power_changed_all => bool;
            /// Set when cache cleaning has completed, a 1-bit boolean flag.
            17:17   clean_caches_completed => bool;
            /// Mirrors the doorbell interrupt line to the CPU, a 1-bit boolean flag.
            18:18   doorbell_mirror => bool;
            /// MCU requires attention, a 1-bit boolean flag.
            19:19   mcu_status => bool;
        }

        /// IRQ sources to clear. Write only.
        pub(crate) GPU_IRQ_CLEAR(u32) @ 0x24 {
            /// Clear the GPU_FAULT interrupt, a 1-bit boolean flag.
            0:0     gpu_fault => bool;
            /// Clear the GPU_PROTECTED_FAULT interrupt, a 1-bit boolean flag.
            1:1     gpu_protected_fault => bool;
            /// Clear the RESET_COMPLETED interrupt, a 1-bit boolean flag.
            8:8     reset_completed => bool;
            /// Clear the POWER_CHANGED_SINGLE interrupt, a 1-bit boolean flag.
            9:9     power_changed_single => bool;
            /// Clear the POWER_CHANGED_ALL interrupt, a 1-bit boolean flag.
            10:10   power_changed_all => bool;
            /// Clear the CLEAN_CACHES_COMPLETED interrupt, a 1-bit boolean flag.
            17:17   clean_caches_completed => bool;
            /// Clear the MCU_STATUS interrupt, a 1-bit boolean flag.
            19:19   mcu_status => bool;
        }

        /// IRQ sources enabled.
        pub(crate) GPU_IRQ_MASK(u32) @ 0x28 {
            /// Enable the GPU_FAULT interrupt, a 1-bit boolean flag.
            0:0     gpu_fault => bool;
            /// Enable the GPU_PROTECTED_FAULT interrupt, a 1-bit boolean flag.
            1:1     gpu_protected_fault => bool;
            /// Enable the RESET_COMPLETED interrupt, a 1-bit boolean flag.
            8:8     reset_completed => bool;
            /// Enable the POWER_CHANGED_SINGLE interrupt, a 1-bit boolean flag.
            9:9     power_changed_single => bool;
            /// Enable the POWER_CHANGED_ALL interrupt, a 1-bit boolean flag.
            10:10   power_changed_all => bool;
            /// Enable the CLEAN_CACHES_COMPLETED interrupt, a 1-bit boolean flag.
            17:17   clean_caches_completed => bool;
            /// Enable the DOORBELL_MIRROR interrupt, a 1-bit boolean flag.
            18:18   doorbell_mirror => bool;
            /// Enable the MCU_STATUS interrupt, a 1-bit boolean flag.
            19:19   mcu_status => bool;
        }

        /// IRQ status for enabled sources. Read only.
        pub(crate) GPU_IRQ_STATUS(u32) @ 0x2c {
            /// GPU_FAULT interrupt status, a 1-bit boolean flag.
            0:0     gpu_fault => bool;
            /// GPU_PROTECTED_FAULT interrupt status, a 1-bit boolean flag.
            1:1     gpu_protected_fault => bool;
            /// RESET_COMPLETED interrupt status, a 1-bit boolean flag.
            8:8     reset_completed => bool;
            /// POWER_CHANGED_SINGLE interrupt status, a 1-bit boolean flag.
            9:9     power_changed_single => bool;
            /// POWER_CHANGED_ALL interrupt status, a 1-bit boolean flag.
            10:10   power_changed_all => bool;
            /// CLEAN_CACHES_COMPLETED interrupt status, a 1-bit boolean flag.
            17:17   clean_caches_completed => bool;
            /// DOORBELL_MIRROR interrupt status, a 1-bit boolean flag.
            18:18   doorbell_mirror => bool;
            /// MCU_STATUS interrupt status, a 1-bit boolean flag.
            19:19   mcu_status => bool;
        }
    }

    /// Helpers for GPU_COMMAND Register
    #[derive(Copy, Clone, Debug, PartialEq)]
    #[repr(u8)]
    pub(crate) enum GpuCommand {
        /// No operation. This is the default value.
        Nop = 0,
        /// Reset the GPU.
        Reset = 1,
        /// Flush caches.
        FlushCaches = 4,
        /// Clear GPU faults.
        ClearFault = 7,
    }

    impl TryFrom<Bounded<u32, 8>> for GpuCommand {
        type Error = Error;

        fn try_from(val: Bounded<u32, 8>) -> Result<Self, Self::Error> {
            match val.get() {
                0 => Ok(GpuCommand::Nop),
                1 => Ok(GpuCommand::Reset),
                4 => Ok(GpuCommand::FlushCaches),
                7 => Ok(GpuCommand::ClearFault),
                _ => Err(EINVAL),
            }
        }
    }

    impl From<GpuCommand> for Bounded<u32, 8> {
        fn from(cmd: GpuCommand) -> Self {
            (cmd as u8).into()
        }
    }

    /// Reset mode for [`GPU_COMMAND::reset()`].
    #[derive(Copy, Clone, Debug, PartialEq)]
    #[repr(u8)]
    pub(crate) enum ResetMode {
        /// Stop all external bus interfaces, then reset the entire GPU.
        SoftReset = 1,
        /// Force a full GPU reset.
        HardReset = 2,
    }

    impl TryFrom<Bounded<u32, 4>> for ResetMode {
        type Error = Error;

        fn try_from(val: Bounded<u32, 4>) -> Result<Self, Self::Error> {
            match val.get() {
                1 => Ok(ResetMode::SoftReset),
                2 => Ok(ResetMode::HardReset),
                _ => Err(EINVAL),
            }
        }
    }

    impl From<ResetMode> for Bounded<u32, 4> {
        fn from(mode: ResetMode) -> Self {
            Bounded::try_new(mode as u32).unwrap()
        }
    }

    /// Cache flush mode for [`GPU_COMMAND::flush_caches()`].
    #[derive(Copy, Clone, Debug, PartialEq)]
    #[repr(u8)]
    pub(crate) enum FlushMode {
        /// No flush.
        None = 0,
        /// Clean the caches.
        Clean = 1,
        /// Invalidate the caches.
        Invalidate = 2,
        /// Clean and invalidate the caches.
        CleanInvalidate = 3,
    }

    impl TryFrom<Bounded<u32, 4>> for FlushMode {
        type Error = Error;

        fn try_from(val: Bounded<u32, 4>) -> Result<Self, Self::Error> {
            match val.get() {
                0 => Ok(FlushMode::None),
                1 => Ok(FlushMode::Clean),
                2 => Ok(FlushMode::Invalidate),
                3 => Ok(FlushMode::CleanInvalidate),
                _ => Err(EINVAL),
            }
        }
    }

    impl From<FlushMode> for Bounded<u32, 4> {
        fn from(mode: FlushMode) -> Self {
            Bounded::try_new(mode as u32).unwrap()
        }
    }

    register! {
        /// GPU command register.
        ///
        /// Use the constructor methods to create commands:
        /// - [`GPU_COMMAND::nop()`]
        /// - [`GPU_COMMAND::reset()`]
        /// - [`GPU_COMMAND::flush_caches()`]
        /// - [`GPU_COMMAND::clear_fault()`]
        pub(crate) GPU_COMMAND (u32) @ 0x30 {
            7:0     command ?=> GpuCommand;
        }
        /// Internal alias for GPU_COMMAND in reset mode.
        /// Use [`GPU_COMMAND::reset()`] instead.
        GPU_COMMAND_RESET (u32) => GPU_COMMAND {
            7:0     command ?=> GpuCommand;
            11:8    reset_mode ?=> ResetMode;
        }

        /// Internal alias for GPU_COMMAND in cache flush mode.
        /// Use [`GPU_COMMAND::flush_caches()`] instead.
        GPU_COMMAND_FLUSH (u32) => GPU_COMMAND {
            7:0     command ?=> GpuCommand;
            /// L2 cache flush mode.
            11:8    l2_flush ?=> FlushMode;
            /// Shader core load/store cache flush mode.
            15:12   lsc_flush ?=> FlushMode;
            /// Shader core other caches flush mode.
            19:16   other_flush ?=> FlushMode;
        }
    }

    impl GPU_COMMAND {
        /// Create a NOP command.
        pub(crate) fn nop() -> Self {
            Self::zeroed()
        }

        /// Create a reset command with the specified reset mode.
        pub(crate) fn reset(mode: ResetMode) -> Self {
            Self::from_raw(
                GPU_COMMAND_RESET::zeroed()
                    .with_command(GpuCommand::Reset)
                    .with_reset_mode(mode)
                    .into_raw(),
            )
        }

        /// Create a cache flush command with the specified flush modes.
        pub(crate) fn flush_caches(l2: FlushMode, lsc: FlushMode, other: FlushMode) -> Self {
            Self::from_raw(
                GPU_COMMAND_FLUSH::zeroed()
                    .with_command(GpuCommand::FlushCaches)
                    .with_l2_flush(l2)
                    .with_lsc_flush(lsc)
                    .with_other_flush(other)
                    .into_raw(),
            )
        }

        /// Create a clear fault command.
        pub(crate) fn clear_fault() -> Self {
            Self::zeroed().with_command(GpuCommand::ClearFault)
        }
    }

    register! {
        /// GPU status register. Read only.
        pub(crate) GPU_STATUS(u32) @ 0x34 {
            /// GPU active, a 1-bit boolean flag.
            0:0     gpu_active => bool;
            /// Power manager active, a 1-bit boolean flag
            1:1     pwr_active => bool;
            /// Page fault active, a 1-bit boolean flag.
            4:4     page_fault => bool;
            /// Protected mode active, a 1-bit boolean flag.
            7:7     protected_mode_active => bool;
            /// Debug mode active, a 1-bit boolean flag.
            8:8     gpu_dbg_enabled => bool;
        }
    }

    #[derive(Copy, Clone, Debug, PartialEq)]
    #[repr(u8)]
    pub(crate) enum ExceptionType {
        /// Exception type: No error.
        Ok = 0x00,
        /// Exception type: GPU external bus error.
        GpuBusFault = 0x80,
        /// Exception type: GPU shareability error.
        GpuShareabilityFault = 0x88,
        /// Exception type: System shareability error.
        SystemShareabilityFault = 0x89,
        /// Exception type: GPU cacheability error.
        GpuCacheabilityFault = 0x8A,
    }

    impl TryFrom<Bounded<u32, 8>> for ExceptionType {
        type Error = Error;

        fn try_from(val: Bounded<u32, 8>) -> Result<Self, Self::Error> {
            match val.get() {
                0x00 => Ok(ExceptionType::Ok),
                0x80 => Ok(ExceptionType::GpuBusFault),
                0x88 => Ok(ExceptionType::GpuShareabilityFault),
                0x89 => Ok(ExceptionType::SystemShareabilityFault),
                0x8A => Ok(ExceptionType::GpuCacheabilityFault),
                _ => Err(EINVAL),
            }
        }
    }

    impl From<ExceptionType> for Bounded<u32, 8> {
        fn from(exc: ExceptionType) -> Self {
            (exc as u8).into()
        }
    }

    #[derive(Copy, Clone, Debug, PartialEq)]
    #[repr(u8)]
    pub(crate) enum AccessType {
        /// Access type: An atomic (read/write) transaction.
        Atomic = 0,
        /// Access type: An execute transaction.
        Execute = 1,
        /// Access type: A read transaction.
        Read = 2,
        /// Access type: A write transaction.
        Write = 3,
    }

    impl From<Bounded<u32, 2>> for AccessType {
        fn from(val: Bounded<u32, 2>) -> Self {
            match val.get() {
                0 => AccessType::Atomic,
                1 => AccessType::Execute,
                2 => AccessType::Read,
                3 => AccessType::Write,
                _ => unreachable!(),
            }
        }
    }

    impl From<AccessType> for Bounded<u32, 2> {
        fn from(access: AccessType) -> Self {
            Bounded::try_new(access as u32).unwrap()
        }
    }

    register! {
        /// GPU fault status register. Read only.
        pub(crate) GPU_FAULTSTATUS(u32) @ 0x3c {
            /// Exception type.
            7:0     exception_type ?=> ExceptionType;
            /// Access type.
            9:8     access_type => AccessType;
            /// The GPU_FAULTADDRESS is valid, a 1-bit boolean flag.
            10:10   address_valid => bool;
            /// The JASID field is valid, a 1-bit boolean flag.
            11:11   jasid_valid => bool;
            /// JASID of the fault, if known.
            15:12   jasid;
            /// ID of the source that triggered the fault.
            31:16   source_id;
        }

        /// GPU fault address. Read only.
        /// Once a fault is reported, it must be manually cleared by issuing a
        /// [`GPU_COMMAND::clear_fault()`] command to the [`GPU_COMMAND`] register. No further GPU
        /// faults will be reported until the previous fault has been cleared.
        pub(crate) GPU_FAULTADDRESS_LO(u32) @ 0x40 {
            31:0    pointer;
        }

        pub(crate) GPU_FAULTADDRESS_HI(u32) @ 0x44 {
            31:0    pointer;
        }

        /// Level 2 cache configuration.
        pub(crate) L2_CONFIG(u32) @ 0x48 {
            /// Requested cache size.
            23:16   cache_size;
            /// Requested hash function index.
            31:24   hash_function;
        }

        /// Global time stamp offset.
        pub(crate) TIMESTAMP_OFFSET_LO(u32) @ 0x88 {
            31:0    offset;
        }

        pub(crate) TIMESTAMP_OFFSET_HI(u32) @ 0x8c {
            31:0    offset;
        }

        /// GPU cycle counter. Read only.
        pub(crate) CYCLE_COUNT_LO(u32) @ 0x90 {
            31:0    count;
        }

        pub(crate) CYCLE_COUNT_HI(u32) @ 0x94 {
            31:0    count;
        }

        /// Global time stamp. Read only.
        pub(crate) TIMESTAMP_LO(u32) @ 0x98 {
            31:0    timestamp;
        }

        pub(crate) TIMESTAMP_HI(u32) @ 0x9c {
            31:0    timestamp;
        }

        /// Maximum number of threads per core. Read only constant.
        pub(crate) THREAD_MAX_THREADS(u32) @ 0xa0 {
            31:0    threads;
        }

        /// Maximum number of threads per workgroup. Read only constant.
        pub(crate) THREAD_MAX_WORKGROUP_SIZE(u32) @ 0xa4 {
            31:0    threads;
        }

        /// Maximum number of threads per barrier. Read only constant.
        pub(crate) THREAD_MAX_BARRIER_SIZE(u32) @ 0xa8 {
            31:0    threads;
        }

        /// Thread features. Read only constant.
        pub(crate) THREAD_FEATURES(u32) @ 0xac {
            /// Total number of registers per core.
            21:0    max_registers;
            /// Implementation technology type.
            23:22   implementation_technology;
            /// Maximum number of compute tasks waiting.
            31:24   max_task_queue;
        }

        /// Support flags for compressed texture formats. Read only constant.
        ///
        /// A bitmap where each bit indicates support for a specific compressed texture format.
        /// The bit position maps to an opaque format ID (`texture_features_key_t` in spec).
        pub(crate) TEXTURE_FEATURES(u32)[4] @ 0xb0 {
            31:0    supported_formats;
        }

        /// Shader core present bitmap. Read only constant.
        pub(crate) SHADER_PRESENT_LO(u32) @ 0x100 {
            31:0    value;
        }

        pub(crate) SHADER_PRESENT_HI(u32) @ 0x104 {
            31:0    value;
        }

        /// Tiler present bitmap. Read only constant.
        pub(crate) TILER_PRESENT_LO(u32) @ 0x110 {
            31:0    present;
        }

        pub(crate) TILER_PRESENT_HI(u32) @ 0x114 {
            31:0    present;
        }

        /// L2 cache present bitmap. Read only constant.
        pub(crate) L2_PRESENT_LO(u32) @ 0x120 {
            31:0    present;
        }

        pub(crate) L2_PRESENT_HI(u32) @ 0x124 {
            31:0    present;
        }

        /// Shader core ready bitmap. Read only.
        pub(crate) SHADER_READY_LO(u32) @ 0x140 {
            31:0    ready;
        }

        pub(crate) SHADER_READY_HI(u32) @ 0x144 {
            31:0    ready;
        }

        /// Tiler ready bitmap. Read only.
        pub(crate) TILER_READY_LO(u32) @ 0x150 {
            31:0    ready;
        }

        pub(crate) TILER_READY_HI(u32) @ 0x154 {
            31:0    ready;
        }

        /// L2 ready bitmap. Read only.
        pub(crate) L2_READY_LO(u32) @ 0x160 {
            31:0    ready;
        }

        pub(crate) L2_READY_HI(u32) @ 0x164 {
            31:0    ready;
        }

        /// Shader core power up bitmap.
        pub(crate) SHADER_PWRON_LO(u32) @ 0x180 {
            31:0    request;
        }

        pub(crate) SHADER_PWRON_HI(u32) @ 0x184 {
            31:0    request;
        }

        /// Tiler power up bitmap.
        pub(crate) TILER_PWRON_LO(u32) @ 0x190 {
            31:0    request;
        }

        pub(crate) TILER_PWRON_HI(u32) @ 0x194 {
            31:0    request;
        }

        /// L2 power up bitmap.
        pub(crate) L2_PWRON_LO(u32) @ 0x1a0 {
            31:0    request;
        }

        pub(crate) L2_PWRON_HI(u32) @ 0x1a4 {
            31:0    request;
        }

        /// Shader core power down bitmap.
        pub(crate) SHADER_PWROFF_LO(u32) @ 0x1c0 {
            31:0    request;
        }

        pub(crate) SHADER_PWROFF_HI(u32) @ 0x1c4 {
            31:0    request;
        }

        /// Tiler power down bitmap.
        pub(crate) TILER_PWROFF_LO(u32) @ 0x1d0 {
            31:0    request;
        }

        pub(crate) TILER_PWROFF_HI(u32) @ 0x1d4 {
            31:0    request;
        }

        /// L2 power down bitmap.
        pub(crate) L2_PWROFF_LO(u32) @ 0x1e0 {
            31:0    request;
        }

        pub(crate) L2_PWROFF_HI(u32) @ 0x1e4 {
            31:0    request;
        }

        /// Shader core power transition bitmap. Read-only.
        pub(crate) SHADER_PWRTRANS_LO(u32) @ 0x200 {
            31:0    changing;
        }

        pub(crate) SHADER_PWRTRANS_HI(u32) @ 0x204 {
            31:0    changing;
        }

        /// Tiler power transition bitmap. Read-only.
        pub(crate) TILER_PWRTRANS_LO(u32) @ 0x210 {
            31:0    changing;
        }

        pub(crate) TILER_PWRTRANS_HI(u32) @ 0x214 {
            31:0    changing;
        }

        /// L2 power transition bitmap. Read-only.
        pub(crate) L2_PWRTRANS_LO(u32) @ 0x220 {
            31:0    changing;
        }

        pub(crate) L2_PWRTRANS_HI(u32) @ 0x224 {
            31:0    changing;
        }

        /// Shader core active bitmap. Read-only.
        pub(crate) SHADER_PWRACTIVE_LO(u32) @ 0x240 {
            31:0    active;
        }

        pub(crate) SHADER_PWRACTIVE_HI(u32) @ 0x244 {
            31:0    active;
        }

        /// Tiler active bitmap. Read-only.
        pub(crate) TILER_PWRACTIVE_LO(u32) @ 0x250 {
            31:0    active;
        }

        pub(crate) TILER_PWRACTIVE_HI(u32) @ 0x254 {
            31:0    active;
        }

        /// L2 active bitmap.  Read-only.
        pub(crate) L2_PWRACTIVE_LO(u32) @ 0x260 {
            31:0    active;
        }

        pub(crate) L2_PWRACTIVE_HI(u32) @ 0x264 {
            31:0    active;
        }

        /// Revision ID. Read only constant.
        pub(crate) REVIDR(u32) @ 0x280 {
            31:0    revision;
        }

        /// Coherency features present. Read only constant.
        /// Supported protocols on the interconnect between the GPU and the
        /// system into which it is integrated.
        pub(crate) COHERENCY_FEATURES(u32) @ 0x300 {
            /// ACE-Lite protocol supported, a 1-bit boolean flag.
            0:0     ace_lite => bool;
            /// ACE protocol supported, a 1-bit boolean flag.
            1:1     ace => bool;
        }
    }

    #[derive(Copy, Clone, Debug, PartialEq)]
    #[repr(u8)]
    pub(crate) enum CoherencyMode {
        /// ACE-Lite coherency protocol.
        AceLite = uapi::drm_panthor_gpu_coherency_DRM_PANTHOR_GPU_COHERENCY_ACE_LITE as u8,
        /// ACE coherency protocol.
        Ace = uapi::drm_panthor_gpu_coherency_DRM_PANTHOR_GPU_COHERENCY_ACE as u8,
        /// No coherency protocol.
        None = uapi::drm_panthor_gpu_coherency_DRM_PANTHOR_GPU_COHERENCY_NONE as u8,
    }

    impl TryFrom<Bounded<u32, 32>> for CoherencyMode {
        type Error = Error;

        fn try_from(val: Bounded<u32, 32>) -> Result<Self, Self::Error> {
            match val.get() {
                0 => Ok(CoherencyMode::AceLite),
                1 => Ok(CoherencyMode::Ace),
                31 => Ok(CoherencyMode::None),
                _ => Err(EINVAL),
            }
        }
    }

    impl From<CoherencyMode> for Bounded<u32, 32> {
        fn from(mode: CoherencyMode) -> Self {
            (mode as u8).into()
        }
    }

    register! {
        /// Coherency enable. An index of which coherency protocols should be used.
        /// This register only selects the protocol for coherency messages on the
        /// interconnect. This is not to enable or disable coherency controlled by MMU.
        pub(crate) COHERENCY_ENABLE(u32) @ 0x304 {
            31:0    l2_cache_protocol_select ?=> CoherencyMode;
        }
    }

    /// Helpers for MCU_CONTROL register
    #[derive(Copy, Clone, Debug, PartialEq)]
    #[repr(u8)]
    pub(crate) enum McuControlMode {
        /// Disable the MCU.
        Disable = 0,
        /// Enable the MCU.
        Enable = 1,
        /// Enable the MCU to execute and automatically reboot after a fast reset.
        Auto = 2,
    }

    impl TryFrom<Bounded<u32, 2>> for McuControlMode {
        type Error = Error;

        fn try_from(val: Bounded<u32, 2>) -> Result<Self, Self::Error> {
            match val.get() {
                0 => Ok(McuControlMode::Disable),
                1 => Ok(McuControlMode::Enable),
                2 => Ok(McuControlMode::Auto),
                _ => Err(EINVAL),
            }
        }
    }

    impl From<McuControlMode> for Bounded<u32, 2> {
        fn from(mode: McuControlMode) -> Self {
            Bounded::try_new(mode as u32).unwrap()
        }
    }

    register! {
        /// MCU control.
        pub(crate) MCU_CONTROL(u32) @ 0x700 {
            /// Request MCU state change.
            1:0 req ?=> McuControlMode;
        }
    }

    /// Helpers for MCU_STATUS register
    #[derive(Copy, Clone, Debug, PartialEq)]
    #[repr(u8)]
    pub(crate) enum McuStatus {
        /// MCU is disabled.
        Disabled = 0,
        /// MCU is enabled.
        Enabled = 1,
        /// The MCU has halted by itself in an orderly manner to enable the core group to be
        /// powered down.
        Halt = 2,
        /// The MCU has encountered an error that prevents it from continuing.
        Fatal = 3,
    }

    impl From<Bounded<u32, 2>> for McuStatus {
        fn from(val: Bounded<u32, 2>) -> Self {
            match val.get() {
                0 => McuStatus::Disabled,
                1 => McuStatus::Enabled,
                2 => McuStatus::Halt,
                3 => McuStatus::Fatal,
                _ => unreachable!(),
            }
        }
    }

    impl From<McuStatus> for Bounded<u32, 2> {
        fn from(status: McuStatus) -> Self {
            Bounded::try_new(status as u32).unwrap()
        }
    }

    register! {
        /// MCU status. Read only.
        pub(crate) MCU_STATUS(u32) @ 0x704 {
            /// Read current state of MCU.
            1:0 value => McuStatus;
        }
    }
}

/// These registers correspond to the JOB_CONTROL register page.
/// They are involved in communication between the firmware running on the MCU and the host.
pub(crate) mod job_control {
    use kernel::register;

    register! {
        /// Raw status of job interrupts.
        ///
        /// Write to this register to trigger these interrupts.
        /// Writing a 1 to a bit forces that bit on.
        pub(crate) JOB_IRQ_RAWSTAT(u32) @ 0x1000 {
            /// CSG request. These bits indicate that CSGn requires attention from the host.
            30:0    csg;
            /// GLB request. Indicates that the GLB interface requires attention from the host.
            31:31   glb => bool;
        }

        /// Clear job interrupts. Write only.
        ///
        /// Write a 1 to a bit to clear the corresponding bit in [`JOB_IRQ_RAWSTAT`].
        pub(crate) JOB_IRQ_CLEAR(u32) @ 0x1004 {
            /// Clear CSG request interrupts.
            30:0    csg;
            /// Clear GLB request interrupt.
            31:31   glb => bool;
        }

        /// Mask for job interrupts.
        ///
        /// Set each bit to 1 to enable the corresponding interrupt source or to 0 to disable it.
        pub(crate) JOB_IRQ_MASK(u32) @ 0x1008 {
            /// Enable CSG request interrupts.
            30:0    csg;
            /// Enable GLB request interrupt.
            31:31   glb => bool;
        }

        /// Active job interrupts. Read only.
        ///
        /// This register contains the result of ANDing together [`JOB_IRQ_RAWSTAT`] and
        /// [`JOB_IRQ_MASK`].
        pub(crate) JOB_IRQ_STATUS(u32) @ 0x100c {
            /// CSG request interrupt status.
            30:0    csg;
            /// GLB request interrupt status.
            31:31   glb => bool;
        }
    }
}

/// These registers correspond to the MMU_CONTROL register page.
/// They are involved in MMU configuration and control.
pub(crate) mod mmu_control {
    use kernel::register;

    register! {
        /// IRQ sources raw status.
        ///
        /// This register contains the raw unmasked interrupt sources for MMU status and exception
        /// handling.
        ///
        /// Writing to this register forces bits on.
        /// Use [`IRQ_CLEAR`] to clear interrupts.
        pub(crate) IRQ_RAWSTAT(u32) @ 0x2000 {
            /// Page fault for address spaces.
            15:0    page_fault;
            /// Command completed in address spaces.
            31:16   command_completed;
        }

        /// IRQ sources to clear.
        /// Write a 1 to a bit to clear the corresponding bit in [`IRQ_RAWSTAT`].
        pub(crate) IRQ_CLEAR(u32) @ 0x2004 {
            /// Clear the PAGE_FAULT interrupt.
            15:0    page_fault;
            /// Clear the COMMAND_COMPLETED interrupt.
            31:16   command_completed;
        }

        /// IRQ sources enabled.
        ///
        /// Set each bit to 1 to enable the corresponding interrupt source, and to 0 to disable it.
        pub(crate) IRQ_MASK(u32) @ 0x2008 {
            /// Enable the PAGE_FAULT interrupt.
            15:0    page_fault;
            /// Enable the COMMAND_COMPLETED interrupt.
            31:16   command_completed;
        }

        /// IRQ status for enabled sources. Read only.
        ///
        /// This register contains the result of ANDing together [`IRQ_RAWSTAT`] and [`IRQ_MASK`].
        pub(crate) IRQ_STATUS(u32) @ 0x200c {
            /// PAGE_FAULT interrupt status.
            15:0    page_fault;
            /// COMMAND_COMPLETED interrupt status.
            31:16   command_completed;
        }
    }

    /// Per-address space registers ASn [0..15] within the MMU_CONTROL page.
    ///
    /// This array contains 16 instances of the MMU_AS_CONTROL register page.
    pub(crate) mod mmu_as_control {
        use core::convert::TryFrom;

        use kernel::{
            error::{
                code::EINVAL,
                Error, //
            },
            num::Bounded,
            register, //
        };

        /// Maximum number of hardware address space slots.
        /// The actual number of slots available is usually lower.
        pub(crate) const MAX_AS: usize = 16;

        /// Address space register stride. The elements in the array are spaced 64B apart.
        const STRIDE: usize = 0x40;

        register! {
            /// Translation table base address. A 64-bit pointer.
            ///
            /// This field contains the address of the top level of a translation table structure.
            /// This must be 16-byte-aligned, so address bits [3:0] are assumed to be zero.
            pub(crate) TRANSTAB(u64)[MAX_AS, stride = STRIDE] @ 0x2400 {
                /// Base address of the translation table.
                63:0    base;
            }

            // TRANSTAB is a logical 64-bit register, but it is laid out in hardware as two
            // 32-bit halves. Define it as separate low/high u32 registers so accesses match
            // the MMIO register layout and do not rely on native 64-bit MMIO transactions.
            pub(crate) TRANSTAB_LO(u32)[MAX_AS, stride = STRIDE] @ 0x2400 {
                   31:0 value;
            }

            pub(crate) TRANSTAB_HI(u32)[MAX_AS, stride = STRIDE] @ 0x2404 {
                31:0 value;
            }
        }

        /// Helpers for MEMATTR Register.

        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum AllocPolicySelect {
            /// Ignore ALLOC_R/ALLOC_W fields.
            Impl = 2,
            /// Use ALLOC_R/ALLOC_W fields for allocation policy.
            Alloc = 3,
        }

        impl TryFrom<Bounded<u8, 2>> for AllocPolicySelect {
            type Error = Error;

            fn try_from(val: Bounded<u8, 2>) -> Result<Self, Self::Error> {
                match val.get() {
                    2 => Ok(Self::Impl),
                    3 => Ok(Self::Alloc),
                    _ => Err(EINVAL),
                }
            }
        }

        impl From<AllocPolicySelect> for Bounded<u8, 2> {
            fn from(val: AllocPolicySelect) -> Self {
                Bounded::try_new(val as u8).unwrap()
            }
        }

        /// Coherency policy for memory attributes. Indicates the shareability of cached accesses.
        ///
        /// The hardware spec defines different interpretations of these values depending on
        /// whether TRANSCFG.MODE is set to IDENTITY or not. IDENTITY mode does not use translation
        /// tables (all input addresses map to the same output address); it is deprecated and not
        /// used by the driver. This enum assumes that TRANSCFG.MODE is not set to IDENTITY.
        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum Coherency {
            /// Midgard inner domain coherency.
            ///
            /// Most flexible mode - can map non-coherent, internally coherent, and system/IO
            /// coherent memory. Used for non-cacheable memory in MAIR conversion.
            MidgardInnerDomain = 0,
            /// CPU inner domain coherency.
            ///
            /// Can map non-coherent and system/IO coherent memory. Used for write-back
            /// cacheable memory in MAIR conversion to maintain CPU-GPU cache coherency.
            CpuInnerDomain = 1,
            /// CPU inner domain with shader coherency.
            ///
            /// Can map internally coherent and system/IO coherent memory. Used for
            /// GPU-internal shared buffers requiring shader coherency.
            CpuInnerDomainShaderCoh = 2,
        }

        impl TryFrom<Bounded<u8, 2>> for Coherency {
            type Error = Error;

            fn try_from(val: Bounded<u8, 2>) -> Result<Self, Self::Error> {
                match val.get() {
                    0 => Ok(Self::MidgardInnerDomain),
                    1 => Ok(Self::CpuInnerDomain),
                    2 => Ok(Self::CpuInnerDomainShaderCoh),
                    _ => Err(EINVAL),
                }
            }
        }

        impl From<Coherency> for Bounded<u8, 2> {
            fn from(val: Coherency) -> Self {
                Bounded::try_new(val as u8).unwrap()
            }
        }

        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum MemoryType {
            /// Normal memory (shared).
            Shared = 0,
            /// Normal memory, inner/outer non-cacheable.
            NonCacheable = 1,
            /// Normal memory, inner/outer write-back cacheable.
            WriteBack = 2,
            /// Triggers MEMORY_ATTRIBUTE_FAULT.
            Fault = 3,
        }

        impl From<Bounded<u8, 2>> for MemoryType {
            fn from(val: Bounded<u8, 2>) -> Self {
                match val.get() {
                    0 => Self::Shared,
                    1 => Self::NonCacheable,
                    2 => Self::WriteBack,
                    3 => Self::Fault,
                    _ => unreachable!(),
                }
            }
        }

        impl From<MemoryType> for Bounded<u8, 2> {
            fn from(val: MemoryType) -> Self {
                Bounded::try_new(val as u8).unwrap()
            }
        }

        register! {
            /// Stage 1 memory attributes (8-bit bitfield).
            ///
            /// This is not an actual register, but a bitfield definition used by the MEMATTR
            /// register. Each of the 8 bytes in MEMATTR follows this layout.
            MMU_MEMATTR_STAGE1(u8) @ 0x0 {
                /// Inner cache write allocation policy.
                0:0     alloc_w => bool;
                /// Inner cache read allocation policy.
                1:1     alloc_r => bool;
                /// Inner allocation policy select.
                3:2     alloc_sel ?=> AllocPolicySelect;
                /// Coherency policy.
                5:4     coherency ?=> Coherency;
                /// Memory type.
                7:6     memory_type => MemoryType;
            }
        }

        impl TryFrom<Bounded<u64, 8>> for MMU_MEMATTR_STAGE1 {
            type Error = Error;

            fn try_from(val: Bounded<u64, 8>) -> Result<Self, Self::Error> {
                Ok(Self::from_raw(val.get() as u8))
            }
        }

        impl From<MMU_MEMATTR_STAGE1> for Bounded<u64, 8> {
            fn from(val: MMU_MEMATTR_STAGE1) -> Self {
                Bounded::try_new(u64::from(val.into_raw())).unwrap()
            }
        }

        register! {
            /// Memory attributes.
            ///
            /// Each address space can configure up to 8 different memory attribute profiles.
            /// Each attribute profile follows the MMU_MEMATTR_STAGE1 layout.
            pub(crate) MEMATTR(u64)[MAX_AS, stride = STRIDE] @ 0x2408 {
                7:0     attribute0 ?=> MMU_MEMATTR_STAGE1;
                15:8    attribute1 ?=> MMU_MEMATTR_STAGE1;
                23:16   attribute2 ?=> MMU_MEMATTR_STAGE1;
                31:24   attribute3 ?=> MMU_MEMATTR_STAGE1;
                39:32   attribute4 ?=> MMU_MEMATTR_STAGE1;
                47:40   attribute5 ?=> MMU_MEMATTR_STAGE1;
                55:48   attribute6 ?=> MMU_MEMATTR_STAGE1;
                63:56   attribute7 ?=> MMU_MEMATTR_STAGE1;
            }

            // MEMATTR is a logical 64-bit register, but it is laid out in hardware as two
            // 32-bit halves. Define it as separate low/high u32 registers so accesses match
            // the MMIO register layout and do not rely on native 64-bit MMIO transactions.
            pub(crate) MEMATTR_LO(u32)[MAX_AS, stride = STRIDE] @ 0x2408 {
                31:0 value;
            }

            pub(crate) MEMATTR_HI(u32)[MAX_AS, stride = STRIDE] @ 0x240c {
                31:0 value;
            }

            /// Lock region address for each address space.
            pub(crate) LOCKADDR(u64)[MAX_AS, stride = STRIDE] @ 0x2410 {
                /// Lock region size.
                5:0     size;
                /// Lock region base address.
                63:12   base;
            }

            // LOCKADDR is a logical 64-bit register, but it is laid out in hardware as two
            // 32-bit halves. Define it as separate low/high u32 registers so accesses match
            // the MMIO register layout and do not rely on native 64-bit MMIO transactions.
            pub(crate) LOCKADDR_LO(u32)[MAX_AS, stride = STRIDE] @ 0x2410 {
               31:0 value;
            }

            pub(crate) LOCKADDR_HI(u32)[MAX_AS, stride = STRIDE] @ 0x2414 {
                31:0 value;
            }
        }

        /// Helpers for MMU COMMAND register.
        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum MmuCommand {
            /// No operation, nothing happens.
            Nop = 0,
            /// Propagate settings to the MMU.
            Update = 1,
            /// Lock an address region.
            Lock = 2,
            /// Unlock an address region.
            Unlock = 3,
            /// Clean and invalidate the L2 cache, then unlock.
            FlushPt = 4,
            /// Clean and invalidate all caches, then unlock.
            FlushMem = 5,
        }

        impl TryFrom<Bounded<u32, 8>> for MmuCommand {
            type Error = Error;

            fn try_from(val: Bounded<u32, 8>) -> Result<Self, Self::Error> {
                match val.get() {
                    0 => Ok(MmuCommand::Nop),
                    1 => Ok(MmuCommand::Update),
                    2 => Ok(MmuCommand::Lock),
                    3 => Ok(MmuCommand::Unlock),
                    4 => Ok(MmuCommand::FlushPt),
                    5 => Ok(MmuCommand::FlushMem),
                    _ => Err(EINVAL),
                }
            }
        }

        impl From<MmuCommand> for Bounded<u32, 8> {
            fn from(cmd: MmuCommand) -> Self {
                (cmd as u8).into()
            }
        }

        register! {
            /// MMU command register for each address space. Write only.
            pub(crate) COMMAND(u32)[MAX_AS, stride = STRIDE] @ 0x2418 {
                7:0     command ?=> MmuCommand;
            }
        }

        /// MMU exception types for FAULTSTATUS register.
        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum MmuExceptionType {
            /// No error.
            Ok = 0x00,
            /// Invalid translation table entry, level 0.
            TranslationFault0 = 0xC0,
            /// Invalid translation table entry, level 1.
            TranslationFault1 = 0xC1,
            /// Invalid translation table entry, level 2.
            TranslationFault2 = 0xC2,
            /// Invalid translation table entry, level 3.
            TranslationFault3 = 0xC3,
            /// Invalid block descriptor.
            TranslationFault4 = 0xC4,
            /// Page permission error, level 0.
            PermissionFault0 = 0xC8,
            /// Page permission error, level 1.
            PermissionFault1 = 0xC9,
            /// Page permission error, level 2.
            PermissionFault2 = 0xCA,
            /// Page permission error, level 3.
            PermissionFault3 = 0xCB,
            /// Access flag not set, level 1.
            AccessFlag1 = 0xD9,
            /// Access flag not set, level 2.
            AccessFlag2 = 0xDA,
            /// Access flag not set, level 3.
            AccessFlag3 = 0xDB,
            /// Virtual address out of range.
            AddressSizeFaultIn = 0xE0,
            /// Physical address out of range, level 0.
            AddressSizeFaultOut0 = 0xE4,
            /// Physical address out of range, level 1.
            AddressSizeFaultOut1 = 0xE5,
            /// Physical address out of range, level 2.
            AddressSizeFaultOut2 = 0xE6,
            /// Physical address out of range, level 3.
            AddressSizeFaultOut3 = 0xE7,
            /// Page attribute error, level 0.
            MemoryAttributeFault0 = 0xE8,
            /// Page attribute error, level 1.
            MemoryAttributeFault1 = 0xE9,
            /// Page attribute error, level 2.
            MemoryAttributeFault2 = 0xEA,
            /// Page attribute error, level 3.
            MemoryAttributeFault3 = 0xEB,
        }

        impl TryFrom<Bounded<u32, 8>> for MmuExceptionType {
            type Error = Error;

            fn try_from(val: Bounded<u32, 8>) -> Result<Self, Self::Error> {
                match val.get() {
                    0x00 => Ok(MmuExceptionType::Ok),
                    0xC0 => Ok(MmuExceptionType::TranslationFault0),
                    0xC1 => Ok(MmuExceptionType::TranslationFault1),
                    0xC2 => Ok(MmuExceptionType::TranslationFault2),
                    0xC3 => Ok(MmuExceptionType::TranslationFault3),
                    0xC4 => Ok(MmuExceptionType::TranslationFault4),
                    0xC8 => Ok(MmuExceptionType::PermissionFault0),
                    0xC9 => Ok(MmuExceptionType::PermissionFault1),
                    0xCA => Ok(MmuExceptionType::PermissionFault2),
                    0xCB => Ok(MmuExceptionType::PermissionFault3),
                    0xD9 => Ok(MmuExceptionType::AccessFlag1),
                    0xDA => Ok(MmuExceptionType::AccessFlag2),
                    0xDB => Ok(MmuExceptionType::AccessFlag3),
                    0xE0 => Ok(MmuExceptionType::AddressSizeFaultIn),
                    0xE4 => Ok(MmuExceptionType::AddressSizeFaultOut0),
                    0xE5 => Ok(MmuExceptionType::AddressSizeFaultOut1),
                    0xE6 => Ok(MmuExceptionType::AddressSizeFaultOut2),
                    0xE7 => Ok(MmuExceptionType::AddressSizeFaultOut3),
                    0xE8 => Ok(MmuExceptionType::MemoryAttributeFault0),
                    0xE9 => Ok(MmuExceptionType::MemoryAttributeFault1),
                    0xEA => Ok(MmuExceptionType::MemoryAttributeFault2),
                    0xEB => Ok(MmuExceptionType::MemoryAttributeFault3),
                    _ => Err(EINVAL),
                }
            }
        }

        impl From<MmuExceptionType> for Bounded<u32, 8> {
            fn from(exc: MmuExceptionType) -> Self {
                (exc as u8).into()
            }
        }

        /// Access type for MMU faults.
        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum MmuAccessType {
            /// An atomic (read/write) transaction.
            Atomic = 0,
            /// An execute transaction.
            Execute = 1,
            /// A read transaction.
            Read = 2,
            /// A write transaction.
            Write = 3,
        }

        impl From<Bounded<u32, 2>> for MmuAccessType {
            fn from(val: Bounded<u32, 2>) -> Self {
                match val.get() {
                    0 => MmuAccessType::Atomic,
                    1 => MmuAccessType::Execute,
                    2 => MmuAccessType::Read,
                    3 => MmuAccessType::Write,
                    _ => unreachable!(),
                }
            }
        }

        impl From<MmuAccessType> for Bounded<u32, 2> {
            fn from(access: MmuAccessType) -> Self {
                Bounded::try_new(access as u32).unwrap()
            }
        }

        register! {
            /// Fault status register for each address space. Read only.
            pub(crate) FAULTSTATUS(u32)[MAX_AS, stride = STRIDE] @ 0x241c {
                /// Exception type.
                7:0     exception_type ?=> MmuExceptionType;
                /// Access type.
                9:8     access_type => MmuAccessType;
                /// ID of the source that triggered the fault.
                31:16   source_id;
            }

            /// Fault address for each address space. Read only.
            pub(crate) FAULTADDRESS_LO(u32)[MAX_AS, stride = STRIDE] @ 0x2420 {
                31:0    pointer;
            }

            pub(crate) FAULTADDRESS_HI(u32)[MAX_AS, stride = STRIDE] @ 0x2424 {
                31:0    pointer;
            }

            /// MMU status register for each address space. Read only.
            pub(crate) STATUS(u32)[MAX_AS, stride = STRIDE] @ 0x2428 {
                /// External address space command is active, a 1-bit boolean flag.
                0:0     active_ext => bool;
                /// Internal address space command is active, a 1-bit boolean flag.
                1:1     active_int => bool;
            }
        }

        /// Helpers for TRANSCFG register.
        ///
        /// Address space mode for TRANSCFG register.
        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum AddressSpaceMode {
            /// The MMU forces all memory access to fail with a decode fault.
            Unmapped = 1,
            /// All input addresses map to the same output address (deprecated).
            Identity = 2,
            /// Translation tables interpreted according to AArch64 4kB granule specification.
            Aarch64_4K = 6,
            /// Translation tables interpreted according to AArch64 64kB granule specification.
            Aarch64_64K = 8,
        }

        impl TryFrom<Bounded<u64, 4>> for AddressSpaceMode {
            type Error = Error;

            fn try_from(val: Bounded<u64, 4>) -> Result<Self, Self::Error> {
                match val.get() {
                    1 => Ok(AddressSpaceMode::Unmapped),
                    2 => Ok(AddressSpaceMode::Identity),
                    6 => Ok(AddressSpaceMode::Aarch64_4K),
                    8 => Ok(AddressSpaceMode::Aarch64_64K),
                    _ => Err(EINVAL),
                }
            }
        }

        impl From<AddressSpaceMode> for Bounded<u64, 4> {
            fn from(mode: AddressSpaceMode) -> Self {
                Bounded::try_new(mode as u64).unwrap()
            }
        }

        /// Input address range restriction for TRANSCFG register.
        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum InaBits {
            /// Invalid VA range (reset value).
            Reset = 0,
            /// 48-bit VA range.
            Bits48 = 7,
            /// 47-bit VA range.
            Bits47 = 8,
            /// 46-bit VA range.
            Bits46 = 9,
            /// 45-bit VA range.
            Bits45 = 10,
            /// 44-bit VA range.
            Bits44 = 11,
            /// 43-bit VA range.
            Bits43 = 12,
            /// 42-bit VA range.
            Bits42 = 13,
            /// 41-bit VA range.
            Bits41 = 14,
            /// 40-bit VA range.
            Bits40 = 15,
            /// 39-bit VA range.
            Bits39 = 16,
            /// 38-bit VA range.
            Bits38 = 17,
            /// 37-bit VA range.
            Bits37 = 18,
            /// 36-bit VA range.
            Bits36 = 19,
            /// 35-bit VA range.
            Bits35 = 20,
            /// 34-bit VA range.
            Bits34 = 21,
            /// 33-bit VA range.
            Bits33 = 22,
            /// 32-bit VA range.
            Bits32 = 23,
            /// 31-bit VA range.
            Bits31 = 24,
            /// 30-bit VA range.
            Bits30 = 25,
            /// 29-bit VA range.
            Bits29 = 26,
            /// 28-bit VA range.
            Bits28 = 27,
            /// 27-bit VA range.
            Bits27 = 28,
            /// 26-bit VA range.
            Bits26 = 29,
            /// 25-bit VA range.
            Bits25 = 30,
        }

        impl TryFrom<Bounded<u64, 5>> for InaBits {
            type Error = Error;

            fn try_from(val: Bounded<u64, 5>) -> Result<Self, Self::Error> {
                match val.get() {
                    0 => Ok(InaBits::Reset),
                    7 => Ok(InaBits::Bits48),
                    8 => Ok(InaBits::Bits47),
                    9 => Ok(InaBits::Bits46),
                    10 => Ok(InaBits::Bits45),
                    11 => Ok(InaBits::Bits44),
                    12 => Ok(InaBits::Bits43),
                    13 => Ok(InaBits::Bits42),
                    14 => Ok(InaBits::Bits41),
                    15 => Ok(InaBits::Bits40),
                    16 => Ok(InaBits::Bits39),
                    17 => Ok(InaBits::Bits38),
                    18 => Ok(InaBits::Bits37),
                    19 => Ok(InaBits::Bits36),
                    20 => Ok(InaBits::Bits35),
                    21 => Ok(InaBits::Bits34),
                    22 => Ok(InaBits::Bits33),
                    23 => Ok(InaBits::Bits32),
                    24 => Ok(InaBits::Bits31),
                    25 => Ok(InaBits::Bits30),
                    26 => Ok(InaBits::Bits29),
                    27 => Ok(InaBits::Bits28),
                    28 => Ok(InaBits::Bits27),
                    29 => Ok(InaBits::Bits26),
                    30 => Ok(InaBits::Bits25),
                    _ => Err(EINVAL),
                }
            }
        }

        impl From<InaBits> for Bounded<u64, 5> {
            fn from(bits: InaBits) -> Self {
                Bounded::try_new(bits as u64).unwrap()
            }
        }

        /// Translation table memory attributes for TRANSCFG register.
        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        pub(crate) enum PtwMemattr {
            /// Invalid (reset value, not valid for enabled address space).
            Invalid = 0,
            /// Normal memory, inner/outer non-cacheable.
            NonCacheable = 1,
            /// Normal memory, inner/outer write-back cacheable.
            WriteBack = 2,
        }

        impl TryFrom<Bounded<u64, 2>> for PtwMemattr {
            type Error = Error;

            fn try_from(val: Bounded<u64, 2>) -> Result<Self, Self::Error> {
                match val.get() {
                    0 => Ok(PtwMemattr::Invalid),
                    1 => Ok(PtwMemattr::NonCacheable),
                    2 => Ok(PtwMemattr::WriteBack),
                    _ => Err(EINVAL),
                }
            }
        }

        impl From<PtwMemattr> for Bounded<u64, 2> {
            fn from(attr: PtwMemattr) -> Self {
                Bounded::try_new(attr as u64).unwrap()
            }
        }

        /// Translation table memory shareability for TRANSCFG register.
        #[derive(Copy, Clone, Debug, PartialEq)]
        #[repr(u8)]
        #[allow(clippy::enum_variant_names)]
        pub(crate) enum PtwShareability {
            /// Non-shareable.
            NonShareable = 0,
            /// Outer shareable.
            OuterShareable = 2,
            /// Inner shareable.
            InnerShareable = 3,
        }

        impl TryFrom<Bounded<u64, 2>> for PtwShareability {
            type Error = Error;

            fn try_from(val: Bounded<u64, 2>) -> Result<Self, Self::Error> {
                match val.get() {
                    0 => Ok(PtwShareability::NonShareable),
                    2 => Ok(PtwShareability::OuterShareable),
                    3 => Ok(PtwShareability::InnerShareable),
                    _ => Err(EINVAL),
                }
            }
        }

        impl From<PtwShareability> for Bounded<u64, 2> {
            fn from(sh: PtwShareability) -> Self {
                Bounded::try_new(sh as u64).unwrap()
            }
        }

        register! {
            /// Translation configuration and control.
            pub(crate) TRANSCFG(u64)[MAX_AS, stride = STRIDE] @ 0x2430 {
                /// Address space mode.
                3:0     mode ?=> AddressSpaceMode;
                /// Address input restriction.
                10:6    ina_bits ?=> InaBits;
                /// Address output restriction.
                18:14   outa_bits;
                /// Translation table concatenation enable, a 1-bit boolean flag.
                22:22   sl_concat_en => bool;
                /// Translation table memory attributes.
                25:24   ptw_memattr ?=> PtwMemattr;
                /// Translation table memory shareability.
                29:28   ptw_sh ?=> PtwShareability;
                /// Inner read allocation hint for translation table walks, a 1-bit boolean flag.
                30:30   r_allocate => bool;
                /// Disable hierarchical access permissions.
                33:33   disable_hier_ap => bool;
                /// Disable access fault checking.
                34:34   disable_af_fault => bool;
                /// Disable execution on all writable pages.
                35:35   wxn => bool;
                /// Enable execution on readable pages.
                36:36   xreadable => bool;
                /// Page-based hardware attributes for translation table walks.
                63:60   ptw_pbha;
            }

            // TRANSCFG is a logical 64-bit register, but it is laid out in hardware as two
            // 32-bit halves. Define it as separate low/high u32 registers so accesses match
            // the MMIO register layout and do not rely on native 64-bit MMIO transactions.
            pub(crate) TRANSCFG_LO(u32)[MAX_AS, stride = STRIDE] @ 0x2430 {
                31:0 value;
            }

            pub(crate) TRANSCFG_HI(u32)[MAX_AS, stride = STRIDE] @ 0x2434 {
                31:0 value;
            }

            /// Extra fault information for each address space. Read only.
            pub(crate) FAULTEXTRA_LO(u32)[MAX_AS, stride = STRIDE] @ 0x2438 {
                31:0    value;
            }

            pub(crate) FAULTEXTRA_HI(u32)[MAX_AS, stride = STRIDE] @ 0x243c {
                31:0    value;
            }
        }
    }
}

/// This module corresponds to the DOORBELL_BLOCK_n[0-63] register pages.
pub(crate) mod doorbell_block {
    use kernel::register;

    /// Number of doorbells available.
    pub(crate) const NUM_DOORBELLS: usize = 64;

    /// Doorbell block stride (64KiB).
    ///
    /// Each block occupies a full page, allowing it to be mapped
    /// separately into a virtual address space.
    const STRIDE: usize = 0x10000;

    register! {
        /// Doorbell request register. Write-only.
        pub(crate) DOORBELL(u32)[NUM_DOORBELLS, stride = STRIDE] @ 0x80000 {
            /// Doorbell set. Writing 1 triggers the doorbell.
            0:0    ring => bool;
        }
    }
}
