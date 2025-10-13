// SPDX-License-Identifier: GPL-2.0

// Copyright (C) 2025 Google LLC.

use core::mem::{size_of, size_of_val, MaybeUninit};
use core::ops::Range;

use kernel::{
    bindings,
    fs::file::{File, FileDescriptorReservation},
    prelude::*,
    sync::{aref::ARef, Arc},
    transmute::{AsBytes, FromBytes},
    uaccess::UserSliceReader,
    uapi,
};

use crate::{
    deferred_close::DeferredFdCloser,
    defs::*,
    node::{Node, NodeRef},
    process::Process,
    DArc,
};

#[derive(Default)]
pub(crate) struct AllocationInfo {
    /// Range within the allocation where we can find the offsets to the object descriptors.
    pub(crate) offsets: Option<Range<usize>>,
    /// The target node of the transaction this allocation is associated to.
    /// Not set for replies.
    pub(crate) target_node: Option<NodeRef>,
    /// When this allocation is dropped, call `pending_oneway_finished` on the node.
    ///
    /// This is used to serialize oneway transaction on the same node. Binder guarantees that
    /// oneway transactions to the same node are delivered sequentially in the order they are sent.
    pub(crate) oneway_node: Option<DArc<Node>>,
    /// Zero the data in the buffer on free.
    pub(crate) clear_on_free: bool,
    /// List of files embedded in this transaction.
    file_list: FileList,
}

/// Represents an allocation that the kernel is currently using.
///
/// When allocations are idle, the range allocator holds the data related to them.
///
/// # Invariants
///
/// This allocation corresponds to an allocation in the range allocator, so the relevant pages are
/// marked in use in the page range.
pub(crate) struct Allocation {
    pub(crate) offset: usize,
    size: usize,
    pub(crate) ptr: usize,
    pub(crate) process: Arc<Process>,
    allocation_info: Option<AllocationInfo>,
    free_on_drop: bool,
    pub(crate) oneway_spam_detected: bool,
    #[allow(dead_code)]
    pub(crate) debug_id: usize,
}

impl Allocation {
    pub(crate) fn new(
        process: Arc<Process>,
        debug_id: usize,
        offset: usize,
        size: usize,
        ptr: usize,
        oneway_spam_detected: bool,
    ) -> Self {
        Self {
            process,
            offset,
            size,
            ptr,
            debug_id,
            oneway_spam_detected,
            allocation_info: None,
            free_on_drop: true,
        }
    }

    fn size_check(&self, offset: usize, size: usize) -> Result {
        let overflow_fail = offset.checked_add(size).is_none();
        let cmp_size_fail = offset.wrapping_add(size) > self.size;
        if overflow_fail || cmp_size_fail {
            return Err(EFAULT);
        }
        Ok(())
    }

    pub(crate) fn copy_into(
        &self,
        reader: &mut UserSliceReader,
        offset: usize,
        size: usize,
    ) -> Result {
        self.size_check(offset, size)?;

        // SAFETY: While this object exists, the range allocator will keep the range allocated, and
        // in turn, the pages will be marked as in use.
        unsafe {
            self.process
                .pages
                .copy_from_user_slice(reader, self.offset + offset, size)
        }
    }

    pub(crate) fn read<T: FromBytes>(&self, offset: usize) -> Result<T> {
        self.size_check(offset, size_of::<T>())?;

        // SAFETY: While this object exists, the range allocator will keep the range allocated, and
        // in turn, the pages will be marked as in use.
        unsafe { self.process.pages.read(self.offset + offset) }
    }

    pub(crate) fn write<T: ?Sized>(&self, offset: usize, obj: &T) -> Result {
        self.size_check(offset, size_of_val::<T>(obj))?;

        // SAFETY: While this object exists, the range allocator will keep the range allocated, and
        // in turn, the pages will be marked as in use.
        unsafe { self.process.pages.write(self.offset + offset, obj) }
    }

    pub(crate) fn fill_zero(&self) -> Result {
        // SAFETY: While this object exists, the range allocator will keep the range allocated, and
        // in turn, the pages will be marked as in use.
        unsafe { self.process.pages.fill_zero(self.offset, self.size) }
    }

