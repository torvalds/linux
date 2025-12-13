// SPDX-License-Identifier: GPL-2.0

use crate::driver::{NovaDevice, NovaDriver};
use crate::gem::NovaObject;
use kernel::{
    alloc::flags::*,
    drm::{self, gem::BaseObject},
    pci,
    prelude::*,
    uapi,
};

pub(crate) struct File;

impl drm::file::DriverFile for File {
    type Driver = NovaDriver;

    fn open(_dev: &NovaDevice) -> Result<Pin<KBox<Self>>> {
        Ok(KBox::new(Self, GFP_KERNEL)?.into())
    }
}

impl File {
    /// IOCTL: get_param: Query GPU / driver metadata.
    pub(crate) fn get_param(
        dev: &NovaDevice,
        getparam: &mut uapi::drm_nova_getparam,
        _file: &drm::File<File>,
    ) -> Result<u32> {
        let adev = &dev.adev;
        let parent = adev.parent().ok_or(ENOENT)?;
        let pdev: &pci::Device = parent.try_into()?;

        let value = match getparam.param as u32 {
            uapi::NOVA_GETPARAM_VRAM_BAR_SIZE => pdev.resource_len(1)?,
            _ => return Err(EINVAL),
        };

        getparam.value = Into::<u64>::into(value);

        Ok(0)
    }

    /// IOCTL: gem_create: Create a new DRM GEM object.
    pub(crate) fn gem_create(
        dev: &NovaDevice,
        req: &mut uapi::drm_nova_gem_create,
        file: &drm::File<File>,
    ) -> Result<u32> {
        let obj = NovaObject::new(dev, req.size.try_into()?)?;

        req.handle = obj.create_handle(file)?;

        Ok(0)
    }

    /// IOCTL: gem_info: Query GEM metadata.
    pub(crate) fn gem_info(
        _dev: &NovaDevice,
        req: &mut uapi::drm_nova_gem_info,
        file: &drm::File<File>,
    ) -> Result<u32> {
        let bo = NovaObject::lookup_handle(file, req.handle)?;

        req.size = bo.size().try_into()?;

        Ok(0)
    }
}
