// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use kernel::{
    page::PAGE_SIZE,
    prelude::*,
    rbtree::{RBTree, RBTreeNode, RBTreeNodeReservation},
    seq_file::SeqFile,
    seq_print,
    task::Pid,
};

use crate::range_alloc::{DescriptorState, FreedRange, Range};

/// Keeps track of allocations in a process' mmap.
///
/// Each process has an mmap where the data for incoming transactions will be placed. This struct
/// keeps track of allocations made in the mmap. For each allocation, we store a descriptor that
/// has metadata related to the allocation. We also keep track of available free space.
pub(super) struct TreeRangeAllocator<T> {
    /// This collection contains descriptors for *both* ranges containing an allocation, *and* free
    /// ranges between allocations. The free ranges get merged, so there are never two free ranges
    /// next to each other.
    tree: RBTree<usize, Descriptor<T>>,
    /// Contains an entry for every free range in `self.tree`. This tree sorts the ranges by size,
    /// letting us look up the smallest range whose size is at least some lower bound.
    free_tree: RBTree<FreeKey, ()>,
    size: usize,
    free_oneway_space: usize,
}

impl<T> TreeRangeAllocator<T> {
    pub(crate) fn from_array(
        size: usize,
        ranges: &mut KVec<Range<T>>,
        alloc: &mut FromArrayAllocs<T>,
    ) -> Self {
        let mut tree = TreeRangeAllocator {
            tree: RBTree::new(),
            free_tree: RBTree::new(),
            size,
            free_oneway_space: size / 2,
        };

        let mut free_offset = 0;
        for range in ranges.drain_all() {
            let free_size = range.offset - free_offset;
            if free_size > 0 {
                let free_node = alloc.free_tree.pop().unwrap();
                tree.free_tree
                    .insert(free_node.into_node((free_size, free_offset), ()));
                let tree_node = alloc.tree.pop().unwrap();
                tree.tree.insert(
                    tree_node.into_node(free_offset, Descriptor::new(free_offset, free_size)),
                );
            }
            free_offset = range.endpoint();

            if range.state.is_oneway() {
                tree.free_oneway_space = tree.free_oneway_space.saturating_sub(range.size);
            }

            let free_res = alloc.free_tree.pop().unwrap();
            let tree_node = alloc.tree.pop().unwrap();
            let mut desc = Descriptor::new(range.offset, range.size);
            desc.state = Some((range.state, free_res));
            tree.tree.insert(tree_node.into_node(range.offset, desc));
        }

        // After the last range, we may need a free range.
        if free_offset < size {
            let free_size = size - free_offset;
            let free_node = alloc.free_tree.pop().unwrap();
            tree.free_tree
                .insert(free_node.into_node((free_size, free_offset), ()));
            let tree_node = alloc.tree.pop().unwrap();
            tree.tree
                .insert(tree_node.into_node(free_offset, Descriptor::new(free_offset, free_size)));
        }

        tree
    }

    pub(crate) fn is_empty(&self) -> bool {
        let mut tree_iter = self.tree.values();
        // There's always at least one range, because index zero is either the start of a free or
        // allocated range.
        let first_value = tree_iter.next().unwrap();
        if tree_iter.next().is_some() {
            // There are never two free ranges next to each other, so if there is more than one
            // descriptor, then at least one of them must hold an allocated range.
            return false;
        }
        // There is only one descriptor. Return true if it is for a free range.
        first_value.state.is_none()
    }

    pub(crate) fn total_size(&self) -> usize {
        self.size
    }

    pub(crate) fn free_oneway_space(&self) -> usize {
        self.free_oneway_space
    }

    pub(crate) fn count_buffers(&self) -> usize {
        self.tree
            .values()
            .filter(|desc| desc.state.is_some())
            .count()
    }

