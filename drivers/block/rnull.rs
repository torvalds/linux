// SPDX-License-Identifier: GPL-2.0

//! This is a Rust implementation of the C null block driver.
//!
//! Supported features:
//!
//! - blk-mq interface
//! - direct completion
//! - block size 4k
//!
//! The driver is not configurable.

use kernel::{
    alloc::flags,
    block::mq::{
        self,
        gen_disk::{self, GenDisk},
        Operations, TagSet,
    },
    error::Result,
    new_mutex, pr_info,
    prelude::*,
    sync::{Arc, Mutex},
    types::ARef,
};

module! {
    type: NullBlkModule,
    name: "rnull_mod",
    author: "Andreas Hindborg",
    description: "Rust implementation of the C null block driver",
    license: "GPL v2",
}

struct NullBlkModule {
    _disk: Pin<KBox<Mutex<GenDisk<NullBlkDevice>>>>,
}

impl kernel::Module for NullBlkModule {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        pr_info!("Rust null_blk loaded\n");
        let tagset = Arc::pin_init(TagSet::new(1, 256, 1), flags::GFP_KERNEL)?;

        let disk = gen_disk::GenDiskBuilder::new()
            .capacity_sectors(4096 << 11)
            .logical_block_size(4096)?
            .physical_block_size(4096)?
            .rotational(false)
            .build(format_args!("rnullb{}", 0), tagset)?;

        let disk = KBox::pin_init(new_mutex!(disk, "nullb:disk"), flags::GFP_KERNEL)?;

        Ok(Self { _disk: disk })
    }
}

struct NullBlkDevice;

#[vtable]
impl Operations for NullBlkDevice {
    #[inline(always)]
    fn queue_rq(rq: ARef<mq::Request<Self>>, _is_last: bool) -> Result {
        mq::Request::end_ok(rq)
            .map_err(|_e| kernel::error::code::EIO)
            // We take no refcounts on the request, so we expect to be able to
            // end the request. The request reference must be unique at this
            // point, and so `end_ok` cannot fail.
            .expect("Fatal error - expected to be able to end request");

        Ok(())
    }

    fn commit_rqs() {}
}
