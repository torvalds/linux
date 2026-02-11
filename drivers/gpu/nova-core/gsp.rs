// SPDX-License-Identifier: GPL-2.0

mod boot;

use kernel::{
    device,
    dma::{
        CoherentAllocation,
        DmaAddress, //
    },
    dma_write,
    pci,
    prelude::*,
    transmute::AsBytes, //
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

/// Array of page table entries, as understood by the GSP bootloader.
#[repr(C)]
struct PteArray<const NUM_ENTRIES: usize>([u64; NUM_ENTRIES]);

/// SAFETY: arrays of `u64` implement `AsBytes` and we are but a wrapper around one.
unsafe impl<const NUM_ENTRIES: usize> AsBytes for PteArray<NUM_ENTRIES> {}

impl<const NUM_PAGES: usize> PteArray<NUM_PAGES> {
    /// Creates a new page table array mapping `NUM_PAGES` GSP pages starting at address `start`.
    fn new(start: DmaAddress) -> Result<Self> {
        let mut ptes = [0u64; NUM_PAGES];
        for (i, pte) in ptes.iter_mut().enumerate() {
            *pte = start
                .checked_add(num::usize_as_u64(i) << GSP_PAGE_SHIFT)
                .ok_or(EOVERFLOW)?;
        }

        Ok(Self(ptes))
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
struct LogBuffer(CoherentAllocation<u8>);

impl LogBuffer {
    /// Creates a new `LogBuffer` mapped on `dev`.
    fn new(dev: &device::Device<device::Bound>) -> Result<Self> {
        const NUM_PAGES: usize = RM_LOG_BUFFER_NUM_PAGES;

        let mut obj = Self(CoherentAllocation::<u8>::alloc_coherent(
            dev,
            NUM_PAGES * GSP_PAGE_SIZE,
            GFP_KERNEL | __GFP_ZERO,
        )?);
        let ptes = PteArray::<NUM_PAGES>::new(obj.0.dma_handle())?;

        // SAFETY: `obj` has just been created and we are its sole user.
        unsafe {
            // Copy the self-mapping PTE at the expected location.
            obj.0
                .as_slice_mut(size_of::<u64>(), size_of_val(&ptes))?
                .copy_from_slice(ptes.as_bytes())
        };

        Ok(obj)
    }
}

/// GSP runtime data.
#[pin_data]
pub(crate) struct Gsp {
    /// Libos arguments.
    pub(crate) libos: CoherentAllocation<LibosMemoryRegionInitArgument>,
    /// Init log buffer.
    loginit: LogBuffer,
    /// Interrupts log buffer.
    logintr: LogBuffer,
    /// RM log buffer.
    logrm: LogBuffer,
    /// Command queue.
    pub(crate) cmdq: Cmdq,
    /// RM arguments.
    rmargs: CoherentAllocation<GspArgumentsPadded>,
}

impl Gsp {
    // Creates an in-place initializer for a `Gsp` manager for `pdev`.
    pub(crate) fn new(pdev: &pci::Device<device::Bound>) -> impl PinInit<Self, Error> + '_ {
        pin_init::pin_init_scope(move || {
            let dev = pdev.as_ref();

            Ok(try_pin_init!(Self {
                libos: CoherentAllocation::<LibosMemoryRegionInitArgument>::alloc_coherent(
                    dev,
                    GSP_PAGE_SIZE / size_of::<LibosMemoryRegionInitArgument>(),
                    GFP_KERNEL | __GFP_ZERO,
                )?,
                loginit: LogBuffer::new(dev)?,
                logintr: LogBuffer::new(dev)?,
                logrm: LogBuffer::new(dev)?,
                cmdq: Cmdq::new(dev)?,
                rmargs: CoherentAllocation::<GspArgumentsPadded>::alloc_coherent(
                    dev,
                    1,
                    GFP_KERNEL | __GFP_ZERO,
                )?,
                _: {
                    // Initialise the logging structures. The OpenRM equivalents are in:
                    // _kgspInitLibosLoggingStructures (allocates memory for buffers)
                    // kgspSetupLibosInitArgs_IMPL (creates pLibosInitArgs[] array)
                    dma_write!(
                        libos[0] = LibosMemoryRegionInitArgument::new("LOGINIT", &loginit.0)
                    )?;
                    dma_write!(
                        libos[1] = LibosMemoryRegionInitArgument::new("LOGINTR", &logintr.0)
                    )?;
                    dma_write!(libos[2] = LibosMemoryRegionInitArgument::new("LOGRM", &logrm.0))?;
                    dma_write!(rmargs[0].inner = fw::GspArgumentsCached::new(cmdq))?;
                    dma_write!(libos[3] = LibosMemoryRegionInitArgument::new("RMARGS", rmargs))?;
                },
            }))
        })
    }
}
