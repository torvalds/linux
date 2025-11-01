// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

//! This module has utilities for managing a page range where unused pages may be reclaimed by a
//! vma shrinker.

// To avoid deadlocks, locks are taken in the order:
//
//  1. mmap lock
//  2. spinlock
//  3. lru spinlock
//
// The shrinker will use trylock methods because it locks them in a different order.

use core::{
    marker::PhantomPinned,
    mem::{size_of, size_of_val, MaybeUninit},
    ptr,
};

use kernel::{
    bindings,
    error::Result,
    ffi::{c_ulong, c_void},
    mm::{virt, Mm, MmWithUser},
    new_mutex, new_spinlock,
    page::{Page, PAGE_SHIFT, PAGE_SIZE},
    prelude::*,
    str::CStr,
    sync::{aref::ARef, Mutex, SpinLock},
    task::Pid,
    transmute::FromBytes,
    types::Opaque,
    uaccess::UserSliceReader,
};

/// Represents a shrinker that can be registered with the kernel.
///
/// Each shrinker can be used by many `ShrinkablePageRange` objects.
#[repr(C)]
pub(crate) struct Shrinker {
    inner: Opaque<*mut bindings::shrinker>,
    list_lru: Opaque<bindings::list_lru>,
}

// SAFETY: The shrinker and list_lru are thread safe.
unsafe impl Send for Shrinker {}
// SAFETY: The shrinker and list_lru are thread safe.
unsafe impl Sync for Shrinker {}

impl Shrinker {
    /// Create a new shrinker.
    ///
    /// # Safety
    ///
    /// Before using this shrinker with a `ShrinkablePageRange`, the `register` method must have
    /// been called exactly once, and it must not have returned an error.
    pub(crate) const unsafe fn new() -> Self {
        Self {
            inner: Opaque::uninit(),
            list_lru: Opaque::uninit(),
        }
    }

    /// Register this shrinker with the kernel.
    pub(crate) fn register(&'static self, name: &CStr) -> Result<()> {
        // SAFETY: These fields are not yet used, so it's okay to zero them.
        unsafe {
            self.inner.get().write(ptr::null_mut());
            self.list_lru.get().write_bytes(0, 1);
        }

        // SAFETY: The field is not yet used, so we can initialize it.
        let ret = unsafe { bindings::__list_lru_init(self.list_lru.get(), false, ptr::null_mut()) };
        if ret != 0 {
            return Err(Error::from_errno(ret));
        }

        // SAFETY: The `name` points at a valid c string.
        let shrinker = unsafe { bindings::shrinker_alloc(0, name.as_char_ptr()) };
        if shrinker.is_null() {
            // SAFETY: We initialized it, so its okay to destroy it.
            unsafe { bindings::list_lru_destroy(self.list_lru.get()) };
            return Err(Error::from_errno(ret));
        }

        // SAFETY: We're about to register the shrinker, and these are the fields we need to
        // initialize. (All other fields are already zeroed.)
        unsafe {
            (&raw mut (*shrinker).count_objects).write(Some(rust_shrink_count));
            (&raw mut (*shrinker).scan_objects).write(Some(rust_shrink_scan));
            (&raw mut (*shrinker).private_data).write(self.list_lru.get().cast());
        }

        // SAFETY: The new shrinker has been fully initialized, so we can register it.
        unsafe { bindings::shrinker_register(shrinker) };

        // SAFETY: This initializes the pointer to the shrinker so that we can use it.
        unsafe { self.inner.get().write(shrinker) };

        Ok(())
    }
}

/// A container that manages a page range in a vma.
///
/// The pages can be thought of as an array of booleans of whether the pages are usable. The
/// methods `use_range` and `stop_using_range` set all booleans in a range to true or false
/// respectively. Initially, no pages are allocated. When a page is not used, it is not freed
/// immediately. Instead, it is made available to the memory shrinker to free it if the device is
/// under memory pressure.
///
/// It's okay for `use_range` and `stop_using_range` to race with each other, although there's no
/// way to know whether an index ends up with true or false if a call to `use_range` races with
/// another call to `stop_using_range` on a given index.
///
/// It's also okay for the two methods to race with themselves, e.g. if two threads call
/// `use_range` on the same index, then that's fine and neither call will return until the page is
/// allocated and mapped.
///
/// The methods that read or write to a range require that the page is marked as in use. So it is
/// _not_ okay to call `stop_using_range` on a page that is in use by the methods that read or
/// write to the page.
#[pin_data(PinnedDrop)]
pub(crate) struct ShrinkablePageRange {
    /// Shrinker object registered with the kernel.
    shrinker: &'static Shrinker,
    /// Pid using this page range. Only used as debugging information.
    pid: Pid,
    /// The mm for the relevant process.
    mm: ARef<Mm>,
    /// Used to synchronize calls to `vm_insert_page` and `zap_page_range_single`.
    #[pin]
    mm_lock: Mutex<()>,
    /// Spinlock protecting changes to pages.
    #[pin]
    lock: SpinLock<Inner>,

