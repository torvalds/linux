// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use kernel::{
    page::{PAGE_MASK, PAGE_SIZE},
    prelude::*,
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
pub(super) struct ArrayRangeAllocator<T> {
    /// This stores all ranges that are allocated. Unlike the tree based allocator, we do *not*
    /// store the free ranges.
    ///
    /// Sorted by offset.
    pub(super) ranges: KVec<Range<T>>,
    size: usize,
    free_oneway_space: usize,
}

struct FindEmptyRes {
    /// Which index in `ranges` should we insert the new range at?
    ///
    /// Inserting the new range at this index keeps `ranges` sorted.
    insert_at_idx: usize,
    /// Which offset should we insert the new range at?
    insert_at_offset: usize,
}

impl<T> ArrayRangeAllocator<T> {
    pub(crate) fn new(size: usize, alloc: EmptyArrayAlloc<T>) -> Self {
        Self {
            ranges: alloc.ranges,
            size,
            free_oneway_space: size / 2,
        }
    }

    pub(crate) fn free_oneway_space(&self) -> usize {
        self.free_oneway_space
    }

    pub(crate) fn count_buffers(&self) -> usize {
        self.ranges.len()
    }

    pub(crate) fn total_size(&self) -> usize {
        self.size
    }

    pub(crate) fn is_full(&self) -> bool {
        self.ranges.len() == self.ranges.capacity()
    }

    pub(crate) fn debug_print(&self, m: &SeqFile) -> Result<()> {
        for range in &self.ranges {
            seq_print!(
                m,
                "  buffer {}: {} size {} pid {} oneway {}",
                0,
                range.offset,
                range.size,
                range.state.pid(),
                range.state.is_oneway(),
            );
            if let DescriptorState::Reserved(_) = range.state {
                seq_print!(m, " reserved\n");
            } else {
                seq_print!(m, " allocated\n");
            }
        }
        Ok(())
    }

    /// Find somewhere to put a new range.
    ///
    /// Unlike the tree implementation, we do not bother to find the smallest gap. The idea is that
    /// fragmentation isn't a big issue when we don't have many ranges.
    ///
    /// Returns the index that the new range should have in `self.ranges` after insertion.
    fn find_empty_range(&self, size: usize) -> Option<FindEmptyRes> {
        let after_last_range = self.ranges.last().map(Range::endpoint).unwrap_or(0);

        if size <= self.total_size() - after_last_range {
            // We can put the range at the end, so just do that.
            Some(FindEmptyRes {
                insert_at_idx: self.ranges.len(),
                insert_at_offset: after_last_range,
            })
        } else {
            let mut end_of_prev = 0;
            for (i, range) in self.ranges.iter().enumerate() {
                // Does it fit before the i'th range?
                if size <= range.offset - end_of_prev {
                    return Some(FindEmptyRes {
                        insert_at_idx: i,
                        insert_at_offset: end_of_prev,
                    });
                }
                end_of_prev = range.endpoint();
            }
            None
        }
    }

    pub(crate) fn reserve_new(
        &mut self,
        debug_id: usize,
        size: usize,
        is_oneway: bool,
        pid: Pid,
    ) -> Result<usize> {
        // Compute new value of free_oneway_space, which is set only on success.
        let new_oneway_space = if is_oneway {
            match self.free_oneway_space.checked_sub(size) {
                Some(new_oneway_space) => new_oneway_space,
                None => return Err(ENOSPC),
            }
        } else {
            self.free_oneway_space
        };

        let FindEmptyRes {
            insert_at_idx,
            insert_at_offset,
        } = self.find_empty_range(size).ok_or(ENOSPC)?;
        self.free_oneway_space = new_oneway_space;

        let new_range = Range {
            offset: insert_at_offset,
            size,
            state: DescriptorState::new(is_oneway, debug_id, pid),
        };
        // Insert the value at the given index to keep the array sorted.
        self.ranges
            .insert_within_capacity(insert_at_idx, new_range)
            .ok()
            .unwrap();

        Ok(insert_at_offset)
    }

    pub(crate) fn reservation_abort(&mut self, offset: usize) -> Result<FreedRange> {
        // This could use a binary search, but linear scans are usually faster for small arrays.
        let i = self
            .ranges
            .iter()
            .position(|range| range.offset == offset)
            .ok_or(EINVAL)?;
        let range = &self.ranges[i];

        if let DescriptorState::Allocated(_) = range.state {
            return Err(EPERM);
        }

        let size = range.size;
        let offset = range.offset;

        if range.state.is_oneway() {
            self.free_oneway_space += size;
        }

        // This computes the range of pages that are no longer used by *any* allocated range. The
        // caller will mark them as unused, which means that they can be freed if the system comes
        // under memory pressure.
        let mut freed_range = FreedRange::interior_pages(offset, size);
        #[expect(clippy::collapsible_if)] // reads better like this
        if offset % PAGE_SIZE != 0 {
            if i == 0 || self.ranges[i - 1].endpoint() <= (offset & PAGE_MASK) {
                freed_range.start_page_idx -= 1;
            }
        }
        if range.endpoint() % PAGE_SIZE != 0 {
            let page_after = (range.endpoint() & PAGE_MASK) + PAGE_SIZE;
            if i + 1 == self.ranges.len() || page_after <= self.ranges[i + 1].offset {
                freed_range.end_page_idx += 1;
            }
        }

        self.ranges.remove(i)?;
        Ok(freed_range)
    }

    pub(crate) fn reservation_commit(&mut self, offset: usize, data: &mut Option<T>) -> Result {
        // This could use a binary search, but linear scans are usually faster for small arrays.
        let range = self
            .ranges
            .iter_mut()
            .find(|range| range.offset == offset)
            .ok_or(ENOENT)?;

        let DescriptorState::Reserved(reservation) = &range.state else {
            return Err(ENOENT);
        };

        range.state = DescriptorState::Allocated(reservation.clone().allocate(data.take()));
        Ok(())
    }

    pub(crate) fn reserve_existing(&mut self, offset: usize) -> Result<(usize, usize, Option<T>)> {
        // This could use a binary search, but linear scans are usually faster for small arrays.
        let range = self
            .ranges
            .iter_mut()
            .find(|range| range.offset == offset)
            .ok_or(ENOENT)?;

        let DescriptorState::Allocated(allocation) = &mut range.state else {
            return Err(ENOENT);
        };

        let data = allocation.take();
        let debug_id = allocation.reservation.debug_id;
        range.state = DescriptorState::Reserved(allocation.reservation.clone());
        Ok((range.size, debug_id, data))
    }

    pub(crate) fn take_for_each<F: Fn(usize, usize, usize, Option<T>)>(&mut self, callback: F) {
        for range in self.ranges.iter_mut() {
            if let DescriptorState::Allocated(allocation) = &mut range.state {
                callback(
                    range.offset,
                    range.size,
                    allocation.reservation.debug_id,
                    allocation.data.take(),
                );
            }
        }
    }
}

pub(crate) struct EmptyArrayAlloc<T> {
    ranges: KVec<Range<T>>,
}

impl<T> EmptyArrayAlloc<T> {
    pub(crate) fn try_new(capacity: usize) -> Result<Self> {
        Ok(Self {
            ranges: KVec::with_capacity(capacity, GFP_KERNEL)?,
        })
    }
}
