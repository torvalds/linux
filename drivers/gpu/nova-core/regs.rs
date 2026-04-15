// SPDX-License-Identifier: GPL-2.0

use kernel::{
    io::{
        register,
        register::WithBase,
        Io, //
    },
    prelude::*,
    time, //
};

use crate::{
    driver::Bar0,
    falcon::{
        DmaTrfCmdSize,
        FalconCoreRev,
        FalconCoreRevSubversion,
        FalconEngine,
        FalconFbifMemType,
        FalconFbifTarget,
        FalconMem,
        FalconModSelAlgo,
        FalconSecurityModel,
        PFalcon2Base,
        PFalconBase,
        PeregrineCoreSelect, //
    },
    gpu::{
        Architecture,
        Chipset, //
    },
    num::FromSafeCast,
};

// PMC

register! {
    /// Basic revision information about the GPU.
    pub(crate) NV_PMC_BOOT_0(u32) @ 0x00000000 {
        /// Lower bits of the architecture.
        28:24   architecture_0;
        /// Implementation version of the architecture.
        23:20   implementation;
        /// MSB of the architecture.
        8:8     architecture_1;
        /// Major revision of the chip.
        7:4     major_revision;
        /// Minor revision of the chip.
        3:0     minor_revision;
    }

    /// Extended architecture information.
    pub(crate) NV_PMC_BOOT_42(u32) @ 0x00000a00 {
        /// Architecture value.
        29:24   architecture ?=> Architecture;
        /// Implementation version of the architecture.
        23:20   implementation;
        /// Major revision of the chip.
        19:16   major_revision;
        /// Minor revision of the chip.
        15:12   minor_revision;
    }
}

impl NV_PMC_BOOT_0 {
    pub(crate) fn is_older_than_fermi(self) -> bool {
        // From https://github.com/NVIDIA/open-gpu-doc/tree/master/manuals :
        const NV_PMC_BOOT_0_ARCHITECTURE_GF100: u32 = 0xc;

        // Older chips left arch1 zeroed out. That, combined with an arch0 value that is less than
        // GF100, means "older than Fermi".
        self.architecture_1() == 0 && self.architecture_0() < NV_PMC_BOOT_0_ARCHITECTURE_GF100
    }
}

impl NV_PMC_BOOT_42 {
    /// Combines `architecture` and `implementation` to obtain a code unique to the chipset.
    pub(crate) fn chipset(self) -> Result<Chipset> {
        self.architecture()
            .map(|arch| {
                ((arch as u32) << Self::IMPLEMENTATION_RANGE.len())
                    | u32::from(self.implementation())
            })
            .and_then(Chipset::try_from)
    }

    /// Returns the raw architecture value from the register.
    fn architecture_raw(self) -> u8 {
        ((self.into_raw() >> Self::ARCHITECTURE_RANGE.start())
            & ((1 << Self::ARCHITECTURE_RANGE.len()) - 1)) as u8
    }
}

impl kernel::fmt::Display for NV_PMC_BOOT_42 {
    fn fmt(&self, f: &mut kernel::fmt::Formatter<'_>) -> kernel::fmt::Result {
        write!(
            f,
            "boot42 = 0x{:08x} (architecture 0x{:x}, implementation 0x{:x})",
            self.inner,
            self.architecture_raw(),
            self.implementation()
        )
    }
}

// PBUS

register! {
    pub(crate) NV_PBUS_SW_SCRATCH(u32)[64] @ 0x00001400 {}

    /// Scratch register 0xe used as FRTS firmware error code.
    pub(crate) NV_PBUS_SW_SCRATCH_0E_FRTS_ERR(u32) => NV_PBUS_SW_SCRATCH[0xe] {
        31:16   frts_err_code;
    }
}

// PFB

