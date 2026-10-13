// SPDX-License-Identifier: GPL-2.0
// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.

use kernel::{
    io::{
        register,
        Io,
        Mmio, //
    },
    sizes::SizeConstants,
    time, //
};
use pin_init::Zeroable;

use crate::{
    driver::NovaRegisters,
    falcon::{
        DmaTrfCmdSize,
        FalconCoreRev,
        FalconCoreRevSubversion,
        FalconFbifMemType,
        FalconFbifTarget,
        FalconMem,
        FalconModSelAlgo,
        FalconSecurityModel,
        PFalcon2Registers,
        PFalconRegisters,
        PeregrineCoreSelect, //
    },
    mm::tlb::TlbAckMode, //
};

// PBUS

register! {
    base: NovaRegisters;

    pub(crate) NV_PBUS_SW_SCRATCH(u32)[64] @ 0x00001400 {}
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
    base: NovaRegisters;

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
        u64::from(self.value()) * u64::SZ_1M
    }
}

// FUSE

pub(crate) const NV_FUSE_OPT_FPF_SIZE: usize = 16;

register! {
    base: NovaRegisters;

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
    base: PFalconRegisters;

    /// Clears the latch of every cause whose bit is written as `1`. Write-only.
    ///
    /// The write ends the latch and not the source, so a cause driven from outside the falcon
    /// stays set. "Retriggering a falcon" in `Documentation/gpu/nova/core/interrupts.rst` names
    /// those causes.
    pub(crate) NV_PFALCON_FALCON_IRQSCLR(u32) @ 0x00000004 {
        6:6     swgen0 => bool;
        4:4     halt => bool;
    }

    /// Interrupt causes latched in the falcon, one bit per cause, whichever target each is routed
    /// to.
    ///
    /// The causes routed to the host are the ones also set in `NV_PRISCV_RISCV_IRQMASK` and
    /// `NV_PRISCV_RISCV_IRQDEST`.
    pub(crate) NV_PFALCON_FALCON_IRQSTAT(u32) @ 0x00000008 {
        6:6     swgen0 => bool;
    }

    pub(crate) NV_PFALCON_FALCON_MAILBOX0(u32) @ 0x00000040 {
        31:0    value => u32;
    }

    pub(crate) NV_PFALCON_FALCON_MAILBOX1(u32) @ 0x00000044 {
        31:0    value => u32;
    }

    /// Used to store version information about the firmware running
    /// on the Falcon processor.
    pub(crate) NV_PFALCON_FALCON_OS(u32) @ 0x00000080 {
        31:0    value => u32;
    }

    pub(crate) NV_PFALCON_FALCON_RM(u32) @ 0x00000084 {
        31:0    value => u32;
    }

    pub(crate) NV_PFALCON_FALCON_HWCFG2(u32) @ 0x000000f4 {
        /// Signal indicating that reset is completed (GA102+).
        31:31   reset_ready => bool;
        /// RISC-V branch privilege lockdown bit.
        13:13   riscv_br_priv_lockdown => bool;
        /// Set to 0 after memory scrubbing is completed.
        12:12   mem_scrubbing => bool;
        10:10   riscv => bool;
    }

    pub(crate) NV_PFALCON_FALCON_CPUCTL(u32) @ 0x00000100 {
        6:6     alias_en => bool;
        4:4     halted => bool;
        1:1     startcpu => bool;
    }

    pub(crate) NV_PFALCON_FALCON_BOOTVEC(u32) @ 0x00000104 {
        31:0    value => u32;
    }

    pub(crate) NV_PFALCON_FALCON_DMACTL(u32) @ 0x0000010c {
        7:7     secure_stat => bool;
        6:3     dmaq_num;
        2:2     imem_scrubbing => bool;
        1:1     dmem_scrubbing => bool;
        0:0     require_ctx => bool;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFBASE(u32) @ 0x00000110 {
        31:0    base => u32;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFMOFFS(u32) @ 0x00000114 {
        23:0    offs;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFCMD(u32) @ 0x00000118 {
        16:16   set_dmtag;
        14:12   ctxdma;
        10:8    size ?=> DmaTrfCmdSize;
        5:5     is_write => bool;
        4:4     imem => bool;
        3:2     sec;
        1:1     idle => bool;
        0:0     full => bool;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFFBOFFS(u32) @ 0x0000011c {
        31:0    offs => u32;
    }

    pub(crate) NV_PFALCON_FALCON_DMATRFBASE1(u32) @ 0x00000128 {
        8:0     base;
    }

    pub(crate) NV_PFALCON_FALCON_HWCFG1(u32) @ 0x0000012c {
        /// Core revision subversion.
        7:6     core_rev_subversion => FalconCoreRevSubversion;
        /// Security model.
        5:4     security_model ?=> FalconSecurityModel;
        /// Core revision.
        3:0     core_rev ?=> FalconCoreRev;
    }

    pub(crate) NV_PFALCON_FALCON_CPUCTL_ALIAS(u32) @ 0x00000130 {
        1:1     startcpu => bool;
    }

    /// IMEM access control register. Up to 4 ports are available for IMEM access.
    pub(crate) NV_PFALCON_FALCON_IMEMC(u32)[4, stride = 16] @ 0x00000180 {
        /// Access secure IMEM.
        28:28     secure => bool;
        /// Auto-increment on write.
        24:24     aincw => bool;
        /// IMEM block and word offset.
        15:0      offs;
    }

    /// IMEM data register. Reading/writing this register accesses IMEM at the address
    /// specified by the corresponding IMEMC register.
    pub(crate) NV_PFALCON_FALCON_IMEMD(u32)[4, stride = 16] @ 0x00000184 {
        31:0      data;
    }

    /// IMEM tag register. Used to set the tag for the current IMEM block.
    pub(crate) NV_PFALCON_FALCON_IMEMT(u32)[4, stride = 16] @ 0x00000188 {
        15:0      tag;
    }

    /// DMEM access control register. Up to 8 ports are available for DMEM access.
    pub(crate) NV_PFALCON_FALCON_DMEMC(u32)[8, stride = 8] @ 0x000001c0 {
        /// Auto-increment on write.
        24:24     aincw => bool;
        /// DMEM block and word offset.
        15:0      offs;
    }

    /// DMEM data register. Reading/writing this register accesses DMEM at the address
    /// specified by the corresponding DMEMC register.
    pub(crate) NV_PFALCON_FALCON_DMEMD(u32)[8, stride = 8] @ 0x000001c4 {
        31:0      data;
    }

    /// Actually known as `NV_PSEC_FALCON_ENGINE` and `NV_PGSP_FALCON_ENGINE` depending on the
    /// falcon instance.
    pub(crate) NV_PFALCON_FALCON_ENGINE(u32) @ 0x000003c0 {
        0:0     reset => bool;
    }

    /// Makes the falcon re-emit its host-routed causes into the interrupt tree. Write-only.
    ///
    /// Present from GA100 on. See "Retriggering a falcon" in
    /// `Documentation/gpu/nova/core/interrupts.rst`.
    ///
    /// The hardware headers declare two elements, and OpenRM writes only the first.
    pub(crate) NV_PFALCON_FALCON_INTR_RETRIGGER(u32)[2] @ 0x000003e8 {
        0:0     trigger => bool;
    }

    pub(crate) NV_PFALCON_FBIF_TRANSCFG(u32)[8] @ 0x00000600 {
        2:2     mem_type => FalconFbifMemType;
        1:0     target ?=> FalconFbifTarget;
    }

    pub(crate) NV_PFALCON_FBIF_CTL(u32) @ 0x00000624 {
        7:7     allow_phys_no_ctx => bool;
    }

    // Falcon EMEM PIO registers (used by FSP on Hopper/Blackwell).
    // These provide the falcon external memory communication interface.

    pub(crate) NV_PFALCON_FALCON_EMEMC(u32) @ 0x00000ac0 {
        /// EMEM byte offset (4-byte aligned) within the block.
        7:2     offs;
        /// EMEM block to access.
        15:8    blk;
        /// Auto-increment the offset after each write.
        24:24   aincw => bool;
        /// Auto-increment the offset after each read.
        25:25   aincr => bool;
    }

    pub(crate) NV_PFALCON_FALCON_EMEMD(u32) @ 0x00000ac4 {
        31:0    data => u32;
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
    pub(crate) fn reset_engine(pfalcon: Mmio<'_, PFalconRegisters>) {
        pfalcon.update(NV_PFALCON_FALCON_ENGINE, |r| r.with_reset(true));

        // TIMEOUT: falcon engine should not take more than 10us to reset.
        time::delay::fsleep(time::Delta::from_micros(10));

        pfalcon.update(NV_PFALCON_FALCON_ENGINE, |r| r.with_reset(false));
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
    base: PFalcon2Registers;

    pub(crate) NV_PFALCON2_FALCON_MOD_SEL(u32) @ 0x00000180 {
        7:0     algo ?=> FalconModSelAlgo;
    }

    pub(crate) NV_PFALCON2_FALCON_BROM_CURR_UCODE_ID(u32) @ 0x00000198 {
        7:0    ucode_id => u8;
    }

    pub(crate) NV_PFALCON2_FALCON_BROM_ENGIDMASK(u32) @ 0x0000019c {
        31:0    value => u32;
    }

    /// OpenRM defines this as a register array, but doesn't specify its size and only uses its
    /// first element. Be conservative until we know the actual size or need to use more registers.
    pub(crate) NV_PFALCON2_FALCON_BROM_PARAADDR(u32)[1] @ 0x00000210 {
        31:0    value => u32;
    }
}

// PRISCV

register! {
    base: PFalcon2Registers;

    /// RISC-V status register for debug (Turing and GA100 only).
    /// Reflects current RISC-V core status.
    pub(crate) NV_PRISCV_RISCV_CORE_SWITCH_RISCV_STATUS(u32) @ 0x00000240 {
        /// RISC-V core active/inactive status.
        0:0     active_stat => bool;
    }

    /// GA102 and later.
    pub(crate) NV_PRISCV_RISCV_CPUCTL(u32) @ 0x00000388 {
        7:7     active_stat => bool;
        4:4     halted => bool;
    }

    /// GA102 and later.
    pub(crate) NV_PRISCV_RISCV_BCR_CTRL(u32) @ 0x00000668 {
        8:8     br_fetch => bool;
        4:4     core_select => PeregrineCoreSelect;
        0:0     valid => bool;
    }
}

// FSP (Foundation Security Processor) queue registers for Hopper/Blackwell Chain of Trust.
// These registers manage falcon EMEM communication queues.

register! {
    base: NovaRegisters;

    pub(crate) NV_PFSP_QUEUE_HEAD(u32)[8] @ 0x008f2c00 {
        31:0    address => u32;
    }

    pub(crate) NV_PFSP_QUEUE_TAIL(u32)[8] @ 0x008f2c04 {
        31:0    address => u32;
    }

    pub(crate) NV_PFSP_MSGQ_HEAD(u32)[8] @ 0x008f2c80 {
        31:0    val => u32;
    }

    pub(crate) NV_PFSP_MSGQ_TAIL(u32)[8] @ 0x008f2c84 {
        31:0    val => u32;
    }
}

// The modules below provide registers that are not identical on all supported chips. They should
// only be used in HAL modules.

pub(crate) mod gm107 {
    use kernel::io::register;

    use crate::driver::NovaRegisters;

    // FUSE

    register! {
        base: NovaRegisters;

        pub(crate) NV_FUSE_STATUS_OPT_DISPLAY(u32) @ 0x00021c04 {
            0:0     display_disabled => bool;
        }
    }
}

pub(crate) mod tu102 {
    use kernel::io::register;

    use crate::falcon::PFalcon2Registers;

    // The RISC-V interrupt routing registers, at the offsets that Turing and GA100 use.

    register! {
        base: PFalcon2Registers;

        /// Enabled causes, one bit per cause. Read-only to the host.
        pub(crate) NV_PRISCV_RISCV_IRQMASK(u32) @ 0x000002b4 {
            31:0    value => u32;
        }

        /// Causes routed to the host, one bit per cause. A clear bit routes the cause to the
        /// RISC-V core.
        pub(crate) NV_PRISCV_RISCV_IRQDEST(u32) @ 0x000002b8 {
            31:0    value => u32;
        }
    }
}

pub(crate) mod ga100 {
    use kernel::io::register;

    use crate::driver::NovaRegisters;

    // FUSE

    register! {
        base: NovaRegisters;

        pub(crate) NV_FUSE_STATUS_OPT_DISPLAY(u32) @ 0x00820c04 {
            0:0     display_disabled => bool;
        }
    }
}

pub(crate) mod ga102 {
    use kernel::io::register;

    use crate::falcon::PFalcon2Registers;

    // The RISC-V interrupt routing registers, at the offsets that GA102 and later use.

    register! {
        base: PFalcon2Registers;

        /// Same as [`super::tu102::NV_PRISCV_RISCV_IRQMASK`], at the GA102 offset.
        pub(crate) NV_PRISCV_RISCV_IRQMASK(u32) @ 0x00000528 {
            31:0    value => u32;
        }

        /// Same as [`super::tu102::NV_PRISCV_RISCV_IRQDEST`], at the GA102 offset.
        pub(crate) NV_PRISCV_RISCV_IRQDEST(u32) @ 0x0000052c {
            31:0    value => u32;
        }
    }
}

pub(crate) const NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE_STATUS_SUCCESS: u32 = 0xff;

pub(crate) mod gh100 {
    use kernel::io::register;

    use crate::driver::NovaRegisters;

    // PTHERM

    register! {
        base: NovaRegisters;

        pub(crate) NV_THERM_I2CS_SCRATCH(u32) @ 0x000200bc {
            31:0    data;
        }

        // Alias to `NV_THERM_I2CS_SCRATCH` when used to check for FSP boot completion.
        pub(crate) NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE(u32) => NV_THERM_I2CS_SCRATCH {
            31:0    fsp_boot_complete;
        }
    }
}

pub(crate) mod gb202 {
    use kernel::io::register;

    use crate::driver::NovaRegisters;

    // PTHERM

    register! {
        base: NovaRegisters;

        pub(crate) NV_THERM_I2CS_SCRATCH(u32) @ 0x00ad00bc {
            31:0    data;
        }

        // Alias to `NV_THERM_I2CS_SCRATCH` when used to check for FSP boot completion.
        pub(crate) NV_THERM_I2CS_SCRATCH_FSP_BOOT_COMPLETE(u32) => NV_THERM_I2CS_SCRATCH {
            31:0    fsp_boot_complete;
        }
    }
}

// MMU TLB

register! {
    base: NovaRegisters;

    /// TLB flush register: PDB address lower bits.
    pub(crate) NV_TLB_FLUSH_PDB_LO(u32) @ 0x00b830a0 {
        /// PDB address bits [39:8].
        31:0    pdb_lo => u32;
    }

    /// TLB flush register: PDB address higher bits.
    pub(crate) NV_TLB_FLUSH_PDB_HI(u32) @ 0x00b830a4 {
        /// PDB address bits [47:40].
        7:0     pdb_hi => u8;
    }

    /// TLB flush control register.
    pub(crate) NV_TLB_FLUSH_CTRL(u32) @ 0x00b830b0 {
        /// Invalidate every VA in the PDB selected by `NV_TLB_FLUSH_PDB_LO/HI`.
        0:0     all_va => bool;
        /// Invalidate TLBs for all PDBs (ignores `NV_TLB_FLUSH_PDB_LO/HI`).
        1:1     all_pdb => bool;
        /// Restrict the flush to the HUB MMU's TLBs; skip broadcasting to the
        /// per-GPC L2 TLBs.
        ///
        /// The GPU MMU has a two-level TLB hierarchy:
        /// 1. The *HUB MMU* sits at the top and serves memory requests from
        ///    "host-side" engines: the host/channel interface, copy engines,
        ///    display, and BAR1/BAR2 accesses.
        /// 2. Each GPC (Graphics Processing Cluster — the block that houses
        ///    shader cores / SMs) has its own L2 TLB that serves requests from
        ///    the compute and graphics engines inside the cluster.
        ///
        /// When set, only the HUB TLBs are invalidated. This is a performance
        /// optimization for flushes that only affect HUB-side mappings (e.g.
        /// BAR1/BAR2 windows), where fanning the invalidation out to every
        /// GPC's L2 TLB would be wasted work. Must be false when flushing
        /// mappings that may be cached by compute/graphics engines.
        2:2     hubtlb_only => bool;
        /// Invalidation acknowledgment scope. See [`TlbAckMode`] for details.
        8:7     ack ?=> TlbAckMode;
        /// Write 1 to kick off the flush. Hardware clears this bit when the
        /// flush completes; reads as 1 while the flush is in progress.
        31:31   trigger => bool;
    }
}

impl NV_TLB_FLUSH_PDB_LO {
    /// Create a register value from a PDB address.
    ///
    /// Extracts bits [39:8] of the address and shifts it right by 8 bits.
    pub(crate) fn from_pdb_addr(addr: u64) -> Self {
        Self::zeroed().with_pdb_lo(((addr >> 8) & 0xFFFF_FFFF) as u32)
    }
}

impl NV_TLB_FLUSH_PDB_HI {
    /// Create a register value from a PDB address.
    ///
    /// Extracts bits [47:40] of the address and shifts it right by 40 bits.
    pub(crate) fn from_pdb_addr(addr: u64) -> Self {
        Self::zeroed().with_pdb_hi(((addr >> 40) & 0xFF) as u8)
    }
}
