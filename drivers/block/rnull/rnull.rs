// SPDX-License-Identifier: GPL-2.0

//! This is a Rust implementation of the C null block driver.

mod configfs;

use kernel::{
    block::{
        self,
        mq::{
            self,
            gen_disk::{self, GenDisk},
            Operations, TagSet,
        },
    },
    error::Result,
    pr_info,
    prelude::*,
    sync::Arc,
    types::ARef,
};
use pin_init::PinInit;

module! {
    type: NullBlkModule,
    name: "rnull_mod",
    authors: ["Andreas Hindborg"],
    description: "Rust implementation of the C null block driver",
    license: "GPL v2",
}

#[pin_data]
struct NullBlkModule {
    #[pin]
    configfs_subsystem: kernel::configfs::Subsystem<configfs::Config>,
}

impl kernel::InPlaceModule for NullBlkModule {
    fn init(_module: &'static ThisModule) -> impl PinInit<Self, Error> {
        pr_info!("Rust null_blk loaded\n");

        try_pin_init!(Self {
            configfs_subsystem <- configfs::subsystem(),
        })
    }
}

struct NullBlkDevice;

impl NullBlkDevice {
    fn new(
        name: &CStr,
        block_size: u32,
        rotational: bool,
        capacity_mib: u64,
    ) -> Result<GenDisk<Self>> {
        let tagset = Arc::pin_init(TagSet::new(1, 256, 1), GFP_KERNEL)?;

        gen_disk::GenDiskBuilder::new()
            .capacity_sectors(capacity_mib << (20 - block::SECTOR_SHIFT))
            .logical_block_size(block_size)?
            .physical_block_size(block_size)?
            .rotational(rotational)
            .build(fmt!("{}", name.to_str()?), tagset, ())
    }
}

#[vtable]
impl Operations for NullBlkDevice {
    type QueueData = ();

    #[inline(always)]
    fn queue_rq(_queue_data: (), rq: ARef<mq::Request<Self>>, _is_last: bool) -> Result {
        mq::Request::end_ok(rq)
            .map_err(|_e| kernel::error::code::EIO)
            // We take no refcounts on the request, so we expect to be able to
            // end the request. The request reference must be unique at this
            // point, and so `end_ok` cannot fail.
            .expect("Fatal error - expected to be able to end request");

        Ok(())
    }

    fn commit_rqs(_queue_data: ()) {}

    fn complete(rq: ARef<mq::Request<Self>>) {
        mq::Request::end_ok(rq)
            .map_err(|_e| kernel::error::code::EIO)
            // We take no refcounts on the request, so we expect to be able to
            // end the request. The request reference must be unique at this
            // point, and so `end_ok` cannot fail.
            .expect("Fatal error - expected to be able to end request");
    }
}
