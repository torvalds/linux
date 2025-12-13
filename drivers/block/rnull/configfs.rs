// SPDX-License-Identifier: GPL-2.0

use super::{NullBlkDevice, THIS_MODULE};
use core::fmt::{Display, Write};
use kernel::{
    block::mq::gen_disk::{GenDisk, GenDiskBuilder},
    c_str,
    configfs::{self, AttributeOperations},
    configfs_attrs, new_mutex,
    page::PAGE_SIZE,
    prelude::*,
    str::{kstrtobool_bytes, CString},
    sync::Mutex,
};
use pin_init::PinInit;

pub(crate) fn subsystem() -> impl PinInit<kernel::configfs::Subsystem<Config>, Error> {
    let item_type = configfs_attrs! {
        container: configfs::Subsystem<Config>,
        data: Config,
        child: DeviceConfig,
        attributes: [
            features: 0,
        ],
    };

    kernel::configfs::Subsystem::new(c_str!("rnull"), item_type, try_pin_init!(Config {}))
}

#[pin_data]
pub(crate) struct Config {}

#[vtable]
impl AttributeOperations<0> for Config {
    type Data = Config;

    fn show(_this: &Config, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        let mut writer = kernel::str::Formatter::new(page);
        writer.write_str("blocksize,size,rotational,irqmode\n")?;
        Ok(writer.bytes_written())
    }
}

#[vtable]
impl configfs::GroupOperations for Config {
    type Child = DeviceConfig;

    fn make_group(
        &self,
        name: &CStr,
    ) -> Result<impl PinInit<configfs::Group<DeviceConfig>, Error>> {
        let item_type = configfs_attrs! {
            container: configfs::Group<DeviceConfig>,
            data: DeviceConfig,
            attributes: [
                // Named for compatibility with C null_blk
                power: 0,
                blocksize: 1,
                rotational: 2,
                size: 3,
                irqmode: 4,
            ],
        };

        Ok(configfs::Group::new(
            name.try_into()?,
            item_type,
            // TODO: cannot coerce new_mutex!() to impl PinInit<_, Error>, so put mutex inside
            try_pin_init!( DeviceConfig {
                data <- new_mutex!(DeviceConfigInner {
                    powered: false,
                    block_size: 4096,
                    rotational: false,
                    disk: None,
                    capacity_mib: 4096,
                    irq_mode: IRQMode::None,
                    name: name.try_into()?,
                }),
            }),
        ))
    }
}

#[derive(Debug, Clone, Copy)]
pub(crate) enum IRQMode {
    None,
    Soft,
}

impl TryFrom<u8> for IRQMode {
    type Error = kernel::error::Error;

    fn try_from(value: u8) -> Result<Self> {
        match value {
            0 => Ok(Self::None),
            1 => Ok(Self::Soft),
            _ => Err(EINVAL),
        }
    }
}

impl Display for IRQMode {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::None => f.write_str("0")?,
            Self::Soft => f.write_str("1")?,
        }
        Ok(())
    }
}

#[pin_data]
pub(crate) struct DeviceConfig {
    #[pin]
    data: Mutex<DeviceConfigInner>,
}

#[pin_data]
struct DeviceConfigInner {
    powered: bool,
    name: CString,
    block_size: u32,
    rotational: bool,
    capacity_mib: u64,
    irq_mode: IRQMode,
    disk: Option<GenDisk<NullBlkDevice>>,
}

#[vtable]
impl configfs::AttributeOperations<0> for DeviceConfig {
    type Data = DeviceConfig;

    fn show(this: &DeviceConfig, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        let mut writer = kernel::str::Formatter::new(page);

        if this.data.lock().powered {
            writer.write_str("1\n")?;
        } else {
            writer.write_str("0\n")?;
        }

        Ok(writer.bytes_written())
    }

    fn store(this: &DeviceConfig, page: &[u8]) -> Result {
        let power_op = kstrtobool_bytes(page)?;
        let mut guard = this.data.lock();

        if !guard.powered && power_op {
            guard.disk = Some(NullBlkDevice::new(
                &guard.name,
                guard.block_size,
                guard.rotational,
                guard.capacity_mib,
                guard.irq_mode,
            )?);
            guard.powered = true;
        } else if guard.powered && !power_op {
            drop(guard.disk.take());
            guard.powered = false;
        }

        Ok(())
    }
}

#[vtable]
impl configfs::AttributeOperations<1> for DeviceConfig {
    type Data = DeviceConfig;

    fn show(this: &DeviceConfig, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        let mut writer = kernel::str::Formatter::new(page);
        writer.write_fmt(fmt!("{}\n", this.data.lock().block_size))?;
        Ok(writer.bytes_written())
    }

    fn store(this: &DeviceConfig, page: &[u8]) -> Result {
        if this.data.lock().powered {
            return Err(EBUSY);
        }

        let text = core::str::from_utf8(page)?.trim();
        let value = text.parse::<u32>().map_err(|_| EINVAL)?;

        GenDiskBuilder::validate_block_size(value)?;
        this.data.lock().block_size = value;
        Ok(())
    }
}

#[vtable]
impl configfs::AttributeOperations<2> for DeviceConfig {
    type Data = DeviceConfig;

    fn show(this: &DeviceConfig, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        let mut writer = kernel::str::Formatter::new(page);

        if this.data.lock().rotational {
            writer.write_str("1\n")?;
        } else {
            writer.write_str("0\n")?;
        }

        Ok(writer.bytes_written())
    }

    fn store(this: &DeviceConfig, page: &[u8]) -> Result {
        if this.data.lock().powered {
            return Err(EBUSY);
        }

        this.data.lock().rotational = kstrtobool_bytes(page)?;

        Ok(())
    }
}

#[vtable]
impl configfs::AttributeOperations<3> for DeviceConfig {
    type Data = DeviceConfig;

    fn show(this: &DeviceConfig, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        let mut writer = kernel::str::Formatter::new(page);
        writer.write_fmt(fmt!("{}\n", this.data.lock().capacity_mib))?;
        Ok(writer.bytes_written())
    }

    fn store(this: &DeviceConfig, page: &[u8]) -> Result {
        if this.data.lock().powered {
            return Err(EBUSY);
        }

        let text = core::str::from_utf8(page)?.trim();
        let value = text.parse::<u64>().map_err(|_| EINVAL)?;

        this.data.lock().capacity_mib = value;
        Ok(())
    }
}

#[vtable]
impl configfs::AttributeOperations<4> for DeviceConfig {
    type Data = DeviceConfig;

    fn show(this: &DeviceConfig, page: &mut [u8; PAGE_SIZE]) -> Result<usize> {
        let mut writer = kernel::str::Formatter::new(page);
        writer.write_fmt(fmt!("{}\n", this.data.lock().irq_mode))?;
        Ok(writer.bytes_written())
    }

    fn store(this: &DeviceConfig, page: &[u8]) -> Result {
        if this.data.lock().powered {
            return Err(EBUSY);
        }

        let text = core::str::from_utf8(page)?.trim();
        let value = text.parse::<u8>().map_err(|_| EINVAL)?;

        this.data.lock().irq_mode = IRQMode::try_from(value)?;
        Ok(())
    }
}