    /// Must not move, since page info has pointers back.
    #[pin]
    _pin: PhantomPinned,
}

struct Inner {
    /// Array of pages.
    ///
    /// Since this is also accessed by the shrinker, we can't use a `Box`, which asserts exclusive
    /// ownership. To deal with that, we manage it using raw pointers.
    pages: *mut PageInfo,
    /// Length of the `pages` array.
    size: usize,
    /// The address of the vma to insert the pages into.
    vma_addr: usize,
}

// SAFETY: proper locking is in place for `Inner`
unsafe impl Send for Inner {}

type StableMmGuard =
    kernel::sync::lock::Guard<'static, (), kernel::sync::lock::mutex::MutexBackend>;

/// An array element that describes the current state of a page.
///
/// There are three states:
///
///  * Free. The page is None. The `lru` element is not queued.
///  * Available. The page is Some. The `lru` element is queued to the shrinker's lru.
///  * Used. The page is Some. The `lru` element is not queued.
///
/// When an element is available, the shrinker is able to free the page.
#[repr(C)]
struct PageInfo {
    lru: bindings::list_head,
    page: Option<Page>,
    range: *const ShrinkablePageRange,
}

impl PageInfo {
    /// # Safety
    ///
    /// The caller ensures that writing to `me.page` is ok, and that the page is not currently set.
    unsafe fn set_page(me: *mut PageInfo, page: Page) {
        // SAFETY: This pointer offset is in bounds.
        let ptr = unsafe { &raw mut (*me).page };

        // SAFETY: The pointer is valid for writing, so also valid for reading.
        if unsafe { (*ptr).is_some() } {
            pr_err!("set_page called when there is already a page");
            // SAFETY: We will initialize the page again below.
            unsafe { ptr::drop_in_place(ptr) };
        }

        // SAFETY: The pointer is valid for writing.
        unsafe { ptr::write(ptr, Some(page)) };
    }

    /// # Safety
    ///
    /// The caller ensures that reading from `me.page` is ok for the duration of 'a.
    unsafe fn get_page<'a>(me: *const PageInfo) -> Option<&'a Page> {
        // SAFETY: This pointer offset is in bounds.
        let ptr = unsafe { &raw const (*me).page };

        // SAFETY: The pointer is valid for reading.
        unsafe { (*ptr).as_ref() }
    }

    /// # Safety
    ///
    /// The caller ensures that writing to `me.page` is ok for the duration of 'a.
    unsafe fn take_page(me: *mut PageInfo) -> Option<Page> {
        // SAFETY: This pointer offset is in bounds.
        let ptr = unsafe { &raw mut (*me).page };

        // SAFETY: The pointer is valid for reading.
        unsafe { (*ptr).take() }
    }

    /// Add this page to the lru list, if not already in the list.
    ///
    /// # Safety
    ///
    /// The pointer must be valid, and it must be the right shrinker and nid.
    unsafe fn list_lru_add(me: *mut PageInfo, nid: i32, shrinker: &'static Shrinker) {
        // SAFETY: This pointer offset is in bounds.
        let lru_ptr = unsafe { &raw mut (*me).lru };
        // SAFETY: The lru pointer is valid, and we're not using it with any other lru list.
        unsafe { bindings::list_lru_add(shrinker.list_lru.get(), lru_ptr, nid, ptr::null_mut()) };
    }

    /// Remove this page from the lru list, if it is in the list.
    ///
    /// # Safety
    ///
    /// The pointer must be valid, and it must be the right shrinker and nid.
    unsafe fn list_lru_del(me: *mut PageInfo, nid: i32, shrinker: &'static Shrinker) {
        // SAFETY: This pointer offset is in bounds.
        let lru_ptr = unsafe { &raw mut (*me).lru };
        // SAFETY: The lru pointer is valid, and we're not using it with any other lru list.
        unsafe { bindings::list_lru_del(shrinker.list_lru.get(), lru_ptr, nid, ptr::null_mut()) };
    }
}

