
//===----------------------------------------------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.txt for details.
//
//===----------------------------------------------------------------------===//


#include "ittnotify_config.h"

#ifndef ITT_FORMAT_DEFINED
#  ifndef ITT_FORMAT
#    define ITT_FORMAT
#  endif /* ITT_FORMAT */
#  ifndef ITT_NO_PARAMS
#    define ITT_NO_PARAMS
#  endif /* ITT_NO_PARAMS */
#endif /* ITT_FORMAT_DEFINED */

/*
 * parameters for macro expected:
 * ITT_STUB(api, type, func_name, arguments, params, func_name_in_dll, group, printf_fmt)
 */
#ifdef __ITT_INTERNAL_INIT

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_domain*, domain_createA, (const char    *name), (ITT_FORMAT name), domain_createA, __itt_group_structure, "\"%s\"")
ITT_STUB(ITTAPI, __itt_domain*, domain_createW, (const wchar_t *name), (ITT_FORMAT name), domain_createW, __itt_group_structure, "\"%S\"")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_domain*, domain_create,  (const char    *name), (ITT_FORMAT name), domain_create,  __itt_group_structure, "\"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_string_handle*, string_handle_createA, (const char    *name), (ITT_FORMAT name), string_handle_createA, __itt_group_structure, "\"%s\"")
ITT_STUB(ITTAPI, __itt_string_handle*, string_handle_createW, (const wchar_t *name), (ITT_FORMAT name), string_handle_createW, __itt_group_structure, "\"%S\"")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_string_handle*, string_handle_create,  (const char    *name), (ITT_FORMAT name), string_handle_create,  __itt_group_structure, "\"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_counter, counter_createA, (const char    *name, const char    *domain), (ITT_FORMAT name, domain), counter_createA, __itt_group_counter, "\"%s\", \"%s\"")
ITT_STUB(ITTAPI, __itt_counter, counter_createW, (const wchar_t *name, const wchar_t *domain), (ITT_FORMAT name, domain), counter_createW, __itt_group_counter, "\"%s\", \"%s\"")
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_counter, counter_create,  (const char    *name, const char    *domain), (ITT_FORMAT name, domain), counter_create,  __itt_group_counter, "\"%s\", \"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_counter, counter_create_typedA, (const char    *name, const char    *domain, __itt_metadata_type type), (ITT_FORMAT name, domain, type), counter_create_typedA, __itt_group_counter, "\"%s\", \"%s\", %d")
ITT_STUB(ITTAPI, __itt_counter, counter_create_typedW, (const wchar_t *name, const wchar_t *domain, __itt_metadata_type type), (ITT_FORMAT name, domain, type), counter_create_typedW, __itt_group_counter, "\"%s\", \"%s\", %d")
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_counter, counter_create_typed,  (const char    *name, const char    *domain, __itt_metadata_type type), (ITT_FORMAT name, domain, type), counter_create_typed,  __itt_group_counter, "\"%s\", \"%s\", %d")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */


ITT_STUBV(ITTAPI, void, pause,  (void), (ITT_NO_PARAMS), pause,  __itt_group_control | __itt_group_legacy, "no args")
ITT_STUBV(ITTAPI, void, resume, (void), (ITT_NO_PARAMS), resume, __itt_group_control | __itt_group_legacy, "no args")

#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, thread_set_nameA, (const char    *name), (ITT_FORMAT name), thread_set_nameA, __itt_group_thread, "\"%s\"")
ITT_STUBV(ITTAPI, void, thread_set_nameW, (const wchar_t *name), (ITT_FORMAT name), thread_set_nameW, __itt_group_thread, "\"%S\"")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, thread_set_name,  (const char    *name), (ITT_FORMAT name), thread_set_name,  __itt_group_thread, "\"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, thread_ignore, (void), (ITT_NO_PARAMS), thread_ignore, __itt_group_thread, "no args")

#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(LIBITTAPI, int,  thr_name_setA, (const char    *name, int namelen), (ITT_FORMAT name, namelen), thr_name_setA, __itt_group_thread | __itt_group_legacy, "\"%s\", %d")
ITT_STUB(LIBITTAPI, int,  thr_name_setW, (const wchar_t *name, int namelen), (ITT_FORMAT name, namelen), thr_name_setW, __itt_group_thread | __itt_group_legacy, "\"%S\", %d")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(LIBITTAPI, int,  thr_name_set,  (const char    *name, int namelen), (ITT_FORMAT name, namelen), thr_name_set,  __itt_group_thread | __itt_group_legacy, "\"%s\", %d")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(LIBITTAPI, void, thr_ignore,   (void),                             (ITT_NO_PARAMS),            thr_ignore,    __itt_group_thread | __itt_group_legacy, "no args")
#endif /* __ITT_INTERNAL_BODY */

ITT_STUBV(ITTAPI, void, enable_attach, (void), (ITT_NO_PARAMS), enable_attach, __itt_group_all, "no args")

#else  /* __ITT_INTERNAL_INIT */

ITT_STUBV(ITTAPI, void, detach, (void), (ITT_NO_PARAMS), detach, __itt_group_control | __itt_group_legacy, "no args")