    pub(crate) fn keep_alive(mut self) {
        self.process
            .buffer_make_freeable(self.offset, self.allocation_info.take());
        self.free_on_drop = false;
    }

    pub(crate) fn set_info(&mut self, info: AllocationInfo) {
        self.allocation_info = Some(info);
    }

    pub(crate) fn get_or_init_info(&mut self) -> &mut AllocationInfo {
        self.allocation_info.get_or_insert_with(Default::default)
    }

    pub(crate) fn set_info_offsets(&mut self, offsets: Range<usize>) {
        self.get_or_init_info().offsets = Some(offsets);
    }

    pub(crate) fn set_info_oneway_node(&mut self, oneway_node: DArc<Node>) {
        self.get_or_init_info().oneway_node = Some(oneway_node);
    }

    pub(crate) fn set_info_clear_on_drop(&mut self) {
        self.get_or_init_info().clear_on_free = true;
    }

    pub(crate) fn set_info_target_node(&mut self, target_node: NodeRef) {
        self.get_or_init_info().target_node = Some(target_node);
    }

    /// Reserve enough space to push at least `num_fds` fds.
    pub(crate) fn info_add_fd_reserve(&mut self, num_fds: usize) -> Result {
        self.get_or_init_info()
            .file_list
            .files_to_translate
            .reserve(num_fds, GFP_KERNEL)?;

        Ok(())
    }

    pub(crate) fn info_add_fd(
        &mut self,
        file: ARef<File>,
        buffer_offset: usize,
        close_on_free: bool,
    ) -> Result {
        self.get_or_init_info().file_list.files_to_translate.push(
            FileEntry {
                file,
                buffer_offset,
                close_on_free,
            },
            GFP_KERNEL,
        )?;

        Ok(())
    }

    pub(crate) fn set_info_close_on_free(&mut self, cof: FdsCloseOnFree) {
        self.get_or_init_info().file_list.close_on_free = cof.0;
    }

    pub(crate) fn translate_fds(&mut self) -> Result<TranslatedFds> {
        let file_list = match self.allocation_info.as_mut() {
            Some(info) => &mut info.file_list,
            None => return Ok(TranslatedFds::new()),
        };

        let files = core::mem::take(&mut file_list.files_to_translate);

        let num_close_on_free = files.iter().filter(|entry| entry.close_on_free).count();
        let mut close_on_free = KVec::with_capacity(num_close_on_free, GFP_KERNEL)?;

        let mut reservations = KVec::with_capacity(files.len(), GFP_KERNEL)?;
        for file_info in files {
            let res = FileDescriptorReservation::get_unused_fd_flags(bindings::O_CLOEXEC)?;
            let fd = res.reserved_fd();
            self.write::<u32>(file_info.buffer_offset, &fd)?;

            reservations.push(
                Reservation {
                    res,
                    file: file_info.file,
                },
                GFP_KERNEL,
            )?;
            if file_info.close_on_free {
                close_on_free.push(fd, GFP_KERNEL)?;
            }
        }

        Ok(TranslatedFds {
            reservations,
            close_on_free: FdsCloseOnFree(close_on_free),
        })
    }

    /// Should the looper return to userspace when freeing this allocation?
    pub(crate) fn looper_need_return_on_free(&self) -> bool {
        // Closing fds involves pushing task_work for execution when we return to userspace. Hence,
        // we should return to userspace asap if we are closing fds.
        match self.allocation_info {
            Some(ref info) => !info.file_list.close_on_free.is_empty(),
            None => false,
        }
    }
}