register! {
    /// Low bits of the physical system memory address used by the GPU to perform sysmembar
    /// operations (see [`crate::fb::SysmemFlush`]).
    pub(crate) NV_PFB_NISO_FLUSH_SYSMEM_ADDR(u32) @ 0x00100c10 {
        31:0    adr_39_08;
    }

    /// High bits of the physical system memory address used by the GPU to perform sysmembar
    /// operations (see [`crate::fb::SysmemFlush`]).
    pub(crate) NV_PFB_NISO_FLUSH_SYSMEM_ADDR_HI(u32) @ 0x00100c40 {
        23:0    adr_63_40;
    }

    pub(crate) NV_PFB_PRI_MMU_LOCAL_MEMORY_RANGE(u32) @ 0x00100ce0 {
        30:30   ecc_mode_enabled => bool;
        9:4     lower_mag;
        3:0     lower_scale;
    }

    pub(crate) NV_PFB_PRI_MMU_WPR2_ADDR_LO(u32) @ 0x001fa824 {
        /// Bits 12..40 of the lower (inclusive) bound of the WPR2 region.
        31:4    lo_val;
    }

    pub(crate) NV_PFB_PRI_MMU_WPR2_ADDR_HI(u32) @ 0x001fa828 {
        /// Bits 12..40 of the higher (exclusive) bound of the WPR2 region.
        31:4    hi_val;
    }
}

impl NV_PFB_PRI_MMU_LOCAL_MEMORY_RANGE {
    /// Returns the usable framebuffer size, in bytes.
    pub(crate) fn usable_fb_size(self) -> u64 {
        let size = (u64::from(self.lower_mag()) << u64::from(self.lower_scale()))
            * u64::from_safe_cast(kernel::sizes::SZ_1M);

        if self.ecc_mode_enabled() {
            // Remove the amount of memory reserved for ECC (one per 16 units).
            size / 16 * 15
        } else {
            size
        }
    }
}

impl NV_PFB_PRI_MMU_WPR2_ADDR_LO {
    /// Returns the lower (inclusive) bound of the WPR2 region.
    pub(crate) fn lower_bound(self) -> u64 {
        u64::from(self.lo_val()) << 12
    }
}

impl NV_PFB_PRI_MMU_WPR2_ADDR_HI {
    /// Returns the higher (exclusive) bound of the WPR2 region.
    ///
    /// A value of zero means the WPR2 region is not set.
    pub(crate) fn higher_bound(self) -> u64 {
        u64::from(self.hi_val()) << 12
    }
}

// PGSP

register! {
    pub(crate) NV_PGSP_QUEUE_HEAD(u32) @ 0x00110c00 {
        31:0    address;
    }
}

// PGC6 register space.
//
// `GC6` is a GPU low-power state where VRAM is in self-refresh and the GPU is powered down (except
// for power rails needed to keep self-refresh working and important registers and hardware
// blocks).
//
// These scratch registers remain powered on even in a low-power state and have a designated group
// number.

register! {
    /// Boot Sequence Interface (BSI) register used to determine
    /// if GSP reload/resume has completed during the boot process.
    pub(crate) NV_PGC6_BSI_SECURE_SCRATCH_14(u32) @ 0x001180f8 {
        26:26   boot_stage_3_handoff => bool;
    }

    /// Privilege level mask register. It dictates whether the host CPU has privilege to access the
    /// `PGC6_AON_SECURE_SCRATCH_GROUP_05` register (which it needs to read GFW_BOOT).
    pub(crate) NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_PRIV_LEVEL_MASK(u32) @ 0x00118128 {
        /// Set after FWSEC lowers its protection level.
        0:0     read_protection_level0 => bool;
    }

    /// OpenRM defines this as a register array, but doesn't specify its size and only uses its
    /// first element. Be conservative until we know the actual size or need to use more registers.
    pub(crate) NV_PGC6_AON_SECURE_SCRATCH_GROUP_05(u32)[1] @ 0x00118234 {}

    /// Scratch group 05 register 0 used as GFW boot progress indicator.
    pub(crate) NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_0_GFW_BOOT(u32)
        => NV_PGC6_AON_SECURE_SCRATCH_GROUP_05[0] {
        /// Progress of GFW boot (0xff means completed).
        7:0    progress;
    }

    pub(crate) NV_PGC6_AON_SECURE_SCRATCH_GROUP_42(u32) @ 0x001183a4 {
        31:0    value;
    }

    /// Scratch group 42 register used as framebuffer size.
    pub(crate) NV_USABLE_FB_SIZE_IN_MB(u32) => NV_PGC6_AON_SECURE_SCRATCH_GROUP_42 {
        /// Usable framebuffer size, in megabytes.
        31:0    value;
    }
}