#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, sync_createA, (void *addr, const char    *objtype, const char    *objname, int attribute), (ITT_FORMAT addr, objtype, objname, attribute), sync_createA, __itt_group_sync | __itt_group_fsync, "%p, \"%s\", \"%s\", %x")
ITT_STUBV(ITTAPI, void, sync_createW, (void *addr, const wchar_t *objtype, const wchar_t *objname, int attribute), (ITT_FORMAT addr, objtype, objname, attribute), sync_createW, __itt_group_sync | __itt_group_fsync, "%p, \"%S\", \"%S\", %x")
ITT_STUBV(ITTAPI, void, sync_renameA, (void *addr, const char    *name), (ITT_FORMAT addr, name), sync_renameA, __itt_group_sync | __itt_group_fsync, "%p, \"%s\"")
ITT_STUBV(ITTAPI, void, sync_renameW, (void *addr, const wchar_t *name), (ITT_FORMAT addr, name), sync_renameW, __itt_group_sync | __itt_group_fsync, "%p, \"%S\"")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, sync_create,  (void *addr, const char    *objtype, const char    *objname, int attribute), (ITT_FORMAT addr, objtype, objname, attribute), sync_create,  __itt_group_sync | __itt_group_fsync, "%p, \"%s\", \"%s\", %x")
ITT_STUBV(ITTAPI, void, sync_rename,  (void *addr, const char    *name), (ITT_FORMAT addr, name), sync_rename,  __itt_group_sync | __itt_group_fsync, "%p, \"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, sync_destroy,    (void *addr), (ITT_FORMAT addr), sync_destroy,   __itt_group_sync | __itt_group_fsync, "%p")

ITT_STUBV(ITTAPI, void, sync_prepare,    (void* addr), (ITT_FORMAT addr), sync_prepare,   __itt_group_sync,  "%p")
ITT_STUBV(ITTAPI, void, sync_cancel,     (void *addr), (ITT_FORMAT addr), sync_cancel,    __itt_group_sync,  "%p")
ITT_STUBV(ITTAPI, void, sync_acquired,   (void *addr), (ITT_FORMAT addr), sync_acquired,  __itt_group_sync,  "%p")
ITT_STUBV(ITTAPI, void, sync_releasing,  (void* addr), (ITT_FORMAT addr), sync_releasing, __itt_group_sync,  "%p")

ITT_STUBV(ITTAPI, void, suppress_push,       (unsigned int mask),                             (ITT_FORMAT mask), suppress_push,  __itt_group_suppress,  "%p")
ITT_STUBV(ITTAPI, void, suppress_pop,        (void),                                          (ITT_NO_PARAMS),   suppress_pop,   __itt_group_suppress,  "no args")
ITT_STUBV(ITTAPI, void, suppress_mark_range, (__itt_suppress_mode_t mode, unsigned int mask, void * address, size_t size),(ITT_FORMAT mode, mask, address, size), suppress_mark_range, __itt_group_suppress, "%d, %p, %p, %d")
ITT_STUBV(ITTAPI, void, suppress_clear_range,(__itt_suppress_mode_t mode, unsigned int mask, void * address, size_t size),(ITT_FORMAT mode, mask, address, size), suppress_clear_range,__itt_group_suppress, "%d, %p, %p, %d")

ITT_STUBV(ITTAPI, void, fsync_prepare,   (void* addr), (ITT_FORMAT addr), sync_prepare,   __itt_group_fsync, "%p")
ITT_STUBV(ITTAPI, void, fsync_cancel,    (void *addr), (ITT_FORMAT addr), sync_cancel,    __itt_group_fsync, "%p")
ITT_STUBV(ITTAPI, void, fsync_acquired,  (void *addr), (ITT_FORMAT addr), sync_acquired,  __itt_group_fsync, "%p")
ITT_STUBV(ITTAPI, void, fsync_releasing, (void* addr), (ITT_FORMAT addr), sync_releasing, __itt_group_fsync, "%p")

ITT_STUBV(ITTAPI, void, model_site_begin,          (__itt_model_site *site, __itt_model_site_instance *instance, const char *name), (ITT_FORMAT site, instance, name), model_site_begin, __itt_group_model, "%p, %p, \"%s\"")
ITT_STUBV(ITTAPI, void, model_site_end,            (__itt_model_site *site, __itt_model_site_instance *instance),                   (ITT_FORMAT site, instance),       model_site_end,   __itt_group_model, "%p, %p")
ITT_STUBV(ITTAPI, void, model_task_begin,          (__itt_model_task *task, __itt_model_task_instance *instance, const char *name), (ITT_FORMAT task, instance, name), model_task_begin, __itt_group_model, "%p, %p, \"%s\"")
ITT_STUBV(ITTAPI, void, model_task_end,            (__itt_model_task *task, __itt_model_task_instance *instance),                   (ITT_FORMAT task, instance),       model_task_end,   __itt_group_model, "%p, %p")
ITT_STUBV(ITTAPI, void, model_lock_acquire,        (void *lock), (ITT_FORMAT lock), model_lock_acquire, __itt_group_model, "%p")
ITT_STUBV(ITTAPI, void, model_lock_release,        (void *lock), (ITT_FORMAT lock), model_lock_release, __itt_group_model, "%p")
ITT_STUBV(ITTAPI, void, model_record_allocation,   (void *addr, size_t size), (ITT_FORMAT addr, size), model_record_allocation,   __itt_group_model, "%p, %d")
ITT_STUBV(ITTAPI, void, model_record_deallocation, (void *addr),              (ITT_FORMAT addr),       model_record_deallocation, __itt_group_model, "%p")
ITT_STUBV(ITTAPI, void, model_induction_uses,      (void* addr, size_t size), (ITT_FORMAT addr, size), model_induction_uses,      __itt_group_model, "%p, %d")
ITT_STUBV(ITTAPI, void, model_reduction_uses,      (void* addr, size_t size), (ITT_FORMAT addr, size), model_reduction_uses,      __itt_group_model, "%p, %d")
ITT_STUBV(ITTAPI, void, model_observe_uses,        (void* addr, size_t size), (ITT_FORMAT addr, size), model_observe_uses,        __itt_group_model, "%p, %d")
ITT_STUBV(ITTAPI, void, model_clear_uses,          (void* addr),              (ITT_FORMAT addr),       model_clear_uses,          __itt_group_model, "%p")

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, model_site_beginW,         (const wchar_t *name),     (ITT_FORMAT name),       model_site_beginW,         __itt_group_model, "\"%s\"")
ITT_STUBV(ITTAPI, void, model_task_beginW,         (const wchar_t *name),     (ITT_FORMAT name),       model_task_beginW,         __itt_group_model, "\"%s\"")
ITT_STUBV(ITTAPI, void, model_iteration_taskW,     (const wchar_t *name),     (ITT_FORMAT name),       model_iteration_taskW,     __itt_group_model, "\"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, model_site_beginA,         (const char *name),        (ITT_FORMAT name),       model_site_beginA,         __itt_group_model, "\"%s\"")
ITT_STUBV(ITTAPI, void, model_site_beginAL,        (const char *name, size_t len), (ITT_FORMAT name, len), model_site_beginAL,    __itt_group_model, "\"%s\", %d")
ITT_STUBV(ITTAPI, void, model_task_beginA,         (const char *name),        (ITT_FORMAT name),       model_task_beginA,         __itt_group_model, "\"%s\"")
ITT_STUBV(ITTAPI, void, model_task_beginAL,        (const char *name, size_t len), (ITT_FORMAT name, len), model_task_beginAL,    __itt_group_model, "\"%s\", %d")
ITT_STUBV(ITTAPI, void, model_iteration_taskA,     (const char *name),        (ITT_FORMAT name),       model_iteration_taskA,     __itt_group_model, "\"%s\"")
ITT_STUBV(ITTAPI, void, model_iteration_taskAL,    (const char *name, size_t len), (ITT_FORMAT name, len), model_iteration_taskAL, __itt_group_model, "\"%s\", %d")
ITT_STUBV(ITTAPI, void, model_site_end_2,          (void),                    (ITT_NO_PARAMS),         model_site_end_2,          __itt_group_model, "no args")
ITT_STUBV(ITTAPI, void, model_task_end_2,          (void),                    (ITT_NO_PARAMS),         model_task_end_2,          __itt_group_model, "no args")
ITT_STUBV(ITTAPI, void, model_lock_acquire_2,      (void *lock),              (ITT_FORMAT lock),       model_lock_acquire_2,      __itt_group_model, "%p")
ITT_STUBV(ITTAPI, void, model_lock_release_2,      (void *lock),              (ITT_FORMAT lock),       model_lock_release_2,      __itt_group_model, "%p")
ITT_STUBV(ITTAPI, void, model_aggregate_task,      (size_t count),            (ITT_FORMAT count),      model_aggregate_task,      __itt_group_model, "%d")
ITT_STUBV(ITTAPI, void, model_disable_push,        (__itt_model_disable x),   (ITT_FORMAT x),          model_disable_push,        __itt_group_model, "%p")
ITT_STUBV(ITTAPI, void, model_disable_pop,         (void),                    (ITT_NO_PARAMS),         model_disable_pop,         __itt_group_model, "no args")
#endif /* __ITT_INTERNAL_BODY */

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_heap_function, heap_function_createA, (const char    *name, const char    *domain), (ITT_FORMAT name, domain), heap_function_createA, __itt_group_heap, "\"%s\", \"%s\"")
ITT_STUB(ITTAPI, __itt_heap_function, heap_function_createW, (const wchar_t *name, const wchar_t *domain), (ITT_FORMAT name, domain), heap_function_createW, __itt_group_heap, "\"%s\", \"%s\"")
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_heap_function, heap_function_create,  (const char    *name, const char    *domain), (ITT_FORMAT name, domain), heap_function_create,  __itt_group_heap, "\"%s\", \"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* __ITT_INTERNAL_BODY */
ITT_STUBV(ITTAPI, void, heap_allocate_begin,   (__itt_heap_function h, size_t size, int initialized),             (ITT_FORMAT h, size, initialized),       heap_allocate_begin, __itt_group_heap, "%p, %lu, %d")
ITT_STUBV(ITTAPI, void, heap_allocate_end,     (__itt_heap_function h, void** addr, size_t size, int initialized), (ITT_FORMAT h, addr, size, initialized), heap_allocate_end,   __itt_group_heap, "%p, %p, %lu, %d")
ITT_STUBV(ITTAPI, void, heap_free_begin,       (__itt_heap_function h, void*  addr), (ITT_FORMAT h, addr), heap_free_begin, __itt_group_heap, "%p, %p")
ITT_STUBV(ITTAPI, void, heap_free_end,         (__itt_heap_function h, void*  addr), (ITT_FORMAT h, addr), heap_free_end,   __itt_group_heap, "%p, %p")
ITT_STUBV(ITTAPI, void, heap_reallocate_begin, (__itt_heap_function h, void*  addr, size_t new_size, int initialized),                  (ITT_FORMAT h, addr, new_size, initialized),           heap_reallocate_begin, __itt_group_heap, "%p, %p, %lu, %d")
ITT_STUBV(ITTAPI, void, heap_reallocate_end,   (__itt_heap_function h, void*  addr, void** new_addr, size_t new_size, int initialized), (ITT_FORMAT h, addr, new_addr, new_size, initialized), heap_reallocate_end,   __itt_group_heap, "%p, %p, %p, %lu, %d")
ITT_STUBV(ITTAPI, void, heap_internal_access_begin, (void), (ITT_NO_PARAMS), heap_internal_access_begin, __itt_group_heap, "no args")
ITT_STUBV(ITTAPI, void, heap_internal_access_end,   (void), (ITT_NO_PARAMS), heap_internal_access_end,   __itt_group_heap, "no args")
ITT_STUBV(ITTAPI, void, heap_record_memory_growth_begin, (void), (ITT_NO_PARAMS), heap_record_memory_growth_begin, __itt_group_heap, "no args")
ITT_STUBV(ITTAPI, void, heap_record_memory_growth_end,   (void), (ITT_NO_PARAMS), heap_record_memory_growth_end,   __itt_group_heap, "no args")
ITT_STUBV(ITTAPI, void, heap_reset_detection, (unsigned int reset_mask),  (ITT_FORMAT reset_mask), heap_reset_detection, __itt_group_heap, "%u")
ITT_STUBV(ITTAPI, void, heap_record,          (unsigned int record_mask), (ITT_FORMAT record_mask),  heap_record,        __itt_group_heap, "%u")