    pub(crate) fn debug_print(&self, m: &SeqFile) -> Result<()> {
        for desc in self.tree.values() {
            let state = match &desc.state {
                Some(state) => &state.0,
                None => continue,
            };
            seq_print!(
                m,
                "  buffer: {} size {} pid {}",
                desc.offset,
                desc.size,
                state.pid(),
            );
            if state.is_oneway() {
                seq_print!(m, " oneway");
            }
            match state {
                DescriptorState::Reserved(_res) => {
                    seq_print!(m, " reserved\n");
                }
                DescriptorState::Allocated(_alloc) => {
                    seq_print!(m, " allocated\n");
                }
            }
        }
        Ok(())
    }

    fn find_best_match(&mut self, size: usize) -> Option<&mut Descriptor<T>> {
        let free_cursor = self.free_tree.cursor_lower_bound(&(size, 0))?;
        let ((_, offset), ()) = free_cursor.current();
        self.tree.get_mut(offset)
    }

    /// Try to reserve a new buffer, using the provided allocation if necessary.
    pub(crate) fn reserve_new(
        &mut self,
        debug_id: usize,
        size: usize,
        is_oneway: bool,
        pid: Pid,
        alloc: ReserveNewTreeAlloc<T>,
    ) -> Result<(usize, bool)> {
        // Compute new value of free_oneway_space, which is set only on success.
        let new_oneway_space = if is_oneway {
            match self.free_oneway_space.checked_sub(size) {
                Some(new_oneway_space) => new_oneway_space,
                None => return Err(ENOSPC),
            }
        } else {
            self.free_oneway_space
        };

        // Start detecting spammers once we have less than 20%
        // of async space left (which is less than 10% of total
        // buffer size).
        //
        // (This will short-circut, so `low_oneway_space` is
        // only called when necessary.)
        let oneway_spam_detected =
            is_oneway && new_oneway_space < self.size / 10 && self.low_oneway_space(pid);

        let (found_size, found_off, tree_node, free_tree_node) = match self.find_best_match(size) {
            None => {
                pr_warn!("ENOSPC from range_alloc.reserve_new - size: {}", size);
                return Err(ENOSPC);
            }
            Some(desc) => {
                let found_size = desc.size;
                let found_offset = desc.offset;

                // In case we need to break up the descriptor
                let new_desc = Descriptor::new(found_offset + size, found_size - size);
                let (tree_node, free_tree_node, desc_node_res) = alloc.initialize(new_desc);

                desc.state = Some((
                    DescriptorState::new(is_oneway, debug_id, pid),
                    desc_node_res,
                ));
                desc.size = size;

                (found_size, found_offset, tree_node, free_tree_node)
            }
        };
        self.free_oneway_space = new_oneway_space;
        self.free_tree.remove(&(found_size, found_off));

        if found_size != size {
            self.tree.insert(tree_node);
            self.free_tree.insert(free_tree_node);
        }

        Ok((found_off, oneway_spam_detected))
    }

