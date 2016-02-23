/*
 * intel_mid_weak_decls.h: Weak declarations of intel-mid.c
 *
 * (C) Copyright 2013 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License.
 */


/* For every CPU addition a new get_<cpuname>_ops interface needs
 * to be added.
 */
extern void *get_penwell_ops(void);
extern void *get_cloverview_ops(void);
extern void *get_tangier_ops(void);