ITT_STUBV(ITTAPI, void, id_create,  (const __itt_domain *domain, __itt_id id), (ITT_FORMAT domain, id), id_create,  __itt_group_structure, "%p, %lu")
ITT_STUBV(ITTAPI, void, id_destroy, (const __itt_domain *domain, __itt_id id), (ITT_FORMAT domain, id), id_destroy, __itt_group_structure, "%p, %lu")

ITT_STUB(ITTAPI, __itt_timestamp, get_timestamp, (void), (ITT_NO_PARAMS), get_timestamp,  __itt_group_structure, "no args")

ITT_STUBV(ITTAPI, void, region_begin, (const __itt_domain *domain, __itt_id id, __itt_id parent, __itt_string_handle *name), (ITT_FORMAT domain, id, parent, name), region_begin, __itt_group_structure, "%p, %lu, %lu, %p")
ITT_STUBV(ITTAPI, void, region_end,   (const __itt_domain *domain, __itt_id id),                                             (ITT_FORMAT domain, id),               region_end,   __itt_group_structure, "%p, %lu")

#ifndef __ITT_INTERNAL_BODY
ITT_STUBV(ITTAPI, void, frame_begin_v3,  (const __itt_domain *domain, __itt_id *id),                                             (ITT_FORMAT domain, id),             frame_begin_v3,  __itt_group_structure, "%p, %p")
ITT_STUBV(ITTAPI, void, frame_end_v3,    (const __itt_domain *domain, __itt_id *id),                                             (ITT_FORMAT domain, id),             frame_end_v3,    __itt_group_structure, "%p, %p")
ITT_STUBV(ITTAPI, void, frame_submit_v3, (const __itt_domain *domain, __itt_id *id, __itt_timestamp begin, __itt_timestamp end), (ITT_FORMAT domain, id, begin, end), frame_submit_v3, __itt_group_structure, "%p, %p, %lu, %lu")
#endif /* __ITT_INTERNAL_BODY */