impl Drop for Allocation {
    fn drop(&mut self) {
        if !self.free_on_drop {
            return;
        }

        if let Some(mut info) = self.allocation_info.take() {
            if let Some(oneway_node) = info.oneway_node.as_ref() {
                oneway_node.pending_oneway_finished();
            }

            info.target_node = None;

            if let Some(offsets) = info.offsets.clone() {
                let view = AllocationView::new(self, offsets.start);
                for i in offsets.step_by(size_of::<usize>()) {
                    if view.cleanup_object(i).is_err() {
                        pr_warn!("Error cleaning up object at offset {}\n", i)
                    }
                }
            }

            for &fd in &info.file_list.close_on_free {
                let closer = match DeferredFdCloser::new(GFP_KERNEL) {
                    Ok(closer) => closer,
                    Err(kernel::alloc::AllocError) => {
                        // Ignore allocation failures.
                        break;
                    }
                };

                // Here, we ignore errors. The operation can fail if the fd is not valid, or if the
                // method is called from a kthread. However, this is always called from a syscall,
                // so the latter case cannot happen, and we don't care about the first case.
                let _ = closer.close_fd(fd);
            }

            if info.clear_on_free {
                if let Err(e) = self.fill_zero() {
                    pr_warn!("Failed to clear data on free: {:?}", e);
                }
            }
        }

        self.process.buffer_raw_free(self.ptr);
    }
}

/// A wrapper around `Allocation` that is being created.
///
/// If the allocation is destroyed while wrapped in this wrapper, then the allocation will be
/// considered to be part of a failed transaction. Successful transactions avoid that by calling
/// `success`, which skips the destructor.
#[repr(transparent)]
pub(crate) struct NewAllocation(pub(crate) Allocation);

impl NewAllocation {
    pub(crate) fn success(self) -> Allocation {
        // This skips the destructor.
        //
        // SAFETY: This type is `#[repr(transparent)]`, so the layout matches.
        unsafe { core::mem::transmute(self) }
    }
}

impl core::ops::Deref for NewAllocation {
    type Target = Allocation;
    fn deref(&self) -> &Allocation {
        &self.0
    }
}

impl core::ops::DerefMut for NewAllocation {
    fn deref_mut(&mut self) -> &mut Allocation {
        &mut self.0
    }
}

/// A view into the beginning of an allocation.
///
/// All attempts to read or write outside of the view will fail. To intentionally access outside of
/// this view, use the `alloc` field of this struct directly.
pub(crate) struct AllocationView<'a> {
    pub(crate) alloc: &'a mut Allocation,
    limit: usize,
}

impl<'a> AllocationView<'a> {
    pub(crate) fn new(alloc: &'a mut Allocation, limit: usize) -> Self {
        AllocationView { alloc, limit }
    }

    pub(crate) fn read<T: FromBytes>(&self, offset: usize) -> Result<T> {
        if offset.checked_add(size_of::<T>()).ok_or(EINVAL)? > self.limit {
            return Err(EINVAL);
        }
        self.alloc.read(offset)
    }

    pub(crate) fn write<T: AsBytes>(&self, offset: usize, obj: &T) -> Result {
        if offset.checked_add(size_of::<T>()).ok_or(EINVAL)? > self.limit {
            return Err(EINVAL);
        }
        self.alloc.write(offset, obj)
    }

    pub(crate) fn copy_into(
        &self,
        reader: &mut UserSliceReader,
        offset: usize,
        size: usize,
    ) -> Result {
        if offset.checked_add(size).ok_or(EINVAL)? > self.limit {
            return Err(EINVAL);
        }
        self.alloc.copy_into(reader, offset, size)
    }

    pub(crate) fn transfer_binder_object(
        &self,
        offset: usize,
        obj: &uapi::flat_binder_object,
        strong: bool,
        node_ref: NodeRef,
    ) -> Result {
        let mut newobj = FlatBinderObject::default();
        let node = node_ref.node.clone();
        if Arc::ptr_eq(&node_ref.node.owner, &self.alloc.process) {
            // The receiving process is the owner of the node, so send it a binder object (instead
            // of a handle).
            let (ptr, cookie) = node.get_id();
            newobj.hdr.type_ = if strong {
                BINDER_TYPE_BINDER
            } else {
                BINDER_TYPE_WEAK_BINDER
            };
            newobj.flags = obj.flags;
            newobj.__bindgen_anon_1.binder = ptr as _;
            newobj.cookie = cookie as _;
            self.write(offset, &newobj)?;
            // Increment the user ref count on the node. It will be decremented as part of the
            // destruction of the buffer, when we see a binder or weak-binder object.
            node.update_refcount(true, 1, strong);
        } else {
            // The receiving process is different from the owner, so we need to insert a handle to
            // the binder object.
            let handle = self
                .alloc
                .process
                .as_arc_borrow()
                .insert_or_update_handle(node_ref, false)?;
            newobj.hdr.type_ = if strong {
                BINDER_TYPE_HANDLE
            } else {
                BINDER_TYPE_WEAK_HANDLE
            };
            newobj.flags = obj.flags;
            newobj.__bindgen_anon_1.handle = handle;
            if self.write(offset, &newobj).is_err() {
                // Decrement ref count on the handle we just created.
                let _ = self
                    .alloc
                    .process
                    .as_arc_borrow()
                    .update_ref(handle, false, strong);
                return Err(EINVAL);
            }
        }

        Ok(())
    }

