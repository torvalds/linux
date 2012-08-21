/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

#ifndef __UBI_DEBUG_H__
#define __UBI_DEBUG_H__

struct ubi_ec_hdr;
struct ubi_vid_hdr;
struct ubi_volume;
struct ubi_vtbl_record;
struct ubi_scan_volume;
struct ubi_scan_leb;
struct ubi_mkvol_req;

#ifdef CONFIG_MTD_UBI_DEBUG
#include <linux/random.h>

#define ubi_assert(expr)  do {                                               \
	if (unlikely(!(expr))) {                                             \
		printk(KERN_CRIT "UBI assert failed in %s at %u (pid %d)\n", \
		       __func__, __LINE__, current->pid);                    \
		ubi_dbg_dump_stack();                                        \
	}                                                                    \
} while (0)

#define dbg_err(fmt, ...) ubi_err(fmt, ##__VA_ARGS__)

#define ubi_dbg_dump_stack() dump_stack()

#define ubi_dbg_print_hex_dump(l, ps, pt, r, g, b, len, a)  \
		print_hex_dump(l, ps, pt, r, g, b, len, a)

#define ubi_dbg_msg(type, fmt, ...) \
	pr_debug("UBI DBG " type ": " fmt "\n", ##__VA_ARGS__)

/* Just a debugging messages not related to any specific UBI subsystem */
#define dbg_msg(fmt, ...)                                    \
	printk(KERN_DEBUG "UBI DBG (pid %d): %s: " fmt "\n", \
	       current->pid, __func__, ##__VA_ARGS__)

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

void ubi_dbg_dump_ec_hdr(const struct ubi_ec_hdr *ec_hdr);
void ubi_dbg_dump_vid_hdr(const struct ubi_vid_hdr *vid_hdr);
void ubi_dbg_dump_vol_info(const struct ubi_volume *vol);
void ubi_dbg_dump_vtbl_record(const struct ubi_vtbl_record *r, int idx);
void ubi_dbg_dump_sv(const struct ubi_scan_volume *sv);
void ubi_dbg_dump_seb(const struct ubi_scan_leb *seb, int type);
void ubi_dbg_dump_mkvol_req(const struct ubi_mkvol_req *req);
void ubi_dbg_dump_flash(struct ubi_device *ubi, int pnum, int offset, int len);

extern unsigned int ubi_chk_flags;

/*
 * Debugging check flags.
 *
 * UBI_CHK_GEN: general checks
 * UBI_CHK_IO: check writes and erases
 */
enum {
	UBI_CHK_GEN = 0x1,
	UBI_CHK_IO  = 0x2,
};

int ubi_dbg_check_all_ff(struct ubi_device *ubi, int pnum, int offset, int len);
int ubi_dbg_check_write(struct ubi_device *ubi, const void *buf, int pnum,
			int offset, int len);

extern unsigned int ubi_tst_flags;

/*
 * Special testing flags.
 *
 * UBIFS_TST_DISABLE_BGT: disable the background thread
 * UBI_TST_EMULATE_BITFLIPS: emulate bit-flips
 * UBI_TST_EMULATE_WRITE_FAILURES: emulate write failures
 * UBI_TST_EMULATE_ERASE_FAILURES: emulate erase failures
 */
enum {
	UBI_TST_DISABLE_BGT            = 0x1,
	UBI_TST_EMULATE_BITFLIPS       = 0x2,
	UBI_TST_EMULATE_WRITE_FAILURES = 0x4,
	UBI_TST_EMULATE_ERASE_FAILURES = 0x8,
};

/**
 * ubi_dbg_is_bgt_disabled - if the background thread is disabled.
 *
 * Returns non-zero if the UBI background thread is disabled for testing
 * purposes.
 */
static inline int ubi_dbg_is_bgt_disabled(void)
{
	return ubi_tst_flags & UBI_TST_DISABLE_BGT;
}

/**
 * ubi_dbg_is_bitflip - if it is time to emulate a bit-flip.
 *
 * Returns non-zero if a bit-flip should be emulated, otherwise returns zero.
 */
static inline int ubi_dbg_is_bitflip(void)
{
	if (ubi_tst_flags & UBI_TST_EMULATE_BITFLIPS)
		return !(random32() % 200);
	return 0;
}

/**
 * ubi_dbg_is_write_failure - if it is time to emulate a write failure.
 *
 * Returns non-zero if a write failure should be emulated, otherwise returns
 * zero.
 */
static inline int ubi_dbg_is_write_failure(void)
{
	if (ubi_tst_flags & UBI_TST_EMULATE_WRITE_FAILURES)
		return !(random32() % 500);
	return 0;
}

/**
 * ubi_dbg_is_erase_failure - if its time to emulate an erase failure.
 *
 * Returns non-zero if an erase failure should be emulated, otherwise returns
 * zero.
 */
static inline int ubi_dbg_is_erase_failure(void)
{
	if (ubi_tst_flags & UBI_TST_EMULATE_ERASE_FAILURES)
		return !(random32() % 400);
	return 0;
}

#else

/* Use "if (0)" to make compiler check arguments even if debugging is off */
#define ubi_assert(expr)  do {                                               \
	if (0) {                                                             \
		printk(KERN_CRIT "UBI assert failed in %s at %u (pid %d)\n", \
		       __func__, __LINE__, current->pid);                    \
	}                                                                    \
} while (0)

#define dbg_err(fmt, ...) do {                                               \
	if (0)                                                               \
		ubi_err(fmt, ##__VA_ARGS__);                                 \
} while (0)

#define ubi_dbg_msg(fmt, ...) do {                                           \
	if (0)                                                               \
		pr_debug(fmt "\n", ##__VA_ARGS__);                           \
} while (0)

#define dbg_msg(fmt, ...)  ubi_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_gen(fmt, ...)  ubi_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_eba(fmt, ...)  ubi_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_wl(fmt, ...)   ubi_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_io(fmt, ...)   ubi_dbg_msg(fmt, ##__VA_ARGS__)
#define dbg_bld(fmt, ...)  ubi_dbg_msg(fmt, ##__VA_ARGS__)

static inline void ubi_dbg_dump_stack(void)                          { return; }
static inline void
ubi_dbg_dump_ec_hdr(const struct ubi_ec_hdr *ec_hdr)                 { return; }
static inline void
ubi_dbg_dump_vid_hdr(const struct ubi_vid_hdr *vid_hdr)              { return; }
static inline void
ubi_dbg_dump_vol_info(const struct ubi_volume *vol)                  { return; }
static inline void
ubi_dbg_dump_vtbl_record(const struct ubi_vtbl_record *r, int idx)   { return; }
static inline void ubi_dbg_dump_sv(const struct ubi_scan_volume *sv) { return; }
static inline void ubi_dbg_dump_seb(const struct ubi_scan_leb *seb,
				    int type)                        { return; }
static inline void
ubi_dbg_dump_mkvol_req(const struct ubi_mkvol_req *req)              { return; }
static inline void ubi_dbg_dump_flash(struct ubi_device *ubi,
				      int pnum, int offset, int len) { return; }
static inline void
ubi_dbg_print_hex_dump(const char *l, const char *ps, int pt, int r,
		       int g, const void *b, size_t len, bool a)     { return; }

static inline int ubi_dbg_is_bgt_disabled(void)                    { return 0; }
static inline int ubi_dbg_is_bitflip(void)                         { return 0; }
static inline int ubi_dbg_is_write_failure(void)                   { return 0; }
static inline int ubi_dbg_is_erase_failure(void)                   { return 0; }
static inline int ubi_dbg_check_all_ff(struct ubi_device *ubi,
				       int pnum, int offset,
				       int len)                    { return 0; }
static inline int ubi_dbg_check_write(struct ubi_device *ubi,
				      const void *buf, int pnum,
				      int offset, int len)         { return 0; }

#endif /* !CONFIG_MTD_UBI_DEBUG */
#endif /* !__UBI_DEBUG_H__ */