ITT_STUBV(ITTAPI, void, task_group,   (const __itt_domain *domain, __itt_id id, __itt_id parent, __itt_string_handle *name), (ITT_FORMAT domain, id, parent, name), task_group,  __itt_group_structure, "%p, %lu, %lu, %p")

ITT_STUBV(ITTAPI, void, task_begin,    (const __itt_domain *domain, __itt_id id, __itt_id parent, __itt_string_handle *name), (ITT_FORMAT domain, id, parent, name), task_begin,    __itt_group_structure, "%p, %lu, %lu, %p")
ITT_STUBV(ITTAPI, void, task_begin_fn, (const __itt_domain *domain, __itt_id id, __itt_id parent, void* fn),                  (ITT_FORMAT domain, id, parent, fn),   task_begin_fn, __itt_group_structure, "%p, %lu, %lu, %p")
ITT_STUBV(ITTAPI, void, task_end,      (const __itt_domain *domain),                                                          (ITT_FORMAT domain),                   task_end,      __itt_group_structure, "%p")

ITT_STUBV(ITTAPI, void, counter_inc_v3,       (const __itt_domain *domain, __itt_string_handle *name),                           (ITT_FORMAT domain, name),        counter_inc_v3,       __itt_group_structure, "%p, %p")
ITT_STUBV(ITTAPI, void, counter_inc_delta_v3, (const __itt_domain *domain, __itt_string_handle *name, unsigned long long value), (ITT_FORMAT domain, name, value), counter_inc_delta_v3, __itt_group_structure, "%p, %p, %lu")
ITT_STUBV(ITTAPI, void, counter_dec_v3,       (const __itt_domain *domain, __itt_string_handle *name),                           (ITT_FORMAT domain, name),        counter_dec_v3,       __itt_group_structure, "%p, %p")
ITT_STUBV(ITTAPI, void, counter_dec_delta_v3, (const __itt_domain *domain, __itt_string_handle *name, unsigned long long value), (ITT_FORMAT domain, name, value), counter_dec_delta_v3, __itt_group_structure, "%p, %p, %lu")

