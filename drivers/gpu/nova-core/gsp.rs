// SPDX-License-Identifier: GPL-2.0

mod boot;

use kernel::{
    debugfs,
    device,
    dma::{
        Coherent,
        CoherentBox,
        DmaAddress, //
    },
    pci,
    prelude::*,
    transmute::{
        AsBytes,
        FromBytes, //
    }, //
};

pub(crate) mod cmdq;
pub(crate) mod commands;
mod fw;
mod sequencer;

pub(crate) use fw::{
    GspFwWprMeta,
    LibosParams, //
};

use crate::{
    gsp::cmdq::Cmdq,
    gsp::fw::{
        GspArgumentsPadded,
        LibosMemoryRegionInitArgument, //
    },
    num,
};

pub(crate) const GSP_PAGE_SHIFT: usize = 12;
pub(crate) const GSP_PAGE_SIZE: usize = 1 << GSP_PAGE_SHIFT;

/// Number of GSP pages to use in a RM log buffer.
const RM_LOG_BUFFER_NUM_PAGES: usize = 0x10;
const LOG_BUFFER_SIZE: usize = RM_LOG_BUFFER_NUM_PAGES * GSP_PAGE_SIZE;

/// Array of page table entries, as understood by the GSP bootloader.
#[repr(C)]
struct PteArray<const NUM_ENTRIES: usize>([u64; NUM_ENTRIES]);

/// SAFETY: arrays of `u64` implement `FromBytes` and we are but a wrapper around one.
unsafe impl<const NUM_ENTRIES: usize> FromBytes for PteArray<NUM_ENTRIES> {}

/// SAFETY: arrays of `u64` implement `AsBytes` and we are but a wrapper around one.
unsafe impl<const NUM_ENTRIES: usize> AsBytes for PteArray<NUM_ENTRIES> {}

impl<const NUM_PAGES: usize> PteArray<NUM_PAGES> {
    /// Returns the page table entry for `index`, for a mapping starting at `start`.
    // TODO: Replace with `IoView` projection once available.
    fn entry(start: DmaAddress, index: usize) -> Result<u64> {
        start
            .checked_add(num::usize_as_u64(index) << GSP_PAGE_SHIFT)
            .ok_or(EOVERFLOW)
    }
}

/// The logging buffers are byte queues that contain encoded printf-like
/// messages from GSP-RM.  They need to be decoded by a special application
/// that can parse the buffers.
///
/// The 'loginit' buffer contains logs from early GSP-RM init and
/// exception dumps.  The 'logrm' buffer contains the subsequent logs. Both are
/// written to directly by GSP-RM and can be any multiple of GSP_PAGE_SIZE.
///
/// The physical address map for the log buffer is stored in the buffer
/// itself, starting with offset 1. Offset 0 contains the "put" pointer (pp).
/// Initially, pp is equal to 0. If the buffer has valid logging data in it,
/// then pp points to index into the buffer where the next logging entry will
/// be written. Therefore, the logging data is valid if:
///   1 <= pp < sizeof(buffer)/sizeof(u64)
struct LogBuffer(Coherent<[u8; LOG_BUFFER_SIZE]>);

impl LogBuffer {
    /// Creates a new `LogBuffer` mapped on `dev`.
    fn new(dev: &device::Device<device::Bound>) -> Result<Self> {
        let obj = Self(Coherent::zeroed(dev, GFP_KERNEL)?);

        let start_addr = obj.0.dma_handle();

        // SAFETY: `obj` has just been created and we are its sole user.
        let pte_region = unsafe {
            &mut obj.0.as_mut()[size_of::<u64>()..][..RM_LOG_BUFFER_NUM_PAGES * size_of::<u64>()]
        };

        // Write values one by one to avoid an on-stack instance of `PteArray`.
        for (i, chunk) in pte_region.chunks_exact_mut(size_of::<u64>()).enumerate() {
            let pte_value = PteArray::<0>::entry(start_addr, i)?;

            chunk.copy_from_slice(&pte_value.to_ne_bytes());
        }

        Ok(obj)
    }
}

struct LogBuffers {
    /// Init log buffer.
    loginit: LogBuffer,
    /// Interrupts log buffer.
    logintr: LogBuffer,
    /// RM log buffer.
    logrm: LogBuffer,
}

/// GSP runtime data.
#[pin_data]
pub(crate) struct Gsp {
    /// Libos arguments.
    pub(crate) libos: Coherent<[LibosMemoryRegionInitArgument]>,
    /// Log buffers, optionally exposed via debugfs.
    #[pin]
    logs: debugfs::Scope<LogBuffers>,
    /// Command queue.
    #[pin]
    pub(crate) cmdq: Cmdq,
    /// RM arguments.
    rmargs: Coherent<GspArgumentsPadded>,
}

impl Gsp {
    // Creates an in-place initializer for a `Gsp` manager for `pdev`.
    pub(crate) fn new(pdev: &pci::Device<device::Bound>) -> impl PinInit<Self, Error> + '_ {
        pin_init::pin_init_scope(move || {
            let dev = pdev.as_ref();

            let loginit = LogBuffer::new(dev)?;
            let logintr = LogBuffer::new(dev)?;
            let logrm = LogBuffer::new(dev)?;

            // Initialise the logging structures. The OpenRM equivalents are in:
            // _kgspInitLibosLoggingStructures (allocates memory for buffers)
            // kgspSetupLibosInitArgs_IMPL (creates pLibosInitArgs[] array)
            Ok(try_pin_init!(Self {
                cmdq <- Cmdq::new(dev),
                rmargs: Coherent::init(dev, GFP_KERNEL, GspArgumentsPadded::new(&cmdq))?,
                libos: {
                    let mut libos = CoherentBox::zeroed_slice(
                        dev,
                        GSP_PAGE_SIZE / size_of::<LibosMemoryRegionInitArgument>(),
                        GFP_KERNEL,
                    )?;

                    libos.init_at(0, LibosMemoryRegionInitArgument::new("LOGINIT", &loginit.0))?;
                    libos.init_at(1, LibosMemoryRegionInitArgument::new("LOGINTR", &logintr.0))?;
                    libos.init_at(2, LibosMemoryRegionInitArgument::new("LOGRM", &logrm.0))?;
                    libos.init_at(3, LibosMemoryRegionInitArgument::new("RMARGS", rmargs))?;

                    libos.into()
                },
                logs <- {
                    let log_buffers = LogBuffers {
                        loginit,
                        logintr,
                        logrm,
                    };

                    #[allow(static_mut_refs)]
                    // SAFETY: `DEBUGFS_ROOT` is created before driver registration and cleared
                    // after driver unregistration, so no probe() can race with its modification.
                    //
                    // PANIC: `DEBUGFS_ROOT` cannot be `None` here.  It is set before driver
                    // registration and cleared after driver unregistration, so it is always
                    // `Some` for the entire lifetime that probe() can be called.
                    let log_parent: &debugfs::Dir = unsafe { crate::DEBUGFS_ROOT.as_ref() }
                        .expect("DEBUGFS_ROOT not initialized");

                    log_parent.scope(log_buffers, dev.name(), |logs, dir| {
                        dir.read_binary_file(c"loginit", &logs.loginit.0);
                        dir.read_binary_file(c"logintr", &logs.logintr.0);
                        dir.read_binary_file(c"logrm", &logs.logrm.0);
                    })
                },
            }))
        })
    }
}