impl ShrinkablePageRange {
    /// Create a new `ShrinkablePageRange` using the given shrinker.
    pub(crate) fn new(shrinker: &'static Shrinker) -> impl PinInit<Self, Error> {
        try_pin_init!(Self {
            shrinker,
            pid: kernel::current!().pid(),
            mm: ARef::from(&**kernel::current!().mm().ok_or(ESRCH)?),
            mm_lock <- new_mutex!((), "ShrinkablePageRange::mm"),
            lock <- new_spinlock!(Inner {
                pages: ptr::null_mut(),
                size: 0,
                vma_addr: 0,
            }, "ShrinkablePageRange"),
            _pin: PhantomPinned,
        })
    }

    pub(crate) fn stable_trylock_mm(&self) -> Option<StableMmGuard> {
        // SAFETY: This extends the duration of the reference. Since this call happens before
        // `mm_lock` is taken in the destructor of `ShrinkablePageRange`, the destructor will block
        // until the returned guard is dropped. This ensures that the guard is valid until dropped.
        let mm_lock = unsafe { &*ptr::from_ref(&self.mm_lock) };

        mm_lock.try_lock()
    }

    /// Register a vma with this page range. Returns the size of the region.
    pub(crate) fn register_with_vma(&self, vma: &virt::VmaNew) -> Result<usize> {
        let num_bytes = usize::min(vma.end() - vma.start(), bindings::SZ_4M as usize);
        let num_pages = num_bytes >> PAGE_SHIFT;

        if !ptr::eq::<Mm>(&*self.mm, &**vma.mm()) {
            pr_debug!("Failed to register with vma: invalid vma->vm_mm");
            return Err(EINVAL);
        }
        if num_pages == 0 {
            pr_debug!("Failed to register with vma: size zero");
            return Err(EINVAL);
        }

        let mut pages = KVVec::<PageInfo>::with_capacity(num_pages, GFP_KERNEL)?;

        // SAFETY: This just initializes the pages array.
        unsafe {
            let self_ptr = self as *const ShrinkablePageRange;
            for i in 0..num_pages {
                let info = pages.as_mut_ptr().add(i);
                (&raw mut (*info).range).write(self_ptr);
                (&raw mut (*info).page).write(None);
                let lru = &raw mut (*info).lru;
                (&raw mut (*lru).next).write(lru);
                (&raw mut (*lru).prev).write(lru);
            }
        }

        let mut inner = self.lock.lock();
        if inner.size > 0 {
            pr_debug!("Failed to register with vma: already registered");
            drop(inner);
            return Err(EBUSY);
        }

        inner.pages = pages.into_raw_parts().0;
        inner.size = num_pages;
        inner.vma_addr = vma.start();

        Ok(num_pages)
    }

    /// Make sure that the given pages are allocated and mapped.
    ///
    /// Must not be called from an atomic context.
    pub(crate) fn use_range(&self, start: usize, end: usize) -> Result<()> {
        if start >= end {
            return Ok(());
        }
        let mut inner = self.lock.lock();
        assert!(end <= inner.size);

        for i in start..end {
            // SAFETY: This pointer offset is in bounds.
            let page_info = unsafe { inner.pages.add(i) };

            // SAFETY: The pointer is valid, and we hold the lock so reading from the page is okay.
            if let Some(page) = unsafe { PageInfo::get_page(page_info) } {
                // Since we're going to use the page, we should remove it from the lru list so that
                // the shrinker will not free it.
                //
                // SAFETY: The pointer is valid, and this is the right shrinker.
                //
                // The shrinker can't free the page between the check and this call to
                // `list_lru_del` because we hold the lock.
                unsafe { PageInfo::list_lru_del(page_info, page.nid(), self.shrinker) };
            } else {
                // We have to allocate a new page. Use the slow path.
                drop(inner);
                // SAFETY: `i < end <= inner.size` so `i` is in bounds.
                match unsafe { self.use_page_slow(i) } {
                    Ok(()) => {}
                    Err(err) => {
                        pr_warn!("Error in use_page_slow: {:?}", err);
                        return Err(err);
                    }
                }
                inner = self.lock.lock();
            }
        }
        Ok(())
    }