ITT_STUBV(ITTAPI, void, marker, (const __itt_domain *domain, __itt_id id, __itt_string_handle *name, __itt_scope scope), (ITT_FORMAT domain, id, name, scope), marker, __itt_group_structure, "%p, %lu, %p, %d")

ITT_STUBV(ITTAPI, void, metadata_add,      (const __itt_domain *domain, __itt_id id, __itt_string_handle *key, __itt_metadata_type type, size_t count, void *data), (ITT_FORMAT domain, id, key, type, count, data), metadata_add, __itt_group_structure, "%p, %lu, %p, %d, %lu, %p")
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, metadata_str_addA, (const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const char* data, size_t length),    (ITT_FORMAT domain, id, key, data, length), metadata_str_addA, __itt_group_structure, "%p, %lu, %p, %p, %lu")
ITT_STUBV(ITTAPI, void, metadata_str_addW, (const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const wchar_t* data, size_t length), (ITT_FORMAT domain, id, key, data, length), metadata_str_addW, __itt_group_structure, "%p, %lu, %p, %p, %lu")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, metadata_str_add,  (const __itt_domain *domain, __itt_id id, __itt_string_handle *key, const char* data, size_t length),    (ITT_FORMAT domain, id, key, data, length), metadata_str_add,  __itt_group_structure, "%p, %lu, %p, %p, %lu")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

ITT_STUBV(ITTAPI, void, relation_add_to_current, (const __itt_domain *domain, __itt_relation relation, __itt_id tail),                (ITT_FORMAT domain, relation, tail),       relation_add_to_current, __itt_group_structure, "%p, %lu, %p")
ITT_STUBV(ITTAPI, void, relation_add,            (const __itt_domain *domain, __itt_id head, __itt_relation relation, __itt_id tail), (ITT_FORMAT domain, head, relation, tail), relation_add,            __itt_group_structure, "%p, %p, %lu, %p")

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(LIBITTAPI, __itt_event, event_createA, (const char    *name, int namelen), (ITT_FORMAT name, namelen), event_createA, __itt_group_mark | __itt_group_legacy, "\"%s\", %d")
ITT_STUB(LIBITTAPI, __itt_event, event_createW, (const wchar_t *name, int namelen), (ITT_FORMAT name, namelen), event_createW, __itt_group_mark | __itt_group_legacy, "\"%S\", %d")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(LIBITTAPI, __itt_event, event_create,  (const char    *name, int namelen), (ITT_FORMAT name, namelen), event_create,  __itt_group_mark | __itt_group_legacy, "\"%s\", %d")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(LIBITTAPI, int,  event_start,          (__itt_event event),                (ITT_FORMAT event),         event_start,   __itt_group_mark | __itt_group_legacy, "%d")
ITT_STUB(LIBITTAPI, int,  event_end,            (__itt_event event),                (ITT_FORMAT event),         event_end,     __itt_group_mark | __itt_group_legacy, "%d")
#endif /* __ITT_INTERNAL_BODY */

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, sync_set_nameA, (void *addr, const char    *objtype, const char    *objname, int attribute), (ITT_FORMAT addr, objtype, objname, attribute), sync_set_nameA, __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p, \"%s\", \"%s\", %x")
ITT_STUBV(ITTAPI, void, sync_set_nameW, (void *addr, const wchar_t *objtype, const wchar_t *objname, int attribute), (ITT_FORMAT addr, objtype, objname, attribute), sync_set_nameW, __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p, \"%S\", \"%S\", %x")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, sync_set_name,  (void *addr, const char    *objtype, const char    *objname, int attribute), (ITT_FORMAT addr, objtype, objname, attribute), sync_set_name,  __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "p, \"%s\", \"%s\", %x")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(LIBITTAPI, int, notify_sync_nameA, (void *p, const char    *objtype, int typelen, const char    *objname, int namelen, int attribute), (ITT_FORMAT p, objtype, typelen, objname, namelen, attribute), notify_sync_nameA, __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p, \"%s\", %d, \"%s\", %d, %x")
ITT_STUB(LIBITTAPI, int, notify_sync_nameW, (void *p, const wchar_t *objtype, int typelen, const wchar_t *objname, int namelen, int attribute), (ITT_FORMAT p, objtype, typelen, objname, namelen, attribute), notify_sync_nameW, __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p, \"%S\", %d, \"%S\", %d, %x")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(LIBITTAPI, int, notify_sync_name,  (void *p, const char    *objtype, int typelen, const char    *objname, int namelen, int attribute), (ITT_FORMAT p, objtype, typelen, objname, namelen, attribute), notify_sync_name,  __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p, \"%s\", %d, \"%s\", %d, %x")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */

