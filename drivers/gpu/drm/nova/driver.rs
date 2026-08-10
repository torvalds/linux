// SPDX-License-Identifier: GPL-2.0

use kernel::{
    auxiliary,
    device::{
        Core,
        DeviceContext, //
    },
    drm::{
        self,
        gem,
        ioctl, //
    },
    prelude::*,
    sync::aref::ARef, //
};

use crate::file::File;
use crate::gem::NovaObject;

pub(crate) struct NovaDriver;

pub(crate) struct Nova<'bound> {
    #[expect(unused)]
    drm: ARef<drm::Device<NovaDriver>>,
    _reg: drm::Registration<'bound, NovaDriver>,
}

/// Convienence type alias for the DRM device type for this driver
pub(crate) type NovaDevice<Ctx = drm::Normal> = drm::Device<NovaDriver, Ctx>;

const INFO: drm::DriverInfo = drm::DriverInfo {
    major: 0,
    minor: 0,
    patchlevel: 0,
    name: c"nova-drm",
    desc: c"NVIDIA Graphics and Compute",
};

const NOVA_CORE_MODULE_NAME: &CStr = c"nova-core";
const AUXILIARY_NAME: &CStr = c"nova-drm";

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
    type Data<'bound> = Nova<'bound>;
    const ID_TABLE: auxiliary::IdTable<Self::IdInfo> = &AUX_TABLE;

    fn probe<'bound>(
        adev: &'bound auxiliary::Device<Core<'_>>,
        _info: &'bound Self::IdInfo,
    ) -> impl PinInit<Self::Data<'bound>, Error> + 'bound {
        let drm = drm::UnregisteredDevice::<Self>::new(adev, Ok(()))?;
        // SAFETY: `reg` is stored in `Nova` and dropped when the driver is unbound; it is
        // never forgotten.
        let reg = unsafe { drm::Registration::new(adev.as_ref(), drm, (), 0)? };

        Ok(Nova {
            drm: reg.device().into(),
            _reg: reg,
        })
    }
}

#[vtable]
impl drm::Driver for NovaDriver {
    type Data = ();
    type RegistrationData<'a> = ();
    type File = File;
    type Object = gem::Object<NovaObject>;
    type ParentDevice<Ctx: DeviceContext> = auxiliary::Device<Ctx>;

    const INFO: drm::DriverInfo = INFO;

    kernel::declare_drm_ioctls! {
        (NOVA_GETPARAM, drm_nova_getparam, ioctl::RENDER_ALLOW, File::get_param),
        (NOVA_GEM_CREATE, drm_nova_gem_create, ioctl::AUTH | ioctl::RENDER_ALLOW, File::gem_create),
        (NOVA_GEM_INFO, drm_nova_gem_info, ioctl::AUTH | ioctl::RENDER_ALLOW, File::gem_info),
    }
}