    /// Mark the given page as in use, slow path.
    ///
    /// Must not be called from an atomic context.
    ///
    /// # Safety
    ///
    /// Assumes that `i` is in bounds.
    #[cold]
    unsafe fn use_page_slow(&self, i: usize) -> Result<()> {
        let new_page = Page::alloc_page(GFP_KERNEL | __GFP_HIGHMEM | __GFP_ZERO)?;

        let mm_mutex = self.mm_lock.lock();
        let inner = self.lock.lock();

        // SAFETY: This pointer offset is in bounds.
        let page_info = unsafe { inner.pages.add(i) };

        // SAFETY: The pointer is valid, and we hold the lock so reading from the page is okay.
        if let Some(page) = unsafe { PageInfo::get_page(page_info) } {
            // The page was already there, or someone else added the page while we didn't hold the
            // spinlock.
            //
            // SAFETY: The pointer is valid, and this is the right shrinker.
            //
            // The shrinker can't free the page between the check and this call to
            // `list_lru_del` because we hold the lock.
            unsafe { PageInfo::list_lru_del(page_info, page.nid(), self.shrinker) };
            return Ok(());
        }

        let vma_addr = inner.vma_addr;
        // Release the spinlock while we insert the page into the vma.
        drop(inner);

        // No overflow since we stay in bounds of the vma.
        let user_page_addr = vma_addr + (i << PAGE_SHIFT);

        // We use `mmput_async` when dropping the `mm` because `use_page_slow` is usually used from
        // a remote process. If the call to `mmput` races with the process shutting down, then the
        // caller of `use_page_slow` becomes responsible for cleaning up the `mm`, which doesn't
        // happen until it returns to userspace. However, the caller might instead go to sleep and
        // wait for the owner of the `mm` to wake it up, which doesn't happen because it's in the
        // middle of a shutdown process that won't complete until the `mm` is dropped. This can
        // amount to a deadlock.
        //
        // Using `mmput_async` avoids this, because then the `mm` cleanup is instead queued to a
        // workqueue.
        MmWithUser::into_mmput_async(self.mm.mmget_not_zero().ok_or(ESRCH)?)
            .mmap_read_lock()
            .vma_lookup(vma_addr)
            .ok_or(ESRCH)?
            .as_mixedmap_vma()
            .ok_or(ESRCH)?
            .vm_insert_page(user_page_addr, &new_page)
            .inspect_err(|err| {
                pr_warn!(
                    "Failed to vm_insert_page({}): vma_addr:{} i:{} err:{:?}",
                    user_page_addr,
                    vma_addr,
                    i,
                    err
                )
            })?;

        let inner = self.lock.lock();

        // SAFETY: The `page_info` pointer is valid and currently does not have a page. The page
        // can be written to since we hold the lock.
        //
        // We released and reacquired the spinlock since we checked that the page is null, but we
        // always hold the mm_lock mutex when setting the page to a non-null value, so it's not
        // possible for someone else to have changed it since our check.
        unsafe { PageInfo::set_page(page_info, new_page) };

        drop(inner);
        drop(mm_mutex);

        Ok(())
    }

    /// If the given page is in use, then mark it as available so that the shrinker can free it.
    ///
    /// May be called from an atomic context.
    pub(crate) fn stop_using_range(&self, start: usize, end: usize) {
        if start >= end {
            return;
        }
        let inner = self.lock.lock();
        assert!(end <= inner.size);

        for i in (start..end).rev() {
            // SAFETY: The pointer is in bounds.
            let page_info = unsafe { inner.pages.add(i) };

            // SAFETY: Okay for reading since we have the lock.
            if let Some(page) = unsafe { PageInfo::get_page(page_info) } {
                // SAFETY: The pointer is valid, and it's the right shrinker.
                unsafe { PageInfo::list_lru_add(page_info, page.nid(), self.shrinker) };
            }
        }
    }

    /// Helper for reading or writing to a range of bytes that may overlap with several pages.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    unsafe fn iterate<T>(&self, mut offset: usize, mut size: usize, mut cb: T) -> Result
    where
        T: FnMut(&Page, usize, usize) -> Result,
    {
        if size == 0 {
            return Ok(());
        }

        let (pages, num_pages) = {
            let inner = self.lock.lock();
            (inner.pages, inner.size)
        };
        let num_bytes = num_pages << PAGE_SHIFT;

        // Check that the request is within the buffer.
        if offset.checked_add(size).ok_or(EFAULT)? > num_bytes {
            return Err(EFAULT);
        }

        let mut page_index = offset >> PAGE_SHIFT;
        offset &= PAGE_SIZE - 1;
        while size > 0 {
            let available = usize::min(size, PAGE_SIZE - offset);
            // SAFETY: The pointer is in bounds.
            let page_info = unsafe { pages.add(page_index) };
            // SAFETY: The caller guarantees that this page is in the "in use" state for the
            // duration of this call to `iterate`, so nobody will change the page.
            let page = unsafe { PageInfo::get_page(page_info) };
            if page.is_none() {
                pr_warn!("Page is null!");
            }
            let page = page.ok_or(EFAULT)?;
            cb(page, offset, available)?;
            size -= available;
            page_index += 1;
            offset = 0;
        }
        Ok(())
    }