ITT_STUBV(LIBITTAPI, void, notify_sync_prepare,   (void *p), (ITT_FORMAT p), notify_sync_prepare,   __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p")
ITT_STUBV(LIBITTAPI, void, notify_sync_cancel,    (void *p), (ITT_FORMAT p), notify_sync_cancel,    __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p")
ITT_STUBV(LIBITTAPI, void, notify_sync_acquired,  (void *p), (ITT_FORMAT p), notify_sync_acquired,  __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p")
ITT_STUBV(LIBITTAPI, void, notify_sync_releasing, (void *p), (ITT_FORMAT p), notify_sync_releasing, __itt_group_sync | __itt_group_fsync | __itt_group_legacy, "%p")
#endif /* __ITT_INTERNAL_BODY */

ITT_STUBV(LIBITTAPI, void, memory_read,   (void *addr, size_t size), (ITT_FORMAT addr, size), memory_read,   __itt_group_legacy, "%p, %lu")
ITT_STUBV(LIBITTAPI, void, memory_write,  (void *addr, size_t size), (ITT_FORMAT addr, size), memory_write,  __itt_group_legacy, "%p, %lu")
ITT_STUBV(LIBITTAPI, void, memory_update, (void *addr, size_t size), (ITT_FORMAT addr, size), memory_update, __itt_group_legacy, "%p, %lu")

ITT_STUB(LIBITTAPI, __itt_state_t,     state_get,    (void),                                    (ITT_NO_PARAMS),   state_get,    __itt_group_legacy, "no args")
ITT_STUB(LIBITTAPI, __itt_state_t,     state_set,    (__itt_state_t s),                         (ITT_FORMAT s),    state_set,    __itt_group_legacy, "%d")
ITT_STUB(LIBITTAPI, __itt_obj_state_t, obj_mode_set, (__itt_obj_prop_t p, __itt_obj_state_t s), (ITT_FORMAT p, s), obj_mode_set, __itt_group_legacy, "%d, %d")
ITT_STUB(LIBITTAPI, __itt_thr_state_t, thr_mode_set, (__itt_thr_prop_t p, __itt_thr_state_t s), (ITT_FORMAT p, s), thr_mode_set, __itt_group_legacy, "%d, %d")

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_frame, frame_createA, (const char    *domain), (ITT_FORMAT domain), frame_createA, __itt_group_frame, "\"%s\"")
ITT_STUB(ITTAPI, __itt_frame, frame_createW, (const wchar_t *domain), (ITT_FORMAT domain), frame_createW, __itt_group_frame, "\"%s\"")
#else  /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_frame, frame_create,  (const char    *domain), (ITT_FORMAT domain), frame_create,  __itt_group_frame, "\"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* __ITT_INTERNAL_BODY */
ITT_STUBV(ITTAPI, void, frame_begin,         (__itt_frame frame),     (ITT_FORMAT frame),  frame_begin,   __itt_group_frame, "%p")
ITT_STUBV(ITTAPI, void, frame_end,           (__itt_frame frame),     (ITT_FORMAT frame),  frame_end,     __itt_group_frame, "%p")

ITT_STUBV(ITTAPI, void, counter_destroy,      (__itt_counter id),                                                                                  (ITT_FORMAT id),        counter_destroy,   __itt_group_counter, "%p")
ITT_STUBV(ITTAPI, void, counter_inc,          (__itt_counter id),                                                                                  (ITT_FORMAT id),        counter_inc,       __itt_group_counter, "%p")
ITT_STUBV(ITTAPI, void, counter_inc_delta,    (__itt_counter id, unsigned long long value),                                                        (ITT_FORMAT id, value), counter_inc_delta, __itt_group_counter, "%p, %lu")
ITT_STUBV(ITTAPI, void, counter_dec,          (__itt_counter id),                                                                                  (ITT_FORMAT id),        counter_dec,       __itt_group_counter, "%p")
ITT_STUBV(ITTAPI, void, counter_dec_delta,    (__itt_counter id, unsigned long long value),                                                        (ITT_FORMAT id, value), counter_dec_delta, __itt_group_counter, "%p, %lu")
ITT_STUBV(ITTAPI, void, counter_set_value,    (__itt_counter id, void *value_ptr),                                                                 (ITT_FORMAT id, value_ptr),                          counter_set_value,    __itt_group_counter, "%p, %p")
ITT_STUBV(ITTAPI, void, counter_set_value_ex, (__itt_counter id, __itt_clock_domain *clock_domain, unsigned long long timestamp, void *value_ptr), (ITT_FORMAT id, clock_domain, timestamp, value_ptr), counter_set_value_ex, __itt_group_counter, "%p, %p, %llu, %p")

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, __itt_mark_type, mark_createA, (const char    *name), (ITT_FORMAT name), mark_createA, __itt_group_mark, "\"%s\"")
ITT_STUB(ITTAPI, __itt_mark_type, mark_createW, (const wchar_t *name), (ITT_FORMAT name), mark_createW, __itt_group_mark, "\"%S\"")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, __itt_mark_type, mark_create,  (const char    *name), (ITT_FORMAT name), mark_create,  __itt_group_mark, "\"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* __ITT_INTERNAL_BODY */
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, int,  markA,        (__itt_mark_type mt, const char    *parameter), (ITT_FORMAT mt, parameter), markA, __itt_group_mark, "%d, \"%s\"")
ITT_STUB(ITTAPI, int,  markW,        (__itt_mark_type mt, const wchar_t *parameter), (ITT_FORMAT mt, parameter), markW, __itt_group_mark, "%d, \"%S\"")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, int,  mark,         (__itt_mark_type mt, const char    *parameter), (ITT_FORMAT mt, parameter), mark,  __itt_group_mark, "%d, \"%s\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, int,  mark_off, (__itt_mark_type mt), (ITT_FORMAT mt), mark_off, __itt_group_mark, "%d")
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, int,  mark_globalA, (__itt_mark_type mt, const char    *parameter), (ITT_FORMAT mt, parameter), mark_globalA, __itt_group_mark, "%d, \"%s\"")
ITT_STUB(ITTAPI, int,  mark_globalW, (__itt_mark_type mt, const wchar_t *parameter), (ITT_FORMAT mt, parameter), mark_globalW, __itt_group_mark, "%d, \"%S\"")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, int,  mark_global,  (__itt_mark_type mt, const char    *parameter), (ITT_FORMAT mt, parameter), mark_global,  __itt_group_mark, "%d, \"%S\"")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, int,  mark_global_off, (__itt_mark_type mt),                        (ITT_FORMAT mt),            mark_global_off, __itt_group_mark, "%d")