impl NV_PGC6_AON_SECURE_SCRATCH_GROUP_05_0_GFW_BOOT {
    /// Returns `true` if GFW boot is completed.
    pub(crate) fn completed(self) -> bool {
        self.progress() == 0xff
    }
}

impl NV_USABLE_FB_SIZE_IN_MB {
    /// Returns the usable framebuffer size, in bytes.
    pub(crate) fn usable_fb_size(self) -> u64 {
        u64::from(self.value()) * u64::from_safe_cast(kernel::sizes::SZ_1M)
    }
}

// PDISP

register! {
    pub(crate) NV_PDISP_VGA_WORKSPACE_BASE(u32) @ 0x00625f04 {
        /// VGA workspace base address divided by 0x10000.
        31:8    addr;
        /// Set if the `addr` field is valid.
        3:3     status_valid => bool;
    }
}

impl NV_PDISP_VGA_WORKSPACE_BASE {
    /// Returns the base address of the VGA workspace, or `None` if none exists.
    pub(crate) fn vga_workspace_addr(self) -> Option<u64> {
        if self.status_valid() {
            Some(u64::from(self.addr()) << 16)
        } else {
            None
        }
    }
}

// FUSE

pub(crate) const NV_FUSE_OPT_FPF_SIZE: usize = 16;

register! {
    pub(crate) NV_FUSE_OPT_FPF_NVDEC_UCODE1_VERSION(u32)[NV_FUSE_OPT_FPF_SIZE] @ 0x00824100 {
        15:0    data => u16;
    }

    pub(crate) NV_FUSE_OPT_FPF_SEC2_UCODE1_VERSION(u32)[NV_FUSE_OPT_FPF_SIZE] @ 0x00824140 {
        15:0    data => u16;
    }

    pub(crate) NV_FUSE_OPT_FPF_GSP_UCODE1_VERSION(u32)[NV_FUSE_OPT_FPF_SIZE] @ 0x008241c0 {
        15:0    data => u16;
    }
}

// PFALCON

