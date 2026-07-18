// SPDX-License-Identifier: GPL-2.0 or MIT

use core::ops::{
    Deref,
    DerefMut, //
};
use kernel::{
    device::{
        Bound,
        Device, //
    },
    io::{
        poll,
        register::Array,
        Io, //
    },
    prelude::*,
    time::Delta,
    transmute::AsBytes,
    uapi, //
};

use crate::{
    driver::IoMem,
    regs::{
        gpu_control::*,
        join_u64, //
    }, //
};

/// Struct containing information that can be queried by userspace. This is read from
/// the GPU's registers.
///
/// # Invariants
///
/// - The layout of this struct is identical to the C `struct drm_panthor_gpu_info`.
#[repr(transparent)]
#[derive(Clone, Copy)]
pub(crate) struct GpuInfo(pub(crate) uapi::drm_panthor_gpu_info);

impl GpuInfo {
    pub(crate) fn new(io: &IoMem<'_>) -> Self {
        Self(uapi::drm_panthor_gpu_info {
            gpu_id: io.read(GPU_ID).into_raw(),
            gpu_rev: io.read(REVIDR).into_raw(),
            csf_id: io.read(CSF_ID).into_raw(),
            l2_features: io.read(L2_FEATURES).into_raw(),
            tiler_features: io.read(TILER_FEATURES).into_raw(),
            mem_features: io.read(MEM_FEATURES).into_raw(),
            mmu_features: io.read(MMU_FEATURES).into_raw(),
            thread_features: io.read(THREAD_FEATURES).into_raw(),
            max_threads: io.read(THREAD_MAX_THREADS).into_raw(),
            thread_max_workgroup_size: io.read(THREAD_MAX_WORKGROUP_SIZE).into_raw(),
            thread_max_barrier_size: io.read(THREAD_MAX_BARRIER_SIZE).into_raw(),
            coherency_features: io.read(COHERENCY_FEATURES).into_raw(),
            texture_features: [
                io.read(TEXTURE_FEATURES::at(0)).supported_formats().get(),
                io.read(TEXTURE_FEATURES::at(1)).supported_formats().get(),
                io.read(TEXTURE_FEATURES::at(2)).supported_formats().get(),
                io.read(TEXTURE_FEATURES::at(3)).supported_formats().get(),
            ],
            as_present: io.read(AS_PRESENT).into_raw(),
            selected_coherency: uapi::drm_panthor_gpu_coherency_DRM_PANTHOR_GPU_COHERENCY_NONE,
            shader_present: join_u64(
                io.read(SHADER_PRESENT_LO).into_raw(),
                io.read(SHADER_PRESENT_HI).into_raw(),
            ),
            l2_present: join_u64(
                io.read(L2_PRESENT_LO).into_raw(),
                io.read(L2_PRESENT_HI).into_raw(),
            ),
            tiler_present: join_u64(
                io.read(TILER_PRESENT_LO).into_raw(),
                io.read(TILER_PRESENT_HI).into_raw(),
            ),
            core_features: io.read(CORE_FEATURES).into_raw(),
            // Padding must be zero.
            pad: 0,
            //GPU_FEATURES register is not available; it was introduced in arch 11.x.
            gpu_features: 0,
        })
    }

    pub(crate) fn log(&self, dev: &Device<Bound>) {
        let gpu_id = GPU_ID::from_raw(self.gpu_id);

        let model_name = if let Some(model) = GPU_MODELS.iter().find(|&f| {
            f.arch_major == gpu_id.arch_major().get() && f.prod_major == gpu_id.prod_major().get()
        }) {
            model.name
        } else {
            "unknown"
        };

        dev_info!(
            dev,
            "mali-{} GPU_ID 0x{:x} major 0x{:x} minor 0x{:x} status 0x{:x}",
            model_name,
            gpu_id.into_raw(),
            gpu_id.ver_major().get(),
            gpu_id.ver_minor().get(),
            gpu_id.ver_status().get()
        );

        dev_info!(
            dev,
            "Features: L2:{:#x} Tiler:{:#x} Mem:{:#x} MMU:{:#x} AS:{:#x}",
            self.l2_features,
            self.tiler_features,
            self.mem_features,
            self.mmu_features,
            self.as_present,
        );

        dev_info!(
            dev,
            "shader_present=0x{:016x} l2_present=0x{:016x} tiler_present=0x{:016x}",
            self.shader_present,
            self.l2_present,
            self.tiler_present,
        );
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
    arch_major: u32,
    prod_major: u32,
}

const GPU_MODELS: [GpuModels; 1] = [GpuModels {
    name: "g610",
    arch_major: 10,
    prod_major: 7,
}];

/// Powers on the l2 block.
pub(crate) fn l2_power_on(dev: &Device, io: &IoMem<'_>) -> Result {
    io.write_reg(L2_PWRON_LO::zeroed().with_const_request::<1>());

    poll::read_poll_timeout(
        || Ok(io.read(L2_READY_LO)),
        |status| status.ready() == 1,
        Delta::from_millis(1),
        Delta::from_millis(100),
    )
    .inspect_err(|_| dev_err!(dev, "Failed to power on the GPU."))?;

    Ok(())
}
