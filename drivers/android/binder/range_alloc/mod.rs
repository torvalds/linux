// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use kernel::{page::PAGE_SIZE, prelude::*, seq_file::SeqFile, task::Pid};

mod tree;
use self::tree::{FromArrayAllocs, ReserveNewTreeAlloc, TreeRangeAllocator};

mod array;
use self::array::{ArrayRangeAllocator, EmptyArrayAlloc};

enum DescriptorState<T> {
    Reserved(Reservation),
    Allocated(Allocation<T>),
}

impl<T> DescriptorState<T> {
    fn new(is_oneway: bool, debug_id: usize, pid: Pid) -> Self {
        DescriptorState::Reserved(Reservation {
            debug_id,
            is_oneway,
            pid,
        })
    }

    fn pid(&self) -> Pid {
        match self {
            DescriptorState::Reserved(inner) => inner.pid,
            DescriptorState::Allocated(inner) => inner.reservation.pid,
        }
    }

    fn is_oneway(&self) -> bool {
        match self {
            DescriptorState::Reserved(inner) => inner.is_oneway,
            DescriptorState::Allocated(inner) => inner.reservation.is_oneway,
        }
    }
}

#[derive(Clone)]
struct Reservation {
    debug_id: usize,
    is_oneway: bool,
    pid: Pid,
}

impl Reservation {
    fn allocate<T>(self, data: Option<T>) -> Allocation<T> {
        Allocation {
            data,
            reservation: self,
        }
    }
}

struct Allocation<T> {
    reservation: Reservation,
    data: Option<T>,
}

impl<T> Allocation<T> {
    fn deallocate(self) -> (Reservation, Option<T>) {
        (self.reservation, self.data)
    }

    fn debug_id(&self) -> usize {
        self.reservation.debug_id
    }

    fn take(&mut self) -> Option<T> {
        self.data.take()
    }
}

/// The array implementation must switch to the tree if it wants to go beyond this number of
/// ranges.
const TREE_THRESHOLD: usize = 8;

/// Represents a range of pages that have just become completely free.
#[derive(Copy, Clone)]
pub(crate) struct FreedRange {
    pub(crate) start_page_idx: usize,
    pub(crate) end_page_idx: usize,
}

impl FreedRange {
    fn interior_pages(offset: usize, size: usize) -> FreedRange {
        FreedRange {
            // Divide round up
            start_page_idx: offset.div_ceil(PAGE_SIZE),
            // Divide round down
            end_page_idx: (offset + size) / PAGE_SIZE,
        }
    }
}

struct Range<T> {
    offset: usize,
    size: usize,
    state: DescriptorState<T>,
}

impl<T> Range<T> {
    fn endpoint(&self) -> usize {
        self.offset + self.size
    }
}

pub(crate) struct RangeAllocator<T> {
    inner: Impl<T>,
}

enum Impl<T> {
    Empty(usize),
    Array(ArrayRangeAllocator<T>),
    Tree(TreeRangeAllocator<T>),
}

impl<T> RangeAllocator<T> {
    pub(crate) fn new(size: usize) -> Self {
        Self {
            inner: Impl::Empty(size),
        }
    }

    pub(crate) fn free_oneway_space(&self) -> usize {
        match &self.inner {
            Impl::Empty(size) => size / 2,
            Impl::Array(array) => array.free_oneway_space(),
            Impl::Tree(tree) => tree.free_oneway_space(),
        }
    }

    pub(crate) fn count_buffers(&self) -> usize {
        match &self.inner {
            Impl::Empty(_size) => 0,
            Impl::Array(array) => array.count_buffers(),
            Impl::Tree(tree) => tree.count_buffers(),
        }
    }

    pub(crate) fn debug_print(&self, m: &SeqFile) -> Result<()> {
        match &self.inner {
            Impl::Empty(_size) => Ok(()),
            Impl::Array(array) => array.debug_print(m),
            Impl::Tree(tree) => tree.debug_print(m),
        }
    }

    /// Try to reserve a new buffer, using the provided allocation if necessary.
    pub(crate) fn reserve_new(&mut self, mut args: ReserveNewArgs<T>) -> Result<ReserveNew<T>> {
        match &mut self.inner {
            Impl::Empty(size) => {
                let empty_array = match args.empty_array_alloc.take() {
                    Some(empty_array) => ArrayRangeAllocator::new(*size, empty_array),
                    None => {
                        return Ok(ReserveNew::NeedAlloc(ReserveNewNeedAlloc {
                            args,
                            need_empty_array_alloc: true,
                            need_new_tree_alloc: false,
                            need_tree_alloc: false,
                        }))
                    }
                };

                self.inner = Impl::Array(empty_array);
                self.reserve_new(args)
            }
            Impl::Array(array) if array.is_full() => {
                let allocs = match args.new_tree_alloc {
                    Some(ref mut allocs) => allocs,
                    None => {
                        return Ok(ReserveNew::NeedAlloc(ReserveNewNeedAlloc {
                            args,
                            need_empty_array_alloc: false,
                            need_new_tree_alloc: true,
                            need_tree_alloc: true,
                        }))
                    }
                };

                let new_tree =
                    TreeRangeAllocator::from_array(array.total_size(), &mut array.ranges, allocs);

                self.inner = Impl::Tree(new_tree);
                self.reserve_new(args)
            }
            Impl::Array(array) => {
                let offset =
                    array.reserve_new(args.debug_id, args.size, args.is_oneway, args.pid)?;
                Ok(ReserveNew::Success(ReserveNewSuccess {
                    offset,
                    oneway_spam_detected: false,
                    _empty_array_alloc: args.empty_array_alloc,
                    _new_tree_alloc: args.new_tree_alloc,
                    _tree_alloc: args.tree_alloc,
                }))
            }
            Impl::Tree(tree) => {
                let alloc = match args.tree_alloc {
                    Some(alloc) => alloc,
                    None => {
                        return Ok(ReserveNew::NeedAlloc(ReserveNewNeedAlloc {
                            args,
                            need_empty_array_alloc: false,
                            need_new_tree_alloc: false,
                            need_tree_alloc: true,
                        }));
                    }
                };
                let (offset, oneway_spam_detected) =
                    tree.reserve_new(args.debug_id, args.size, args.is_oneway, args.pid, alloc)?;
                Ok(ReserveNew::Success(ReserveNewSuccess {
                    offset,
                    oneway_spam_detected,
                    _empty_array_alloc: args.empty_array_alloc,
                    _new_tree_alloc: args.new_tree_alloc,
                    _tree_alloc: None,
                }))
            }
        }
    }

