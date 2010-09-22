/*
 * Copyright (c) 2007 Bruno Randolf <bruno@thinktube.com>
 *
 *  This file is free software: you may copy, redistribute and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation, either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  This file is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * Copyright (c) 2004-2005 Atheros Communications, Inc.
 * Copyright (c) 2006 Devicescape Software, Inc.
 * Copyright (c) 2007 Jiri Slaby <jirislaby@gmail.com>
 * Copyright (c) 2007 Luis R. Rodriguez <mcgrof@winlab.rutgers.edu>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#ifndef _ATH5K_DEBUG_H
#define _ATH5K_DEBUG_H

struct ath5k_softc;
struct ath5k_hw;
struct sk_buff;
struct ath5k_buf;

struct ath5k_dbg_info {
	unsigned int		level;		/* debug level */
	/* debugfs entries */
	struct dentry		*debugfs_phydir;
	struct dentry		*debugfs_debug;
	struct dentry		*debugfs_registers;
	struct dentry		*debugfs_beacon;
	struct dentry		*debugfs_reset;
	struct dentry		*debugfs_antenna;
	struct dentry		*debugfs_misc;
	struct dentry		*debugfs_frameerrors;
	struct dentry		*debugfs_ani;
	struct dentry		*debugfs_queue;
};

/**
 * enum ath5k_debug_level - ath5k debug level
 *
 * @ATH5K_DEBUG_RESET: reset processing
 * @ATH5K_DEBUG_INTR: interrupt handling
 * @ATH5K_DEBUG_MODE: mode init/setup
 * @ATH5K_DEBUG_XMIT: basic xmit operation
 * @ATH5K_DEBUG_BEACON: beacon handling
 * @ATH5K_DEBUG_CALIBRATE: periodic calibration
 * @ATH5K_DEBUG_TXPOWER: transmit power setting
 * @ATH5K_DEBUG_LED: led management
 * @ATH5K_DEBUG_DUMP_RX: print received skb content
 * @ATH5K_DEBUG_DUMP_TX: print transmit skb content
 * @ATH5K_DEBUG_DUMPBANDS: dump bands
 * @ATH5K_DEBUG_TRACE: trace function calls
 * @ATH5K_DEBUG_DESC: descriptor setup
 * @ATH5K_DEBUG_ANY: show at any debug level
 *
 * The debug level is used to control the amount and type of debugging output
 * we want to see. The debug level is given in calls to ATH5K_DBG to specify
 * where the message should appear, and the user can control the debugging
 * messages he wants to see, either by the module parameter 'debug' on module
 * load, or dynamically by using debugfs 'ath5k/phyX/debug'. these levels can
 * be combined together by bitwise OR.
 */
enum ath5k_debug_level {
	ATH5K_DEBUG_RESET	= 0x00000001,
	ATH5K_DEBUG_INTR	= 0x00000002,
	ATH5K_DEBUG_MODE	= 0x00000004,
	ATH5K_DEBUG_XMIT	= 0x00000008,
	ATH5K_DEBUG_BEACON	= 0x00000010,
	ATH5K_DEBUG_CALIBRATE	= 0x00000020,
	ATH5K_DEBUG_TXPOWER	= 0x00000040,
	ATH5K_DEBUG_LED		= 0x00000080,
	ATH5K_DEBUG_DUMP_RX	= 0x00000100,
	ATH5K_DEBUG_DUMP_TX	= 0x00000200,
	ATH5K_DEBUG_DUMPBANDS	= 0x00000400,
	ATH5K_DEBUG_ANI		= 0x00002000,
	ATH5K_DEBUG_DESC	= 0x00004000,
	ATH5K_DEBUG_ANY		= 0xffffffff
};

#ifdef CONFIG_ATH5K_DEBUG

#define ATH5K_DBG(_sc, _m, _fmt, ...) do { \
	if (unlikely((_sc)->debug.level & (_m) && net_ratelimit())) \
		ATH5K_PRINTK(_sc, KERN_DEBUG, "(%s:%d): " _fmt, \
			__func__, __LINE__, ##__VA_ARGS__); \
	} while (0)

#define ATH5K_DBG_UNLIMIT(_sc, _m, _fmt, ...) do { \
	if (unlikely((_sc)->debug.level & (_m))) \
		ATH5K_PRINTK(_sc, KERN_DEBUG, "(%s:%d): " _fmt, \
			__func__, __LINE__, ##__VA_ARGS__); \
	} while (0)

void
ath5k_debug_init(void);

void
ath5k_debug_init_device(struct ath5k_softc *sc);

void
ath5k_debug_finish(void);

void
ath5k_debug_finish_device(struct ath5k_softc *sc);

void
ath5k_debug_printrxbuffs(struct ath5k_softc *sc, struct ath5k_hw *ah);

void
ath5k_debug_dump_bands(struct ath5k_softc *sc);

void
ath5k_debug_dump_skb(struct ath5k_softc *sc,
			struct sk_buff *skb, const char *prefix, int tx);

void
ath5k_debug_printtxbuf(struct ath5k_softc *sc, struct ath5k_buf *bf);

#else /* no debugging */

#include <linux/compiler.h>

static inline void __attribute__ ((format (printf, 3, 4)))
ATH5K_DBG(struct ath5k_softc *sc, unsigned int m, const char *fmt, ...) {}

static inline void __attribute__ ((format (printf, 3, 4)))
ATH5K_DBG_UNLIMIT(struct ath5k_softc *sc, unsigned int m, const char *fmt, ...)
{}

static inline void
ath5k_debug_init(void) {}

static inline void
ath5k_debug_init_device(struct ath5k_softc *sc) {}

static inline void
ath5k_debug_finish(void) {}

static inline void
ath5k_debug_finish_device(struct ath5k_softc *sc) {}

static inline void
ath5k_debug_printrxbuffs(struct ath5k_softc *sc, struct ath5k_hw *ah) {}

static inline void
ath5k_debug_dump_bands(struct ath5k_softc *sc) {}

static inline void
ath5k_debug_dump_skb(struct ath5k_softc *sc,
			struct sk_buff *skb, const char *prefix, int tx) {}

static inline void
ath5k_debug_printtxbuf(struct ath5k_softc *sc, struct ath5k_buf *bf) {}

#endif /* ifdef CONFIG_ATH5K_DEBUG */

#endif /* ifndef _ATH5K_DEBUG_H */
