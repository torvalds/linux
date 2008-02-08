/*
 * Call simulator hook functions
 */

#ifndef HOOK_H
#define HOOK_H

int hook_call(unsigned id, unsigned pcnt, ...);

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
  hook_sbrk

};

#endif