#ifndef __ITT_INTERNAL_BODY
ITT_STUB(ITTAPI, __itt_caller, stack_caller_create, (void), (ITT_NO_PARAMS), stack_caller_create,  __itt_group_stitch, "no args")
#endif /* __ITT_INTERNAL_BODY */
ITT_STUBV(ITTAPI, void, stack_caller_destroy, (__itt_caller id), (ITT_FORMAT id), stack_caller_destroy, __itt_group_stitch, "%p")
ITT_STUBV(ITTAPI, void, stack_callee_enter,   (__itt_caller id), (ITT_FORMAT id), stack_callee_enter,   __itt_group_stitch, "%p")
ITT_STUBV(ITTAPI, void, stack_callee_leave,   (__itt_caller id), (ITT_FORMAT id), stack_callee_leave,   __itt_group_stitch, "%p")

ITT_STUB(ITTAPI,  __itt_clock_domain*, clock_domain_create, (__itt_get_clock_info_fn fn, void* fn_data), (ITT_FORMAT fn, fn_data), clock_domain_create, __itt_group_structure, "%p, %p")
ITT_STUBV(ITTAPI, void,                clock_domain_reset,  (void),                                      (ITT_NO_PARAMS),          clock_domain_reset,  __itt_group_structure, "no args")
ITT_STUBV(ITTAPI, void, id_create_ex,  (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id), (ITT_FORMAT domain, clock_domain, timestamp, id), id_create_ex,  __itt_group_structure, "%p, %p, %lu, %lu")
ITT_STUBV(ITTAPI, void, id_destroy_ex, (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id), (ITT_FORMAT domain, clock_domain, timestamp, id), id_destroy_ex, __itt_group_structure, "%p, %p, %lu, %lu")
ITT_STUBV(ITTAPI, void, task_begin_ex,    (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id, __itt_id parentid, __itt_string_handle *name), (ITT_FORMAT domain, clock_domain, timestamp, id, parentid, name), task_begin_ex, __itt_group_structure, "%p, %p, %lu, %lu, %lu, %p")
ITT_STUBV(ITTAPI, void, task_begin_fn_ex, (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id, __itt_id parentid, void* fn),                  (ITT_FORMAT domain, clock_domain, timestamp, id, parentid, fn), task_begin_fn_ex, __itt_group_structure, "%p, %p, %lu, %lu, %lu, %p")
ITT_STUBV(ITTAPI, void, task_end_ex,      (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp),                                                            (ITT_FORMAT domain, clock_domain, timestamp), task_end_ex, __itt_group_structure, "%p, %p, %lu")
ITT_STUBV(ITTAPI, void, task_begin_overlapped,       (const __itt_domain *domain, __itt_id id, __itt_id parent, __itt_string_handle *name),                                                                   (ITT_FORMAT domain, id, parent, name), task_begin_overlapped, __itt_group_structure, "%p, %lu, %lu, %p")
ITT_STUBV(ITTAPI, void, task_begin_overlapped_ex,    (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id, __itt_id parentid, __itt_string_handle *name), (ITT_FORMAT domain, clock_domain, timestamp, id, parentid, name), task_begin_overlapped_ex, __itt_group_structure, "%p, %p, %lu, %lu, %lu, %p")
ITT_STUBV(ITTAPI, void, task_end_overlapped, (const __itt_domain *domain, __itt_id id),                                                                                                                       (ITT_FORMAT domain, id), task_end_overlapped, __itt_group_structure, "%p, %lu")
ITT_STUBV(ITTAPI, void, task_end_overlapped_ex, (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id),                                                    (ITT_FORMAT domain, clock_domain, timestamp, id), task_end_overlapped_ex, __itt_group_structure, "%p, %p, %lu, %lu")
ITT_STUBV(ITTAPI, void, marker_ex, (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id id, __itt_string_handle *name, __itt_scope scope), (ITT_FORMAT domain, clock_domain, timestamp, id, name, scope), marker_ex, __itt_group_structure, "%p, %p, %lu, %lu, %p, %d")
ITT_STUBV(ITTAPI, void, metadata_add_with_scope, (const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, __itt_metadata_type type, size_t count, void *data), (ITT_FORMAT domain, scope, key, type, count, data), metadata_add_with_scope, __itt_group_structure, "%p, %d, %p, %d, %lu, %p")
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, metadata_str_add_with_scopeA, (const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const char *data, size_t length),    (ITT_FORMAT domain, scope, key, data, length), metadata_str_add_with_scopeA, __itt_group_structure, "%p, %d, %p, %p, %lu")
ITT_STUBV(ITTAPI, void, metadata_str_add_with_scopeW, (const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const wchar_t *data, size_t length), (ITT_FORMAT domain, scope, key, data, length), metadata_str_add_with_scopeW, __itt_group_structure, "%p, %d, %p, %p, %lu")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, metadata_str_add_with_scope,  (const __itt_domain *domain, __itt_scope scope, __itt_string_handle *key, const char *data, size_t length),    (ITT_FORMAT domain, scope, key, data, length), metadata_str_add_with_scope,  __itt_group_structure, "%p, %d, %p, %p, %lu")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, relation_add_to_current_ex, (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_relation relation, __itt_id tail),                (ITT_FORMAT domain, clock_domain, timestamp, relation, tail),       relation_add_to_current_ex, __itt_group_structure, "%p, %p, %lu, %d, %lu")
ITT_STUBV(ITTAPI, void, relation_add_ex,            (const __itt_domain *domain, __itt_clock_domain* clock_domain, unsigned long long timestamp, __itt_id head, __itt_relation relation, __itt_id tail), (ITT_FORMAT domain, clock_domain, timestamp, head, relation, tail), relation_add_ex,            __itt_group_structure, "%p, %p, %lu, %lu, %d, %lu")
ITT_STUB(ITTAPI,  __itt_track_group*, track_group_create, (__itt_string_handle* name, __itt_track_group_type track_group_type),                    (ITT_FORMAT name, track_group_type),        track_group_create, __itt_group_structure, "%p, %d")
ITT_STUB(ITTAPI,  __itt_track*,       track_create,       (__itt_track_group* track_group,__itt_string_handle* name, __itt_track_type track_type), (ITT_FORMAT track_group, name, track_type), track_create,       __itt_group_structure, "%p, %p, %d")
ITT_STUBV(ITTAPI, void,               set_track,          (__itt_track *track),                                                                    (ITT_FORMAT track),                         set_track,          __itt_group_structure, "%p")