    /// Copy from userspace into this page range.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    pub(crate) unsafe fn copy_from_user_slice(
        &self,
        reader: &mut UserSliceReader,
        offset: usize,
        size: usize,
    ) -> Result {
        // SAFETY: `self.iterate` has the same safety requirements as `copy_from_user_slice`.
        unsafe {
            self.iterate(offset, size, |page, offset, to_copy| {
                page.copy_from_user_slice_raw(reader, offset, to_copy)
            })
        }
    }

    /// Copy from this page range into kernel space.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    pub(crate) unsafe fn read<T: FromBytes>(&self, offset: usize) -> Result<T> {
        let mut out = MaybeUninit::<T>::uninit();
        let mut out_offset = 0;
        // SAFETY: `self.iterate` has the same safety requirements as `read`.
        unsafe {
            self.iterate(offset, size_of::<T>(), |page, offset, to_copy| {
                // SAFETY: The sum of `offset` and `to_copy` is bounded by the size of T.
                let obj_ptr = (out.as_mut_ptr() as *mut u8).add(out_offset);
                // SAFETY: The pointer points is in-bounds of the `out` variable, so it is valid.
                page.read_raw(obj_ptr, offset, to_copy)?;
                out_offset += to_copy;
                Ok(())
            })?;
        }
        // SAFETY: We just initialised the data.
        Ok(unsafe { out.assume_init() })
    }

    /// Copy from kernel space into this page range.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    pub(crate) unsafe fn write<T: ?Sized>(&self, offset: usize, obj: &T) -> Result {
        let mut obj_offset = 0;
        // SAFETY: `self.iterate` has the same safety requirements as `write`.
        unsafe {
            self.iterate(offset, size_of_val(obj), |page, offset, to_copy| {
                // SAFETY: The sum of `offset` and `to_copy` is bounded by the size of T.
                let obj_ptr = (obj as *const T as *const u8).add(obj_offset);
                // SAFETY: We have a reference to the object, so the pointer is valid.
                page.write_raw(obj_ptr, offset, to_copy)?;
                obj_offset += to_copy;
                Ok(())
            })
        }
    }

    /// Write zeroes to the given range.
    ///
    /// # Safety
    ///
    /// All pages touched by this operation must be in use for the duration of this call.
    pub(crate) unsafe fn fill_zero(&self, offset: usize, size: usize) -> Result {
        // SAFETY: `self.iterate` has the same safety requirements as `copy_into`.
        unsafe {
            self.iterate(offset, size, |page, offset, len| {
                page.fill_zero_raw(offset, len)
            })
        }
    }
}

#[pinned_drop]
impl PinnedDrop for ShrinkablePageRange {
    fn drop(self: Pin<&mut Self>) {
        let (pages, size) = {
            let lock = self.lock.lock();
            (lock.pages, lock.size)
        };

        if size == 0 {
            return;
        }

        // Note: This call is also necessary for the safety of `stable_trylock_mm`.
        let mm_lock = self.mm_lock.lock();

        // This is the destructor, so unlike the other methods, we only need to worry about races
        // with the shrinker here. Since we hold the `mm_lock`, we also can't race with the
        // shrinker, and after this loop, the shrinker will not access any of our pages since we
        // removed them from the lru list.
        for i in 0..size {
            // SAFETY: Loop is in-bounds of the size.
            let p_ptr = unsafe { pages.add(i) };
            // SAFETY: No other readers, so we can read.
            if let Some(p) = unsafe { PageInfo::get_page(p_ptr) } {
                // SAFETY: The pointer is valid and it's the right shrinker.
                unsafe { PageInfo::list_lru_del(p_ptr, p.nid(), self.shrinker) };
            }
        }

        drop(mm_lock);

        // SAFETY: `pages` was allocated as an `KVVec<PageInfo>` with capacity `size`. Furthermore,
        // all `size` elements are initialized. Also, the array is no longer shared with the
        // shrinker due to the above loop.
        drop(unsafe { KVVec::from_raw_parts(pages, size, size) });
    }
}

