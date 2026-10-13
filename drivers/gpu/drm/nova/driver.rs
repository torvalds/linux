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

use nova_core::api::{
    NovaCoreApi,
    NovaCoreApiHandle, //
};

pub(crate) struct NovaDriver;

#[pin_data]
pub(crate) struct Nova<'bound> {
    drm: ARef<drm::Device<NovaDriver>>,
    #[pin]
    reg: drm::Registration<'bound, NovaDriver>,
}

/// DRM registration data, accessible from ioctl handlers via the registration guard.
pub(crate) struct DrmRegData<'bound> {
    pub(crate) api: NovaCoreApiHandle<'bound>,
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
        try_pin_init!(Self::Data {
            reg <- {
                let drm = drm::UnregisteredDevice::<Self>::new(adev, Ok(()))?;

                let reg_data = DrmRegData {
                    api: NovaCoreApi::of(adev)?,
                };

                // SAFETY: `reg` is stored in `Self::Data` and dropped when the driver is unbound;
                // it is never forgotten.
                unsafe { drm::Registration::new(adev.as_ref(), drm, reg_data) }
            },
            drm: reg.device().into(),
        })
    }
}

#[vtable]
impl drm::Driver for NovaDriver {
    type Data = ();
    type RegistrationData<'a> = DrmRegData<'a>;
    type File = File;
    type Object = gem::Object<NovaObject>;
    type ParentDevice<Ctx: DeviceContext> = auxiliary::Device<Ctx>;

    const INFO: drm::DriverInfo = INFO;
    const FEAT_RENDER: bool = true;

    kernel::declare_drm_ioctls! {
        (NOVA_INFO, drm_nova_info, ioctl::RENDER_ALLOW, File::info),
        (NOVA_GEM_CREATE, drm_nova_gem_create, ioctl::AUTH | ioctl::RENDER_ALLOW, File::gem_create),
        (NOVA_GEM_INFO, drm_nova_gem_info, ioctl::AUTH | ioctl::RENDER_ALLOW, File::gem_info),
    }
}