    fn cleanup_object(&self, index_offset: usize) -> Result {
        let offset = self.alloc.read(index_offset)?;
        let header = self.read::<BinderObjectHeader>(offset)?;
        match header.type_ {
            BINDER_TYPE_WEAK_BINDER | BINDER_TYPE_BINDER => {
                let obj = self.read::<FlatBinderObject>(offset)?;
                let strong = header.type_ == BINDER_TYPE_BINDER;
                // SAFETY: The type is `BINDER_TYPE_{WEAK_}BINDER`, so the `binder` field is
                // populated.
                let ptr = unsafe { obj.__bindgen_anon_1.binder };
                let cookie = obj.cookie;
                self.alloc.process.update_node(ptr, cookie, strong);
                Ok(())
            }
            BINDER_TYPE_WEAK_HANDLE | BINDER_TYPE_HANDLE => {
                let obj = self.read::<FlatBinderObject>(offset)?;
                let strong = header.type_ == BINDER_TYPE_HANDLE;
                // SAFETY: The type is `BINDER_TYPE_{WEAK_}HANDLE`, so the `handle` field is
                // populated.
                let handle = unsafe { obj.__bindgen_anon_1.handle };
                self.alloc
                    .process
                    .as_arc_borrow()
                    .update_ref(handle, false, strong)
            }
            _ => Ok(()),
        }
    }
}

/// A binder object as it is serialized.
///
/// # Invariants
///
/// All bytes must be initialized, and the value of `self.hdr.type_` must be one of the allowed
/// types.
#[repr(C)]
pub(crate) union BinderObject {
    hdr: uapi::binder_object_header,
    fbo: uapi::flat_binder_object,
    fdo: uapi::binder_fd_object,
    bbo: uapi::binder_buffer_object,
    fdao: uapi::binder_fd_array_object,
}