register! {
    pub(crate) NV_PFALCON_FALCON_IRQSCLR(u32) @ PFalconBase + 0x00000004 {
        6:6     swgen0 => bool;
        4:4     halt => bool;
    }

    pub(crate) NV_PFALCON_FALCON_MAILBOX0(u32) @ PFalconBase + 0x00000040 {
        31:0    value => u32;
    }

    pub(crate) NV_PFALCON_FALCON_MAILBOX1(u32) @ PFalconBase + 0x00000044 {
        31:0    value => u32;
    }

    /// Used to store version information about the firmware running
    /// on the Falcon processor.
    pub(crate) NV_PFALCON_FALCON_OS(u32) @ PFalconBase + 0x00000080 {
        31:0    value => u32;
    }

    pub(crate) NV_PFALCON_FALCON_RM(u32) @ PFalconBase + 0x00000084 {
        31:0    value => u32;
    }

    pub(crate) NV_PFALCON_FALCON_HWCFG2(u32) @ PFalconBase + 0x000000f4 {
        /// Signal indicating that reset is completed (GA102+).
        31:31   reset_ready => bool;
        /// Set to 0 after memory scrubbing is completed.
        12:12   mem_scrubbing => bool;
        10:10   riscv => bool;
    }

    pub(crate) NV_PFALCON_FALCON_CPUCTL(u32) @ PFalconBase + 0x00000100 {
        6:6     alias_en => bool;
        4:4     halted => bool;
        1:1     startcpu => bool;
    }

    pub(crate) NV_PFALCON_FALCON_BOOTVEC(u32) @ PFalconBase + 0x00000104 {
        31:0    value => u32;
    }

    pub(crate) NV_PFALCON_FALCON_DMACTL(u32) @ PFalconBase + 0x0000010c {
        7:7     secure_stat => bool;
        6:3     dmaq_num;
        2:2     imem_scrubbing => bool;
        1:1     dmem_scrubbing => bool;
        0:0     require_ctx => bool;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFBASE(u32) @ PFalconBase + 0x00000110 {
        31:0    base => u32;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFMOFFS(u32) @ PFalconBase + 0x00000114 {
        23:0    offs;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFCMD(u32) @ PFalconBase + 0x00000118 {
        16:16   set_dmtag;
        14:12   ctxdma;
        10:8    size ?=> DmaTrfCmdSize;
        5:5     is_write => bool;
        4:4     imem => bool;
        3:2     sec;
        1:1     idle => bool;
        0:0     full => bool;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFFBOFFS(u32) @ PFalconBase + 0x0000011c {
        31:0    offs => u32;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFBASE1(u32) @ PFalconBase + 0x00000128 {
        8:0     base;
    }

    pub(crate) NV_PFALCON_FALCON_HWCFG1(u32) @ PFalconBase + 0x0000012c {
        /// Core revision subversion.
        7:6     core_rev_subversion => FalconCoreRevSubversion;
        /// Security model.
        5:4     security_model ?=> FalconSecurityModel;
        /// Core revision.
        3:0     core_rev ?=> FalconCoreRev;
    }

    pub(crate) NV_PFALCON_FALCON_CPUCTL_ALIAS(u32) @ PFalconBase + 0x00000130 {
        1:1     startcpu => bool;
    }

    /// IMEM access control register. Up to 4 ports are available for IMEM access.
    pub(crate) NV_PFALCON_FALCON_IMEMC(u32)[4, stride = 16] @ PFalconBase + 0x00000180 {
        /// Access secure IMEM.
        28:28     secure => bool;
        /// Auto-increment on write.
        24:24     aincw => bool;
        /// IMEM block and word offset.
        15:0      offs;
    }

    /// IMEM data register. Reading/writing this register accesses IMEM at the address
    /// specified by the corresponding IMEMC register.
    pub(crate) NV_PFALCON_FALCON_IMEMD(u32)[4, stride = 16] @ PFalconBase + 0x00000184 {
        31:0      data;
    }

    /// IMEM tag register. Used to set the tag for the current IMEM block.
    pub(crate) NV_PFALCON_FALCON_IMEMT(u32)[4, stride = 16] @ PFalconBase + 0x00000188 {
        15:0      tag;
    }

    /// DMEM access control register. Up to 8 ports are available for DMEM access.
    pub(crate) NV_PFALCON_FALCON_DMEMC(u32)[8, stride = 8] @ PFalconBase + 0x000001c0 {
        /// Auto-increment on write.
        24:24     aincw => bool;
        /// DMEM block and word offset.
        15:0      offs;
    }

    /// DMEM data register. Reading/writing this register accesses DMEM at the address
    /// specified by the corresponding DMEMC register.
    pub(crate) NV_PFALCON_FALCON_DMEMD(u32)[8, stride = 8] @ PFalconBase + 0x000001c4 {
        31:0      data;
    }

    /// Actually known as `NV_PSEC_FALCON_ENGINE` and `NV_PGSP_FALCON_ENGINE` depending on the
    /// falcon instance.
    pub(crate) NV_PFALCON_FALCON_ENGINE(u32) @ PFalconBase + 0x000003c0 {
        0:0     reset => bool;
    }

    pub(crate) NV_PFALCON_FBIF_TRANSCFG(u32)[8] @ PFalconBase + 0x00000600 {
        2:2     mem_type => FalconFbifMemType;
        1:0     target ?=> FalconFbifTarget;
    }

    pub(crate) NV_PFALCON_FBIF_CTL(u32) @ PFalconBase + 0x00000624 {
        7:7     allow_phys_no_ctx => bool;
    }
}

impl NV_PFALCON_FALCON_DMACTL {
    /// Returns `true` if memory scrubbing is completed.
    pub(crate) fn mem_scrubbing_done(self) -> bool {
        !self.dmem_scrubbing() && !self.imem_scrubbing()
    }
}

impl NV_PFALCON_FALCON_DMATRFCMD {
    /// Programs the `imem` and `sec` fields for the given FalconMem
    pub(crate) fn with_falcon_mem(self, mem: FalconMem) -> Self {
        let this = self.with_imem(mem != FalconMem::Dmem);

        match mem {
            FalconMem::ImemSecure => this.with_const_sec::<1>(),
            _ => this.with_const_sec::<0>(),
        }
    }
}

impl NV_PFALCON_FALCON_ENGINE {
    /// Resets the falcon
    pub(crate) fn reset_engine<E: FalconEngine>(bar: &Bar0) {
        bar.update(Self::of::<E>(), |r| r.with_reset(true));

        // TIMEOUT: falcon engine should not take more than 10us to reset.
        time::delay::fsleep(time::Delta::from_micros(10));

        bar.update(Self::of::<E>(), |r| r.with_reset(false));
    }
}

impl NV_PFALCON_FALCON_HWCFG2 {
    /// Returns `true` if memory scrubbing is completed.
    pub(crate) fn mem_scrubbing_done(self) -> bool {
        !self.mem_scrubbing()
    }
}

/* PFALCON2 */

register! {
    pub(crate) NV_PFALCON2_FALCON_MOD_SEL(u32) @ PFalcon2Base + 0x00000180 {
        7:0     algo ?=> FalconModSelAlgo;
    }

    pub(crate) NV_PFALCON2_FALCON_BROM_CURR_UCODE_ID(u32) @ PFalcon2Base + 0x00000198 {
        7:0    ucode_id => u8;
    }

    pub(crate) NV_PFALCON2_FALCON_BROM_ENGIDMASK(u32) @ PFalcon2Base + 0x0000019c {
        31:0    value => u32;
    }

    /// OpenRM defines this as a register array, but doesn't specify its size and only uses its
    /// first element. Be conservative until we know the actual size or need to use more registers.
    pub(crate) NV_PFALCON2_FALCON_BROM_PARAADDR(u32)[1] @ PFalcon2Base + 0x00000210 {
        31:0    value => u32;
    }
}

// PRISCV

register! {
    /// RISC-V status register for debug (Turing and GA100 only).
    /// Reflects current RISC-V core status.
    pub(crate) NV_PRISCV_RISCV_CORE_SWITCH_RISCV_STATUS(u32) @ PFalcon2Base + 0x00000240 {
        /// RISC-V core active/inactive status.
        0:0     active_stat => bool;
    }

    /// GA102 and later.
    pub(crate) NV_PRISCV_RISCV_CPUCTL(u32) @ PFalcon2Base + 0x00000388 {
        7:7     active_stat => bool;
        0:0     halted => bool;
    }

    /// GA102 and later.
    pub(crate) NV_PRISCV_RISCV_BCR_CTRL(u32) @ PFalcon2Base + 0x00000668 {
        8:8     br_fetch => bool;
        4:4     core_select => PeregrineCoreSelect;
        0:0     valid => bool;
    }
}

// The modules below provide registers that are not identical on all supported chips. They should
// only be used in HAL modules.

pub(crate) mod gm107 {
    use kernel::io::register;

    // FUSE

    register! {
        pub(crate) NV_FUSE_STATUS_OPT_DISPLAY(u32) @ 0x00021c04 {
            0:0     display_disabled => bool;
        }
    }
}

pub(crate) mod ga100 {
    use kernel::io::register;

    // FUSE

    register! {
        pub(crate) NV_FUSE_STATUS_OPT_DISPLAY(u32) @ 0x00820c04 {
            0:0     display_disabled => bool;
        }
    }
}
