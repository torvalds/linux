/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

#ifndef __UBI_DEBUG_H__
#define __UBI_DEBUG_H__

void ubi_dump_flash(struct ubi_device *ubi, int pnum, int offset, int len);
void ubi_dump_ec_hdr(const struct ubi_ec_hdr *ec_hdr);
void ubi_dump_vid_hdr(const struct ubi_vid_hdr *vid_hdr);

#include <linux/random.h>

#define ubi_assert(expr)  do {                                               \
	if (unlikely(!(expr))) {                                             \
		pr_crit("UBI assert failed in %s at %u (pid %d)\n",          \
		       __func__, __LINE__, current->pid);                    \
		dump_stack();                                                \
	}                                                                    \
} while (0)

#define ubi_dbg_print_hex_dump(l, ps, pt, r, g, b, len, a)                   \
		print_hex_dump(l, ps, pt, r, g, b, len, a)

#define ubi_dbg_msg(type, fmt, ...) \
	pr_debug("UBI DBG " type " (pid %d): " fmt "\n", current->pid,       \
		 ##__VA_ARGS__)

/* General debugging messages */
#define dbg_gen(fmt, ...) ubi_dbg_msg("gen", fmt, ##__VA_ARGS__)
/* Messages from the eraseblock association sub-system */
#define dbg_eba(fmt, ...) ubi_dbg_msg("eba", fmt, ##__VA_ARGS__)
/* Messages from the wear-leveling sub-system */
#define dbg_wl(fmt, ...)  ubi_dbg_msg("wl", fmt, ##__VA_ARGS__)
/* Messages from the input/output sub-system */
#define dbg_io(fmt, ...)  ubi_dbg_msg("io", fmt, ##__VA_ARGS__)
/* Initialization and build messages */
#define dbg_bld(fmt, ...) ubi_dbg_msg("bld", fmt, ##__VA_ARGS__)

void ubi_dump_vol_info(const struct ubi_volume *vol);
void ubi_dump_vtbl_record(const struct ubi_vtbl_record *r, int idx);
void ubi_dump_av(const struct ubi_ainf_volume *av);
void ubi_dump_aeb(const struct ubi_ainf_peb *aeb, int type);
void ubi_dump_mkvol_req(const struct ubi_mkvol_req *req);
int ubi_self_check_all_ff(struct ubi_device *ubi, int pnum, int offset,
			  int len);
int ubi_debugfs_init(void);
void ubi_debugfs_exit(void);
int ubi_debugfs_init_dev(struct ubi_device *ubi);
void ubi_debugfs_exit_dev(struct ubi_device *ubi);

/**
 * ubi_dbg_is_bgt_disabled - if the background thread is disabled.
 * @ubi: UBI device description object
 *
 * Returns non-zero if the UBI background thread is disabled for testing
 * purposes.
 */
static inline int ubi_dbg_is_bgt_disabled(const struct ubi_device *ubi)
{
	return ubi->dbg.disable_bgt;
}

/**
 * ubi_dbg_is_bitflip - if it is time to emulate a bit-flip.
 * @ubi: UBI device description object
 *
 * Returns non-zero if a bit-flip should be emulated, otherwise returns zero.
 */
static inline int ubi_dbg_is_bitflip(const struct ubi_device *ubi)
{
	if (ubi->dbg.emulate_bitflips)
		return !(prandom_u32() % 200);
	return 0;
}

/**
 * ubi_dbg_is_write_failure - if it is time to emulate a write failure.
 * @ubi: UBI device description object
 *
 * Returns non-zero if a write failure should be emulated, otherwise returns
 * zero.
 */
static inline int ubi_dbg_is_write_failure(const struct ubi_device *ubi)
{
	if (ubi->dbg.emulate_io_failures)
		return !(prandom_u32() % 500);
	return 0;
}

/**
 * ubi_dbg_is_erase_failure - if its time to emulate an erase failure.
 * @ubi: UBI device description object
 *
 * Returns non-zero if an erase failure should be emulated, otherwise returns
 * zero.
 */
static inline int ubi_dbg_is_erase_failure(const struct ubi_device *ubi)
{
	if (ubi->dbg.emulate_io_failures)
		return !(prandom_u32() % 400);
	return 0;
}

static inline int ubi_dbg_chk_io(const struct ubi_device *ubi)
{
	return ubi->dbg.chk_io;
}

static inline int ubi_dbg_chk_gen(const struct ubi_device *ubi)
{
	return ubi->dbg.chk_gen;
}

static inline int ubi_dbg_chk_fastmap(const struct ubi_device *ubi)
{
	return ubi->dbg.chk_fastmap;
}

static inline void ubi_enable_dbg_chk_fastmap(struct ubi_device *ubi)
{
	ubi->dbg.chk_fastmap = 1;
}

int ubi_dbg_power_cut(struct ubi_device *ubi, int caller);
#endif /* !__UBI_DEBUG_H__ */
