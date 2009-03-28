/*
 * Copyright (c) 2008-2009 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef DEBUG_H
#define DEBUG_H

enum ATH_DEBUG {
	ATH_DBG_RESET		= 0x00000001,
	ATH_DBG_REG_IO		= 0x00000002,
	ATH_DBG_QUEUE		= 0x00000004,
	ATH_DBG_EEPROM		= 0x00000008,
	ATH_DBG_CALIBRATE	= 0x00000010,
	ATH_DBG_CHANNEL		= 0x00000020,
	ATH_DBG_INTERRUPT	= 0x00000040,
	ATH_DBG_REGULATORY	= 0x00000080,
	ATH_DBG_ANI		= 0x00000100,
	ATH_DBG_POWER_MGMT	= 0x00000200,
	ATH_DBG_XMIT		= 0x00000400,
	ATH_DBG_BEACON		= 0x00001000,
	ATH_DBG_CONFIG		= 0x00002000,
	ATH_DBG_KEYCACHE	= 0x00004000,
	ATH_DBG_FATAL		= 0x00008000,
	ATH_DBG_ANY		= 0xffffffff
};

#define DBG_DEFAULT (ATH_DBG_FATAL)

#ifdef CONFIG_ATH9K_DEBUG

/**
 * struct ath_interrupt_stats - Contains statistics about interrupts
 * @total: Total no. of interrupts generated so far
 * @rxok: RX with no errors
 * @rxeol: RX with no more RXDESC available
 * @rxorn: RX FIFO overrun
 * @txok: TX completed at the requested rate
 * @txurn: TX FIFO underrun
 * @mib: MIB regs reaching its threshold
 * @rxphyerr: RX with phy errors
 * @rx_keycache_miss: RX with key cache misses
 * @swba: Software Beacon Alert
 * @bmiss: Beacon Miss
 * @bnr: Beacon Not Ready
 * @cst: Carrier Sense TImeout
 * @gtt: Global TX Timeout
 * @tim: RX beacon TIM occurrence
 * @cabend: RX End of CAB traffic
 * @dtimsync: DTIM sync lossage
 * @dtim: RX Beacon with DTIM
 */
struct ath_interrupt_stats {
	u32 total;
	u32 rxok;
	u32 rxeol;
	u32 rxorn;
	u32 txok;
	u32 txeol;
	u32 txurn;
	u32 mib;
	u32 rxphyerr;
	u32 rx_keycache_miss;
	u32 swba;
	u32 bmiss;
	u32 bnr;
	u32 cst;
	u32 gtt;
	u32 tim;
	u32 cabend;
	u32 dtimsync;
	u32 dtim;
};

struct ath_legacy_rc_stats {
	u32 success;
};

struct ath_11n_rc_stats {
	u32 success;
	u32 retries;
	u32 xretries;
	u8 per;
};

struct ath_stats {
	struct ath_interrupt_stats istats;
	struct ath_legacy_rc_stats legacy_rcstats[12];	/* max(11a,11b,11g) */
	struct ath_11n_rc_stats n_rcstats[16];		/* 0..15 MCS rates */
};

struct ath9k_debug {
	int debug_mask;
	struct dentry *debugfs_phy;
	struct dentry *debugfs_dma;
	struct dentry *debugfs_interrupt;
	struct dentry *debugfs_rcstat;
	struct dentry *debugfs_wiphy;
	struct ath_stats stats;
};

void DPRINTF(struct ath_softc *sc, int dbg_mask, const char *fmt, ...);
int ath9k_init_debug(struct ath_softc *sc);
void ath9k_exit_debug(struct ath_softc *sc);
int ath9k_debug_create_root(void);
void ath9k_debug_remove_root(void);
void ath_debug_stat_interrupt(struct ath_softc *sc, enum ath9k_int status);
void ath_debug_stat_rc(struct ath_softc *sc, struct sk_buff *skb);
void ath_debug_stat_retries(struct ath_softc *sc, int rix,
			    int xretries, int retries, u8 per);

#else

static inline void DPRINTF(struct ath_softc *sc, int dbg_mask,
			   const char *fmt, ...)
{
}

static inline int ath9k_init_debug(struct ath_softc *sc)
{
	return 0;
}

static inline void ath9k_exit_debug(struct ath_softc *sc)
{
}

static inline int ath9k_debug_create_root(void)
{
	return 0;
}

static inline void ath9k_debug_remove_root(void)
{
}

static inline void ath_debug_stat_interrupt(struct ath_softc *sc,
					    enum ath9k_int status)
{
}

static inline void ath_debug_stat_rc(struct ath_softc *sc,
				     struct sk_buff *skb)
{
}

static inline void ath_debug_stat_retries(struct ath_softc *sc, int rix,
					  int xretries, int retries, u8 per)
{
}

#endif /* CONFIG_ATH9K_DEBUG */

#endif /* DEBUG_H */