    /// Deletes the allocations at `offset`.
    pub(crate) fn reservation_abort(&mut self, offset: usize) -> Result<FreedRange> {
        match &mut self.inner {
            Impl::Empty(_size) => Err(EINVAL),
            Impl::Array(array) => array.reservation_abort(offset),
            Impl::Tree(tree) => {
                let freed_range = tree.reservation_abort(offset)?;
                if tree.is_empty() {
                    self.inner = Impl::Empty(tree.total_size());
                }
                Ok(freed_range)
            }
        }
    }

    /// Called when an allocation is no longer in use by the kernel.
    ///
    /// The value in `data` will be stored, if any. A mutable reference is used to avoid dropping
    /// the `T` when an error is returned.
    pub(crate) fn reservation_commit(&mut self, offset: usize, data: &mut Option<T>) -> Result {
        match &mut self.inner {
            Impl::Empty(_size) => Err(EINVAL),
            Impl::Array(array) => array.reservation_commit(offset, data),
            Impl::Tree(tree) => tree.reservation_commit(offset, data),
        }
    }

    /// Called when the kernel starts using an allocation.
    ///
    /// Returns the size of the existing entry and the data associated with it.
    pub(crate) fn reserve_existing(&mut self, offset: usize) -> Result<(usize, usize, Option<T>)> {
        match &mut self.inner {
            Impl::Empty(_size) => Err(EINVAL),
            Impl::Array(array) => array.reserve_existing(offset),
            Impl::Tree(tree) => tree.reserve_existing(offset),
        }
    }

    /// Call the provided callback at every allocated region.
    ///
    /// This destroys the range allocator. Used only during shutdown.
    pub(crate) fn take_for_each<F: Fn(usize, usize, usize, Option<T>)>(&mut self, callback: F) {
        match &mut self.inner {
            Impl::Empty(_size) => {}
            Impl::Array(array) => array.take_for_each(callback),
            Impl::Tree(tree) => tree.take_for_each(callback),
        }
    }
}

/// The arguments for `reserve_new`.
#[derive(Default)]
pub(crate) struct ReserveNewArgs<T> {
    pub(crate) size: usize,
    pub(crate) is_oneway: bool,
    pub(crate) debug_id: usize,
    pub(crate) pid: Pid,
    pub(crate) empty_array_alloc: Option<EmptyArrayAlloc<T>>,
    pub(crate) new_tree_alloc: Option<FromArrayAllocs<T>>,
    pub(crate) tree_alloc: Option<ReserveNewTreeAlloc<T>>,
}

/// The return type of `ReserveNew`.
pub(crate) enum ReserveNew<T> {
    Success(ReserveNewSuccess<T>),
    NeedAlloc(ReserveNewNeedAlloc<T>),
}

/// Returned by `reserve_new` when the reservation was successul.
pub(crate) struct ReserveNewSuccess<T> {
    pub(crate) offset: usize,
    pub(crate) oneway_spam_detected: bool,

    // If the user supplied an allocation that we did not end up using, then we return it here.
    // The caller will kfree it outside of the lock.
    _empty_array_alloc: Option<EmptyArrayAlloc<T>>,
    _new_tree_alloc: Option<FromArrayAllocs<T>>,
    _tree_alloc: Option<ReserveNewTreeAlloc<T>>,
}

/// Returned by `reserve_new` to request the caller to make an allocation before calling the method
/// again.
pub(crate) struct ReserveNewNeedAlloc<T> {
    args: ReserveNewArgs<T>,
    need_empty_array_alloc: bool,
    need_new_tree_alloc: bool,
    need_tree_alloc: bool,
}

impl<T> ReserveNewNeedAlloc<T> {
    /// Make the necessary allocations for another call to `reserve_new`.
    pub(crate) fn make_alloc(mut self) -> Result<ReserveNewArgs<T>> {
        if self.need_empty_array_alloc && self.args.empty_array_alloc.is_none() {
            self.args.empty_array_alloc = Some(EmptyArrayAlloc::try_new(TREE_THRESHOLD)?);
        }
        if self.need_new_tree_alloc && self.args.new_tree_alloc.is_none() {
            self.args.new_tree_alloc = Some(FromArrayAllocs::try_new(TREE_THRESHOLD)?);
        }
        if self.need_tree_alloc && self.args.tree_alloc.is_none() {
            self.args.tree_alloc = Some(ReserveNewTreeAlloc::try_new()?);
        }
        Ok(self.args)
    }
}