/// # Safety
/// Called by the shrinker.
#[no_mangle]
unsafe extern "C" fn rust_shrink_count(
    shrink: *mut bindings::shrinker,
    _sc: *mut bindings::shrink_control,
) -> c_ulong {
    // SAFETY: We can access our own private data.
    let list_lru = unsafe { (*shrink).private_data.cast::<bindings::list_lru>() };
    // SAFETY: Accessing the lru list is okay. Just an FFI call.
    unsafe { bindings::list_lru_count(list_lru) }
}

/// # Safety
/// Called by the shrinker.
#[no_mangle]
unsafe extern "C" fn rust_shrink_scan(
    shrink: *mut bindings::shrinker,
    sc: *mut bindings::shrink_control,
) -> c_ulong {
    // SAFETY: We can access our own private data.
    let list_lru = unsafe { (*shrink).private_data.cast::<bindings::list_lru>() };
    // SAFETY: Caller guarantees that it is safe to read this field.
    let nr_to_scan = unsafe { (*sc).nr_to_scan };
    // SAFETY: Accessing the lru list is okay. Just an FFI call.
    unsafe {
        bindings::list_lru_walk(
            list_lru,
            Some(bindings::rust_shrink_free_page_wrap),
            ptr::null_mut(),
            nr_to_scan,
        )
    }
}

const LRU_SKIP: bindings::lru_status = bindings::lru_status_LRU_SKIP;
const LRU_REMOVED_ENTRY: bindings::lru_status = bindings::lru_status_LRU_REMOVED_RETRY;

/// # Safety
/// Called by the shrinker.
#[no_mangle]
unsafe extern "C" fn rust_shrink_free_page(
    item: *mut bindings::list_head,
    lru: *mut bindings::list_lru_one,
    _cb_arg: *mut c_void,
) -> bindings::lru_status {
    // Fields that should survive after unlocking the lru lock.
    let page;
    let page_index;
    let mm;
    let mmap_read;
    let mm_mutex;
    let vma_addr;

    {
        // CAST: The `list_head` field is first in `PageInfo`.
        let info = item as *mut PageInfo;
        // SAFETY: The `range` field of `PageInfo` is immutable.
        let range = unsafe { &*((*info).range) };

        mm = match range.mm.mmget_not_zero() {
            Some(mm) => MmWithUser::into_mmput_async(mm),
            None => return LRU_SKIP,
        };

        mm_mutex = match range.stable_trylock_mm() {
            Some(guard) => guard,
            None => return LRU_SKIP,
        };

        mmap_read = match mm.mmap_read_trylock() {
            Some(guard) => guard,
            None => return LRU_SKIP,
        };

        // We can't lock it normally here, since we hold the lru lock.
        let inner = match range.lock.try_lock() {
            Some(inner) => inner,
            None => return LRU_SKIP,
        };

        // SAFETY: The item is in this lru list, so it's okay to remove it.
        unsafe { bindings::list_lru_isolate(lru, item) };

        // SAFETY: Both pointers are in bounds of the same allocation.
        page_index = unsafe { info.offset_from(inner.pages) } as usize;

        // SAFETY: We hold the spinlock, so we can take the page.
        //
        // This sets the page pointer to zero before we unmap it from the vma. However, we call
        // `zap_page_range` before we release the mmap lock, so `use_page_slow` will not be able to
        // insert a new page until after our call to `zap_page_range`.
        page = unsafe { PageInfo::take_page(info) };
        vma_addr = inner.vma_addr;

        // From this point on, we don't access this PageInfo or ShrinkablePageRange again, because
        // they can be freed at any point after we unlock `lru_lock`. This is with the exception of
        // `mm_mutex` which is kept alive by holding the lock.
    }

    // SAFETY: The lru lock is locked when this method is called.
    unsafe { bindings::spin_unlock(&raw mut (*lru).lock) };

    if let Some(vma) = mmap_read.vma_lookup(vma_addr) {
        let user_page_addr = vma_addr + (page_index << PAGE_SHIFT);
        vma.zap_page_range_single(user_page_addr, PAGE_SIZE);
    }

    drop(mmap_read);
    drop(mm_mutex);
    drop(mm);
    drop(page);

    // SAFETY: We just unlocked the lru lock, but it should be locked when we return.
    unsafe { bindings::spin_lock(&raw mut (*lru).lock) };

    LRU_REMOVED_ENTRY
}
