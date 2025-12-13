// SPDX-License-Identifier: GPL-2.0 or MIT

use kernel::drm;
use kernel::prelude::*;
use kernel::uaccess::UserSlice;
use kernel::uapi;

use crate::driver::TyrDevice;
use crate::TyrDriver;

#[pin_data]
pub(crate) struct File {}

/// Convenience type alias for our DRM `File` type
pub(crate) type DrmFile = drm::file::File<File>;

impl drm::file::DriverFile for File {
    type Driver = TyrDriver;

    fn open(_dev: &drm::Device<Self::Driver>) -> Result<Pin<KBox<Self>>> {
        KBox::try_pin_init(try_pin_init!(Self {}), GFP_KERNEL)
    }
}

impl File {
    pub(crate) fn dev_query(
        tdev: &TyrDevice,
        devquery: &mut uapi::drm_panthor_dev_query,
        _file: &DrmFile,
    ) -> Result<u32> {
        if devquery.pointer == 0 {
            match devquery.type_ {
                uapi::drm_panthor_dev_query_type_DRM_PANTHOR_DEV_QUERY_GPU_INFO => {
                    devquery.size = core::mem::size_of_val(&tdev.gpu_info) as u32;
                    Ok(0)
                }
                _ => Err(EINVAL),
            }
        } else {
            match devquery.type_ {
                uapi::drm_panthor_dev_query_type_DRM_PANTHOR_DEV_QUERY_GPU_INFO => {
                    let mut writer = UserSlice::new(
                        UserPtr::from_addr(devquery.pointer as usize),
                        devquery.size as usize,
                    )
                    .writer();

                    writer.write(&tdev.gpu_info)?;

                    Ok(0)
                }
                _ => Err(EINVAL),
            }
        }
    }
}
