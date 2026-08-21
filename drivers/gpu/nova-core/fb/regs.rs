// SPDX-License-Identifier: GPL-2.0

use kernel::{
    io::register,
    sizes::SizeConstants, //
};

// PDISP

register! {
    pub(super) NV_PDISP_VGA_WORKSPACE_BASE(u32) @ 0x00625f04 {
        /// VGA workspace base address divided by 0x10000.
        31:8    addr;
        /// Set if the `addr` field is valid.
        3:3     status_valid => bool;
    }
}

impl NV_PDISP_VGA_WORKSPACE_BASE {
    /// Returns the base address of the VGA workspace, or `None` if none exists.
    pub(super) fn vga_workspace_addr(self) -> Option<u64> {
        if self.status_valid() {
            Some(u64::from(self.addr()) << 16)
        } else {
            None
        }
    }
}

// PFB

register! {
    /// Low bits of the physical system memory address used by the GPU to perform sysmembar
    /// operations (see [`crate::fb::SysmemFlush`]).
    pub(super) NV_PFB_NISO_FLUSH_SYSMEM_ADDR(u32) @ 0x00100c10 {
        31:0    adr_39_08;
    }

    /// High bits of the physical system memory address used by the GPU to perform sysmembar
    /// operations.
    pub(super) NV_PFB_NISO_FLUSH_SYSMEM_ADDR_HI(u32) @ 0x00100c40 {
        23:0    adr_63_40;
    }

    pub(super) NV_PFB_PRI_MMU_LOCAL_MEMORY_RANGE(u32) @ 0x00100ce0 {
        30:30   ecc_mode_enabled => bool;
        9:4     lower_mag;
        3:0     lower_scale;
    }

    pub(super) NV_PFB_PRI_MMU_WPR2_ADDR_LO(u32) @ 0x001fa824 {
        /// Bits 12..40 of the lower (inclusive) bound of the WPR2 region.
        31:4    lo_val;
    }

    pub(super) NV_PFB_PRI_MMU_WPR2_ADDR_HI(u32) @ 0x001fa828 {
        /// Bits 12..40 of the higher (exclusive) bound of the WPR2 region.
        31:4    hi_val;
    }
}

/// Base of the GB10x HSHUB0 register window (`NV_HSHUB0_PRIV_BASE` in Open RM).
///
/// The base is provided by the GB10x framebuffer HAL.
pub(super) struct Hshub0Base(());

register! {
    // GB10x sysmem flush registers, relative to the HSHUB0 base. GB10x routes sysmembar
    // through a primary and an EG (egress) pair that must both be programmed to the same
    // address. Hardware ignores bits 7:0 of each LO register. The boot path uses a fixed
    // HSHUB0 base, so the multiple runtime-discovered HSHUB bases are not needed here.
    pub(super) NV_PFB_HSHUB_PCIE_FLUSH_SYSMEM_ADDR_LO(u32) @ Hshub0Base + 0x00000e50 {
        31:0    adr => u32;
    }

    pub(super) NV_PFB_HSHUB_PCIE_FLUSH_SYSMEM_ADDR_HI(u32) @ Hshub0Base + 0x00000e54 {
        19:0    adr;
    }

    pub(super) NV_PFB_HSHUB_EG_PCIE_FLUSH_SYSMEM_ADDR_LO(u32) @ Hshub0Base + 0x000006c0 {
        31:0    adr => u32;
    }

    pub(super) NV_PFB_HSHUB_EG_PCIE_FLUSH_SYSMEM_ADDR_HI(u32) @ Hshub0Base + 0x000006c4 {
        19:0    adr;
    }
}

register! {
    // GB20x FBHUB0 sysmem flush registers. Unlike the older
    // NV_PFB_NISO_FLUSH_SYSMEM_ADDR registers, which encode the address with an
    // 8-bit right-shift, these take the raw address split into lower and upper
    // halves. Hardware ignores bits 7:0 of the LO register.
    pub(super) NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_LO(u32) @ 0x008a1d58 {
        31:0    adr => u32;
    }

    pub(super) NV_PFB_FBHUB0_PCIE_FLUSH_SYSMEM_ADDR_HI(u32) @ 0x008a1d5c {
        19:0    adr;
    }
}

register! {
    /// Low bits of the physical system memory address used by the GPU to perform
    /// sysmembar operations on Hopper.
    ///
    /// Like the GB20x FBHUB0 registers, and unlike the Ampere
    /// `NV_PFB_NISO_FLUSH_SYSMEM_ADDR` registers (which encode the address with an
    /// 8-bit right-shift), these take the raw address split into lower and upper
    /// halves. Hardware ignores bits 7:0 of the LO register.
    pub(super) NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_LO(u32) @ 0x00100a34 {
        31:0    adr => u32;
    }

    /// High bits of the physical system memory address used by the GPU to perform
    /// sysmembar operations on Hopper.
    pub(super) NV_PFB_FBHUB_PCIE_FLUSH_SYSMEM_ADDR_HI(u32) @ 0x00100a38 {
        19:0    adr;
    }
}

impl NV_PFB_PRI_MMU_LOCAL_MEMORY_RANGE {
    /// Returns the usable framebuffer size, in bytes.
    pub(super) fn usable_fb_size(self) -> u64 {
        let size = (u64::from(self.lower_mag()) << u64::from(self.lower_scale())) * u64::SZ_1M;

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
    pub(super) fn lower_bound(self) -> u64 {
        u64::from(self.lo_val()) << 12
    }
}

impl NV_PFB_PRI_MMU_WPR2_ADDR_HI {
    /// Returns the higher (exclusive) bound of the WPR2 region.
    ///
    /// A value of zero means the WPR2 region is not set.
    pub(super) fn higher_bound(self) -> u64 {
        u64::from(self.hi_val()) << 12
    }

    /// Returns whether the WPR2 region is currently set.
    pub(super) fn is_wpr2_set(self) -> bool {
        self.hi_val() != 0
    }
}