    pub(crate) fn reservation_abort(&mut self, offset: usize) -> Result<FreedRange> {
        let mut cursor = self.tree.cursor_lower_bound(&offset).ok_or_else(|| {
            pr_warn!(
                "EINVAL from range_alloc.reservation_abort - offset: {}",
                offset
            );
            EINVAL
        })?;

        let (_, desc) = cursor.current_mut();

        if desc.offset != offset {
            pr_warn!(
                "EINVAL from range_alloc.reservation_abort - offset: {}",
                offset
            );
            return Err(EINVAL);
        }

        let (reservation, free_node_res) = desc.try_change_state(|state| match state {
            Some((DescriptorState::Reserved(reservation), free_node_res)) => {
                (None, Ok((reservation, free_node_res)))
            }
            None => {
                pr_warn!(
                    "EINVAL from range_alloc.reservation_abort - offset: {}",
                    offset
                );
                (None, Err(EINVAL))
            }
            allocated => {
                pr_warn!(
                    "EPERM from range_alloc.reservation_abort - offset: {}",
                    offset
                );
                (allocated, Err(EPERM))
            }
        })?;

        let mut size = desc.size;
        let mut offset = desc.offset;
        let free_oneway_space_add = if reservation.is_oneway { size } else { 0 };

        self.free_oneway_space += free_oneway_space_add;

        let mut freed_range = FreedRange::interior_pages(offset, size);
        // Compute how large the next free region needs to be to include one more page in
        // the newly freed range.
        let add_next_page_needed = match (offset + size) % PAGE_SIZE {
            0 => usize::MAX,
            unalign => PAGE_SIZE - unalign,
        };
        // Compute how large the previous free region needs to be to include one more page
        // in the newly freed range.
        let add_prev_page_needed = match offset % PAGE_SIZE {
            0 => usize::MAX,
            unalign => unalign,
        };

        // Merge next into current if next is free
        let remove_next = match cursor.peek_next() {
            Some((_, next)) if next.state.is_none() => {
                if next.size >= add_next_page_needed {
                    freed_range.end_page_idx += 1;
                }
                self.free_tree.remove(&(next.size, next.offset));
                size += next.size;
                true
            }
            _ => false,
        };

        if remove_next {
            let (_, desc) = cursor.current_mut();
            desc.size = size;
            cursor.remove_next();
        }

        // Merge current into prev if prev is free
        match cursor.peek_prev_mut() {
            Some((_, prev)) if prev.state.is_none() => {
                if prev.size >= add_prev_page_needed {
                    freed_range.start_page_idx -= 1;
                }
                // merge previous with current, remove current
                self.free_tree.remove(&(prev.size, prev.offset));
                offset = prev.offset;
                size += prev.size;
                prev.size = size;
                cursor.remove_current();
            }
            _ => {}
        };

        self.free_tree
            .insert(free_node_res.into_node((size, offset), ()));

        Ok(freed_range)
    }

    pub(crate) fn reservation_commit(&mut self, offset: usize, data: &mut Option<T>) -> Result {
        let desc = self.tree.get_mut(&offset).ok_or(ENOENT)?;

        desc.try_change_state(|state| match state {
            Some((DescriptorState::Reserved(reservation), free_node_res)) => (
                Some((
                    DescriptorState::Allocated(reservation.allocate(data.take())),
                    free_node_res,
                )),
                Ok(()),
            ),
            other => (other, Err(ENOENT)),
        })
    }

    /// Takes an entry at the given offset from [`DescriptorState::Allocated`] to
    /// [`DescriptorState::Reserved`].
    ///
    /// Returns the size of the existing entry and the data associated with it.
    pub(crate) fn reserve_existing(&mut self, offset: usize) -> Result<(usize, usize, Option<T>)> {
        let desc = self.tree.get_mut(&offset).ok_or_else(|| {
            pr_warn!(
                "ENOENT from range_alloc.reserve_existing - offset: {}",
                offset
            );
            ENOENT
        })?;

        let (debug_id, data) = desc.try_change_state(|state| match state {
            Some((DescriptorState::Allocated(allocation), free_node_res)) => {
                let (reservation, data) = allocation.deallocate();
                let debug_id = reservation.debug_id;
                (
                    Some((DescriptorState::Reserved(reservation), free_node_res)),
                    Ok((debug_id, data)),
                )
            }
            other => {
                pr_warn!(
                    "ENOENT from range_alloc.reserve_existing - offset: {}",
                    offset
                );
                (other, Err(ENOENT))
            }
        })?;

        Ok((desc.size, debug_id, data))
    }

    /// Call the provided callback at every allocated region.
    ///
    /// This destroys the range allocator. Used only during shutdown.
    pub(crate) fn take_for_each<F: Fn(usize, usize, usize, Option<T>)>(&mut self, callback: F) {
        for (_, desc) in self.tree.iter_mut() {
            if let Some((DescriptorState::Allocated(allocation), _)) = &mut desc.state {
                callback(
                    desc.offset,
                    desc.size,
                    allocation.debug_id(),
                    allocation.take(),
                );
            }
        }
    }

