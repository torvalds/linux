// SPDX-License-Identifier: GPL-2.0 or MIT

use core::ops::Deref;
use core::ops::DerefMut;
use kernel::bits::genmask_u32;
use kernel::device::Bound;
use kernel::device::Device;
use kernel::devres::Devres;
use kernel::io::poll;
use kernel::platform;
use kernel::prelude::*;
use kernel::time::Delta;
use kernel::transmute::AsBytes;
use kernel::uapi;

use crate::driver::IoMem;
use crate::regs;

/// Struct containing information that can be queried by userspace. This is read from
/// the GPU's registers.
///
/// # Invariants
///
/// - The layout of this struct identical to the C `struct drm_panthor_gpu_info`.
#[repr(transparent)]
#[derive(Clone, Copy)]
pub(crate) struct GpuInfo(pub(crate) uapi::drm_panthor_gpu_info);

impl GpuInfo {
    pub(crate) fn new(dev: &Device<Bound>, iomem: &Devres<IoMem>) -> Result<Self> {
        let gpu_id = regs::GPU_ID.read(dev, iomem)?;
        let csf_id = regs::GPU_CSF_ID.read(dev, iomem)?;
        let gpu_rev = regs::GPU_REVID.read(dev, iomem)?;
        let core_features = regs::GPU_CORE_FEATURES.read(dev, iomem)?;
        let l2_features = regs::GPU_L2_FEATURES.read(dev, iomem)?;
        let tiler_features = regs::GPU_TILER_FEATURES.read(dev, iomem)?;
        let mem_features = regs::GPU_MEM_FEATURES.read(dev, iomem)?;
        let mmu_features = regs::GPU_MMU_FEATURES.read(dev, iomem)?;
        let thread_features = regs::GPU_THREAD_FEATURES.read(dev, iomem)?;
        let max_threads = regs::GPU_THREAD_MAX_THREADS.read(dev, iomem)?;
        let thread_max_workgroup_size = regs::GPU_THREAD_MAX_WORKGROUP_SIZE.read(dev, iomem)?;
        let thread_max_barrier_size = regs::GPU_THREAD_MAX_BARRIER_SIZE.read(dev, iomem)?;
        let coherency_features = regs::GPU_COHERENCY_FEATURES.read(dev, iomem)?;

        let texture_features = regs::GPU_TEXTURE_FEATURES0.read(dev, iomem)?;

        let as_present = regs::GPU_AS_PRESENT.read(dev, iomem)?;

        let shader_present = u64::from(regs::GPU_SHADER_PRESENT_LO.read(dev, iomem)?);
        let shader_present =
            shader_present | u64::from(regs::GPU_SHADER_PRESENT_HI.read(dev, iomem)?) << 32;

        let tiler_present = u64::from(regs::GPU_TILER_PRESENT_LO.read(dev, iomem)?);
        let tiler_present =
            tiler_present | u64::from(regs::GPU_TILER_PRESENT_HI.read(dev, iomem)?) << 32;

        let l2_present = u64::from(regs::GPU_L2_PRESENT_LO.read(dev, iomem)?);
        let l2_present = l2_present | u64::from(regs::GPU_L2_PRESENT_HI.read(dev, iomem)?) << 32;

        Ok(Self(uapi::drm_panthor_gpu_info {
            gpu_id,
            gpu_rev,
            csf_id,
            l2_features,
            tiler_features,
            mem_features,
            mmu_features,
            thread_features,
            max_threads,
            thread_max_workgroup_size,
            thread_max_barrier_size,
            coherency_features,
            // TODO: Add texture_features_{1,2,3}.
            texture_features: [texture_features, 0, 0, 0],
            as_present,
            selected_coherency: uapi::drm_panthor_gpu_coherency_DRM_PANTHOR_GPU_COHERENCY_NONE,
            shader_present,
            l2_present,
            tiler_present,
            core_features,
            pad: 0,
            gpu_features: 0,
        }))
    }

