/* vi: set sw=4 ts=4: */
/*
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#ifndef APPLET_METADATA_H
#define APPLET_METADATA_H 1

/* Note: can be included by both host and target builds! */

/* order matters: used as index into "install_dir[]" in appletlib.c */
typedef enum bb_install_loc_t {
	BB_DIR_ROOT = 0,
	BB_DIR_BIN,
	BB_DIR_SBIN,
#if ENABLE_INSTALL_NO_USR
	BB_DIR_USR_BIN  = BB_DIR_BIN,
	BB_DIR_USR_SBIN = BB_DIR_SBIN,
#else
	BB_DIR_USR_BIN,
	BB_DIR_USR_SBIN,
#endif
} bb_install_loc_t;

typedef enum bb_suid_t {
	BB_SUID_DROP = 0,
	BB_SUID_MAYBE,
	BB_SUID_REQUIRE
} bb_suid_t;

#endif
