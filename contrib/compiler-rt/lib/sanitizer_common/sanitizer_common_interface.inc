//===-- sanitizer_common_interface.inc ------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
// Sanitizer Common interface list.
//===----------------------------------------------------------------------===//
INTERFACE_FUNCTION(__sanitizer_acquire_crash_state)
INTERFACE_FUNCTION(__sanitizer_annotate_contiguous_container)
INTERFACE_FUNCTION(__sanitizer_contiguous_container_find_bad_address)
INTERFACE_FUNCTION(__sanitizer_set_death_callback)
INTERFACE_FUNCTION(__sanitizer_set_report_path)
INTERFACE_FUNCTION(__sanitizer_set_report_fd)
INTERFACE_FUNCTION(__sanitizer_verify_contiguous_container)
INTERFACE_WEAK_FUNCTION(__sanitizer_report_error_summary)
INTERFACE_WEAK_FUNCTION(__sanitizer_sandbox_on_notify)
// Sanitizer weak hooks
INTERFACE_WEAK_FUNCTION(__sanitizer_weak_hook_memcmp)
INTERFACE_WEAK_FUNCTION(__sanitizer_weak_hook_strcmp)
INTERFACE_WEAK_FUNCTION(__sanitizer_weak_hook_strncmp)
INTERFACE_WEAK_FUNCTION(__sanitizer_weak_hook_strstr)
// Stacktrace interface.
INTERFACE_FUNCTION(__sanitizer_get_module_and_offset_for_pc)
INTERFACE_FUNCTION(__sanitizer_symbolize_global)
INTERFACE_FUNCTION(__sanitizer_symbolize_pc)
// Allocator interface.
INTERFACE_FUNCTION(__sanitizer_get_allocated_size)
INTERFACE_FUNCTION(__sanitizer_get_current_allocated_bytes)
INTERFACE_FUNCTION(__sanitizer_get_estimated_allocated_size)
INTERFACE_FUNCTION(__sanitizer_get_free_bytes)
INTERFACE_FUNCTION(__sanitizer_get_heap_size)
INTERFACE_FUNCTION(__sanitizer_get_ownership)
INTERFACE_FUNCTION(__sanitizer_get_unmapped_bytes)
INTERFACE_FUNCTION(__sanitizer_install_malloc_and_free_hooks)
INTERFACE_FUNCTION(__sanitizer_purge_allocator)
INTERFACE_FUNCTION(__sanitizer_print_memory_profile)
INTERFACE_WEAK_FUNCTION(__sanitizer_free_hook)
INTERFACE_WEAK_FUNCTION(__sanitizer_malloc_hook)