    pub(crate) fn log(&self, pdev: &platform::Device) {
        let major = (self.gpu_id >> 16) & 0xff;
        let minor = (self.gpu_id >> 8) & 0xff;
        let status = self.gpu_id & 0xff;

        let model_name = if let Some(model) = GPU_MODELS
            .iter()
            .find(|&f| f.major == major && f.minor == minor)
        {
            model.name
        } else {
            "unknown"
        };

        dev_info!(
            pdev,
            "mali-{} id 0x{:x} major 0x{:x} minor 0x{:x} status 0x{:x}",
            model_name,
            self.gpu_id >> 16,
            major,
            minor,
            status
        );

        dev_info!(
            pdev,
            "Features: L2:{:#x} Tiler:{:#x} Mem:{:#x} MMU:{:#x} AS:{:#x}",
            self.l2_features,
            self.tiler_features,
            self.mem_features,
            self.mmu_features,
            self.as_present
        );

        dev_info!(
            pdev,
            "shader_present=0x{:016x} l2_present=0x{:016x} tiler_present=0x{:016x}",
            self.shader_present,
            self.l2_present,
            self.tiler_present
        );
    }

    /// Returns the number of virtual address bits supported by the GPU.
    #[expect(dead_code)]
    pub(crate) fn va_bits(&self) -> u32 {
        self.mmu_features & genmask_u32(0..=7)
    }

    /// Returns the number of physical address bits supported by the GPU.
    #[expect(dead_code)]
    pub(crate) fn pa_bits(&self) -> u32 {
        (self.mmu_features >> 8) & genmask_u32(0..=7)
    }
}

impl Deref for GpuInfo {
    type Target = uapi::drm_panthor_gpu_info;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

impl DerefMut for GpuInfo {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.0
    }
}

// SAFETY: `GpuInfo`'s invariant guarantees that it is the same type that is
// already exposed to userspace by the C driver. This implies that it fulfills
// the requirements for `AsBytes`.
//
// This means:
//
// - No implicit padding,
// - No kernel pointers,
// - No interior mutability.
unsafe impl AsBytes for GpuInfo {}

struct GpuModels {
    name: &'static str,
    major: u32,
    minor: u32,
}

const GPU_MODELS: [GpuModels; 1] = [GpuModels {
    name: "g610",
    major: 10,
    minor: 7,
}];

#[allow(dead_code)]
pub(crate) struct GpuId {
    pub(crate) arch_major: u32,
    pub(crate) arch_minor: u32,
    pub(crate) arch_rev: u32,
    pub(crate) prod_major: u32,
    pub(crate) ver_major: u32,
    pub(crate) ver_minor: u32,
    pub(crate) ver_status: u32,
}

impl From<u32> for GpuId {
    fn from(value: u32) -> Self {
        GpuId {
            arch_major: (value & genmask_u32(28..=31)) >> 28,
            arch_minor: (value & genmask_u32(24..=27)) >> 24,
            arch_rev: (value & genmask_u32(20..=23)) >> 20,
            prod_major: (value & genmask_u32(16..=19)) >> 16,
            ver_major: (value & genmask_u32(12..=15)) >> 12,
            ver_minor: (value & genmask_u32(4..=11)) >> 4,
            ver_status: value & genmask_u32(0..=3),
        }
    }
}

/// Powers on the l2 block.
pub(crate) fn l2_power_on(dev: &Device<Bound>, iomem: &Devres<IoMem>) -> Result {
    regs::L2_PWRON_LO.write(dev, iomem, 1)?;

    poll::read_poll_timeout(
        || regs::L2_READY_LO.read(dev, iomem),
        |status| *status == 1,
        Delta::from_millis(1),
        Delta::from_millis(100),
    )
    .inspect_err(|_| dev_err!(dev, "Failed to power on the GPU."))?;

    Ok(())
}
