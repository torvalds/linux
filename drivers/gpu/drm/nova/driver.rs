// SPDX-License-Identifier: GPL-2.0

use kernel::{
    auxiliary, c_str, device::Core, drm, drm::gem, drm::ioctl, prelude::*, sync::aref::ARef,
};

use crate::file::File;
use crate::gem::NovaObject;

pub(crate) struct NovaDriver {
    #[expect(unused)]
    drm: ARef<drm::Device<Self>>,
}

/// Convienence type alias for the DRM device type for this driver
pub(crate) type NovaDevice = drm::Device<NovaDriver>;

#[pin_data]
pub(crate) struct NovaData {
    pub(crate) adev: ARef<auxiliary::Device>,
}

const INFO: drm::DriverInfo = drm::DriverInfo {
    major: 0,
    minor: 0,
    patchlevel: 0,
    name: c_str!("nova"),
    desc: c_str!("Nvidia Graphics"),
};

const NOVA_CORE_MODULE_NAME: &CStr = c_str!("NovaCore");
const AUXILIARY_NAME: &CStr = c_str!("nova-drm");

kernel::auxiliary_device_table!(
    AUX_TABLE,
    MODULE_AUX_TABLE,
    <NovaDriver as auxiliary::Driver>::IdInfo,
    [(
        auxiliary::DeviceId::new(NOVA_CORE_MODULE_NAME, AUXILIARY_NAME),
        ()
    )]
);

impl auxiliary::Driver for NovaDriver {
    type IdInfo = ();
    const ID_TABLE: auxiliary::IdTable<Self::IdInfo> = &AUX_TABLE;

    fn probe(adev: &auxiliary::Device<Core>, _info: &Self::IdInfo) -> Result<Pin<KBox<Self>>> {
        let data = try_pin_init!(NovaData { adev: adev.into() });

        let drm = drm::Device::<Self>::new(adev.as_ref(), data)?;
        drm::Registration::new_foreign_owned(&drm, adev.as_ref(), 0)?;

        Ok(KBox::new(Self { drm }, GFP_KERNEL)?.into())
    }
}

#[vtable]
impl drm::Driver for NovaDriver {
    type Data = NovaData;
    type File = File;
    type Object = gem::Object<NovaObject>;

    const INFO: drm::DriverInfo = INFO;

    kernel::declare_drm_ioctls! {
        (NOVA_GETPARAM, drm_nova_getparam, ioctl::RENDER_ALLOW, File::get_param),
        (NOVA_GEM_CREATE, drm_nova_gem_create, ioctl::AUTH | ioctl::RENDER_ALLOW, File::gem_create),
        (NOVA_GEM_INFO, drm_nova_gem_info, ioctl::AUTH | ioctl::RENDER_ALLOW, File::gem_info),
    }
}