/// A view into a `BinderObject` that can be used in a match statement.
pub(crate) enum BinderObjectRef<'a> {
    Binder(&'a mut uapi::flat_binder_object),
    Handle(&'a mut uapi::flat_binder_object),
    Fd(&'a mut uapi::binder_fd_object),
    Ptr(&'a mut uapi::binder_buffer_object),
    Fda(&'a mut uapi::binder_fd_array_object),
}

impl BinderObject {
    pub(crate) fn read_from(reader: &mut UserSliceReader) -> Result<BinderObject> {
        let object = Self::read_from_inner(|slice| {
            let read_len = usize::min(slice.len(), reader.len());
            reader.clone_reader().read_slice(&mut slice[..read_len])?;
            Ok(())
        })?;

        // If we used a object type smaller than the largest object size, then we've read more
        // bytes than we needed to. However, we used `.clone_reader()` to avoid advancing the
        // original reader. Now, we call `skip` so that the caller's reader is advanced by the
        // right amount.
        //
        // The `skip` call fails if the reader doesn't have `size` bytes available. This could
        // happen if the type header corresponds to an object type that is larger than the rest of
        // the reader.
        //
        // Any extra bytes beyond the size of the object are inaccessible after this call, so
        // reading them again from the `reader` later does not result in TOCTOU bugs.
        reader.skip(object.size())?;

        Ok(object)
    }

    /// Use the provided reader closure to construct a `BinderObject`.
    ///
    /// The closure should write the bytes for the object into the provided slice.
    pub(crate) fn read_from_inner<R>(reader: R) -> Result<BinderObject>
    where
        R: FnOnce(&mut [u8; size_of::<BinderObject>()]) -> Result<()>,
    {
        let mut obj = MaybeUninit::<BinderObject>::zeroed();

        // SAFETY: The lengths of `BinderObject` and `[u8; size_of::<BinderObject>()]` are equal,
        // and the byte array has an alignment requirement of one, so the pointer cast is okay.
        // Additionally, `obj` was initialized to zeros, so the byte array will not be
        // uninitialized.
        (reader)(unsafe { &mut *obj.as_mut_ptr().cast() })?;

        // SAFETY: The entire object is initialized, so accessing this field is safe.
        let type_ = unsafe { obj.assume_init_ref().hdr.type_ };
        if Self::type_to_size(type_).is_none() {
            // The value of `obj.hdr_type_` was invalid.
            return Err(EINVAL);
        }

        // SAFETY: All bytes are initialized (since we zeroed them at the start) and we checked
        // that `self.hdr.type_` is one of the allowed types, so the type invariants are satisfied.
        unsafe { Ok(obj.assume_init()) }
    }

    pub(crate) fn as_ref(&mut self) -> BinderObjectRef<'_> {
        use BinderObjectRef::*;
        // SAFETY: The constructor ensures that all bytes of `self` are initialized, and all
        // variants of this union accept all initialized bit patterns.
        unsafe {
            match self.hdr.type_ {
                BINDER_TYPE_WEAK_BINDER | BINDER_TYPE_BINDER => Binder(&mut self.fbo),
                BINDER_TYPE_WEAK_HANDLE | BINDER_TYPE_HANDLE => Handle(&mut self.fbo),
                BINDER_TYPE_FD => Fd(&mut self.fdo),
                BINDER_TYPE_PTR => Ptr(&mut self.bbo),
                BINDER_TYPE_FDA => Fda(&mut self.fdao),
                // SAFETY: By the type invariant, the value of `self.hdr.type_` cannot have any
                // other value than the ones checked above.
                _ => core::hint::unreachable_unchecked(),
            }
        }
    }

    pub(crate) fn size(&self) -> usize {
        // SAFETY: The entire object is initialized, so accessing this field is safe.
        let type_ = unsafe { self.hdr.type_ };

        // SAFETY: The type invariants guarantee that the type field is correct.
        unsafe { Self::type_to_size(type_).unwrap_unchecked() }
    }

    fn type_to_size(type_: u32) -> Option<usize> {
        match type_ {
            BINDER_TYPE_WEAK_BINDER => Some(size_of::<uapi::flat_binder_object>()),
            BINDER_TYPE_BINDER => Some(size_of::<uapi::flat_binder_object>()),
            BINDER_TYPE_WEAK_HANDLE => Some(size_of::<uapi::flat_binder_object>()),
            BINDER_TYPE_HANDLE => Some(size_of::<uapi::flat_binder_object>()),
            BINDER_TYPE_FD => Some(size_of::<uapi::binder_fd_object>()),
            BINDER_TYPE_PTR => Some(size_of::<uapi::binder_buffer_object>()),
            BINDER_TYPE_FDA => Some(size_of::<uapi::binder_fd_array_object>()),
            _ => None,
        }
    }
}

#[derive(Default)]
struct FileList {
    files_to_translate: KVec<FileEntry>,
    close_on_free: KVec<u32>,
}

struct FileEntry {
    /// The file for which a descriptor will be created in the recipient process.
    file: ARef<File>,
    /// The offset in the buffer where the file descriptor is stored.
    buffer_offset: usize,
    /// Whether this fd should be closed when the allocation is freed.
    close_on_free: bool,
}

pub(crate) struct TranslatedFds {
    reservations: KVec<Reservation>,
    /// If commit is called, then these fds should be closed. (If commit is not called, then they
    /// shouldn't be closed.)
    close_on_free: FdsCloseOnFree,
}

struct Reservation {
    res: FileDescriptorReservation,
    file: ARef<File>,
}

impl TranslatedFds {
    pub(crate) fn new() -> Self {
        Self {
            reservations: KVec::new(),
            close_on_free: FdsCloseOnFree(KVec::new()),
        }
    }

    pub(crate) fn commit(self) -> FdsCloseOnFree {
        for entry in self.reservations {
            entry.res.fd_install(entry.file);
        }

        self.close_on_free
    }
}

pub(crate) struct FdsCloseOnFree(KVec<u32>);
