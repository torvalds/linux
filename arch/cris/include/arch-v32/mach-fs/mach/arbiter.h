/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_CRIS_ARCH_ARBITER_H
#define _ASM_CRIS_ARCH_ARBITER_H

#define EXT_REGION 0
#define INT_REGION 1

typedef void (watch_callback)(void);

enum {
	arbiter_all_dmas = 0x3ff,
	arbiter_cpu = 0xc00,
	arbiter_all_clients = 0x3fff
};

enum {
	arbiter_all_read = 0x55,
	arbiter_all_write = 0xaa,
	arbiter_all_accesses = 0xff
};

int crisv32_arbiter_allocate_bandwidth(int client, int region,
		unsigned long bandwidth);
int crisv32_arbiter_watch(unsigned long start, unsigned long size,
		unsigned long clients, unsigned long accesses,
		watch_callback * cb);
int crisv32_arbiter_unwatch(int id);

#endif