    /// Find the amount and size of buffers allocated by the current caller.
    ///
    /// The idea is that once we cross the threshold, whoever is responsible
    /// for the low async space is likely to try to send another async transaction,
    /// and at some point we'll catch them in the act.  This is more efficient
    /// than keeping a map per pid.
    fn low_oneway_space(&self, calling_pid: Pid) -> bool {
        let mut total_alloc_size = 0;
        let mut num_buffers = 0;
        for (_, desc) in self.tree.iter() {
            if let Some((state, _)) = &desc.state {
                if state.is_oneway() && state.pid() == calling_pid {
                    total_alloc_size += desc.size;
                    num_buffers += 1;
                }
            }
        }

        // Warn if this pid has more than 50 transactions, or more than 50% of
        // async space (which is 25% of total buffer size). Oneway spam is only
        // detected when the threshold is exceeded.
        num_buffers > 50 || total_alloc_size > self.size / 4
    }
}

type TreeDescriptorState<T> = (DescriptorState<T>, FreeNodeRes);
struct Descriptor<T> {
    size: usize,
    offset: usize,
    state: Option<TreeDescriptorState<T>>,
}

impl<T> Descriptor<T> {
    fn new(offset: usize, size: usize) -> Self {
        Self {
            size,
            offset,
            state: None,
        }
    }

    fn try_change_state<F, Data>(&mut self, f: F) -> Result<Data>
    where
        F: FnOnce(Option<TreeDescriptorState<T>>) -> (Option<TreeDescriptorState<T>>, Result<Data>),
    {
        let (new_state, result) = f(self.state.take());
        self.state = new_state;
        result
    }
}

// (Descriptor.size, Descriptor.offset)
type FreeKey = (usize, usize);
type FreeNodeRes = RBTreeNodeReservation<FreeKey, ()>;

/// An allocation for use by `reserve_new`.
pub(crate) struct ReserveNewTreeAlloc<T> {
    tree_node_res: RBTreeNodeReservation<usize, Descriptor<T>>,
    free_tree_node_res: FreeNodeRes,
    desc_node_res: FreeNodeRes,
}

impl<T> ReserveNewTreeAlloc<T> {
    pub(crate) fn try_new() -> Result<Self> {
        let tree_node_res = RBTreeNodeReservation::new(GFP_KERNEL)?;
        let free_tree_node_res = RBTreeNodeReservation::new(GFP_KERNEL)?;
        let desc_node_res = RBTreeNodeReservation::new(GFP_KERNEL)?;
        Ok(Self {
            tree_node_res,
            free_tree_node_res,
            desc_node_res,
        })
    }

    fn initialize(
        self,
        desc: Descriptor<T>,
    ) -> (
        RBTreeNode<usize, Descriptor<T>>,
        RBTreeNode<FreeKey, ()>,
        FreeNodeRes,
    ) {
        let size = desc.size;
        let offset = desc.offset;
        (
            self.tree_node_res.into_node(offset, desc),
            self.free_tree_node_res.into_node((size, offset), ()),
            self.desc_node_res,
        )
    }
}

/// An allocation for creating a tree from an `ArrayRangeAllocator`.
pub(crate) struct FromArrayAllocs<T> {
    tree: KVec<RBTreeNodeReservation<usize, Descriptor<T>>>,
    free_tree: KVec<RBTreeNodeReservation<FreeKey, ()>>,
}

impl<T> FromArrayAllocs<T> {
    pub(crate) fn try_new(len: usize) -> Result<Self> {
        let num_descriptors = 2 * len + 1;

        let mut tree = KVec::with_capacity(num_descriptors, GFP_KERNEL)?;
        for _ in 0..num_descriptors {
            tree.push(RBTreeNodeReservation::new(GFP_KERNEL)?, GFP_KERNEL)?;
        }

        let mut free_tree = KVec::with_capacity(num_descriptors, GFP_KERNEL)?;
        for _ in 0..num_descriptors {
            free_tree.push(RBTreeNodeReservation::new(GFP_KERNEL)?, GFP_KERNEL)?;
        }

        Ok(Self { tree, free_tree })
    }
}