#ifndef __ITT_INTERNAL_BODY
ITT_STUB(ITTAPI, const char*, api_version, (void), (ITT_NO_PARAMS), api_version, __itt_group_all & ~__itt_group_legacy, "no args")
#endif /* __ITT_INTERNAL_BODY */

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUB(ITTAPI, int, av_saveA, (void *data, int rank, const int *dimensions, int type, const char *filePath, int columnOrder), (ITT_FORMAT data, rank, dimensions, type, filePath, columnOrder), av_saveA, __itt_group_arrays, "%p, %d, %p, %d, \"%s\", %d")
ITT_STUB(ITTAPI, int, av_saveW, (void *data, int rank, const int *dimensions, int type, const wchar_t *filePath, int columnOrder), (ITT_FORMAT data, rank, dimensions, type, filePath, columnOrder), av_saveW, __itt_group_arrays, "%p, %d, %p, %d, \"%S\", %d")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUB(ITTAPI, int, av_save,  (void *data, int rank, const int *dimensions, int type, const char *filePath, int columnOrder), (ITT_FORMAT data, rank, dimensions, type, filePath, columnOrder), av_save,  __itt_group_arrays, "%p, %d, %p, %d, \"%s\", %d")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* __ITT_INTERNAL_BODY */

#ifndef __ITT_INTERNAL_BODY
#if ITT_PLATFORM==ITT_PLATFORM_WIN
ITT_STUBV(ITTAPI, void, module_loadA, (void *start_addr, void* end_addr, const char *path), (ITT_FORMAT start_addr, end_addr, path), module_loadA, __itt_group_none, "%p, %p, %p")
ITT_STUBV(ITTAPI, void, module_loadW, (void *start_addr, void* end_addr, const wchar_t *path), (ITT_FORMAT start_addr, end_addr, path), module_loadW, __itt_group_none, "%p, %p, %p")
#else  /* ITT_PLATFORM!=ITT_PLATFORM_WIN */
ITT_STUBV(ITTAPI, void, module_load, (void *start_addr, void *end_addr, const char *path), (ITT_FORMAT start_addr, end_addr, path), module_load, __itt_group_none, "%p, %p, %p")
#endif /* ITT_PLATFORM==ITT_PLATFORM_WIN */
#endif /* __ITT_INTERNAL_BODY */


#endif /* __ITT_INTERNAL_INIT */
