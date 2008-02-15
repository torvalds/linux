/*
 * Simulator hook call mechanism
 */

#ifndef __hook_h__
#define __hook_h__

int hook_call(unsigned id, unsigned pcnt, ...);
int hook_call_str(unsigned id, unsigned size, const char *str);

enum hook_ids {
  hook_debug_on = 1,
  hook_debug_off,
  hook_stop_sim_ok,
  hook_stop_sim_fail,
  hook_alloc_shared,
  hook_ptr_shared,
  hook_free_shared,
  hook_file2shared,
  hook_cmp_shared,
  hook_print_params,
  hook_sim_time,
  hook_stop_sim,
  hook_kick_dog,
  hook_dog_timeout,
  hook_rand,
  hook_srand,
  hook_rand_range,
  hook_print_str,
  hook_print_hex,
  hook_cmp_offset_shared,
  hook_fill_random_shared,
  hook_alloc_random_data,
  hook_calloc_random_data,
  hook_print_int,
  hook_print_uint,
  hook_fputc,
  hook_init_fd,
  hook_sbrk,
  hook_print_context_descr,
  hook_print_data_descr,
  hook_print_group_descr,
  hook_fill_shared,
  hook_sl_srand,
  hook_sl_rand_irange,
  hook_sl_rand_urange,
  hook_sl_sh_malloc_aligned,
  hook_sl_sh_calloc_aligned,
  hook_sl_sh_alloc_random_data,
  hook_sl_sh_file2mem,
  hook_sl_vera_mbox_handle,
  hook_sl_vera_mbox_put,
  hook_sl_vera_mbox_get,
  hook_sl_system,
  hook_sl_sh_hexdump
};

#endif
