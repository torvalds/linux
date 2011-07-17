/*
 * Copyright (c) 2011 Atheros Communications Inc.
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

#include "htc_hif.h"

enum ATH6K_DEBUG_MASK {
	ATH6KL_DBG_WLAN_CONNECT = BIT(0),     /* wlan connect */
	ATH6KL_DBG_WLAN_SCAN    = BIT(1),     /* wlan scan */
	ATH6KL_DBG_WLAN_TX      = BIT(2),     /* wlan tx */
	ATH6KL_DBG_WLAN_RX      = BIT(3),     /* wlan rx */
	ATH6KL_DBG_BMI		= BIT(4),     /* bmi tracing */
	ATH6KL_DBG_HTC_SEND     = BIT(5),     /* htc send */
	ATH6KL_DBG_HTC_RECV     = BIT(6),     /* htc recv */
	ATH6KL_DBG_IRQ		= BIT(7),     /* interrupt processing */
	ATH6KL_DBG_PM           = BIT(8),     /* power management */
	ATH6KL_DBG_WLAN_NODE    = BIT(9),     /* general wlan node tracing */
	ATH6KL_DBG_WMI          = BIT(10),    /* wmi tracing */
	ATH6KL_DBG_TRC	        = BIT(11),    /* generic func tracing */
	ATH6KL_DBG_SCATTER	= BIT(12),    /* hif scatter tracing */
	ATH6KL_DBG_WLAN_CFG     = BIT(13),    /* cfg80211 i/f file tracing */
	ATH6KL_DBG_RAW_BYTES    = BIT(14),    /* dump tx/rx and wmi frames */
	ATH6KL_DBG_ANY	        = 0xffffffff  /* enable all logs */
};

extern unsigned int debug_mask;
extern int ath6kl_printk(const char *level, const char *fmt, ...)
	__attribute__ ((format (printf, 2, 3)));

#define ath6kl_info(fmt, ...)				\
	ath6kl_printk(KERN_INFO, fmt, ##__VA_ARGS__)
#define ath6kl_err(fmt, ...)					\
	ath6kl_printk(KERN_ERR, fmt, ##__VA_ARGS__)
#define ath6kl_warn(fmt, ...)					\
	ath6kl_printk(KERN_WARNING, fmt, ##__VA_ARGS__)

#define AR_DBG_LVL_CHECK(mask)	(debug_mask & mask)

#ifdef CONFIG_ATH6KL_DEBUG
#define ath6kl_dbg(mask, fmt, ...)					\
	({								\
	 int rtn;							\
	 if (debug_mask & mask)						\
		rtn = ath6kl_printk(KERN_DEBUG, fmt, ##__VA_ARGS__);	\
	 else								\
		rtn = 0;						\
									\
	 rtn;								\
	 })

static inline void ath6kl_dbg_dump(enum ATH6K_DEBUG_MASK mask,
				   const char *msg, const void *buf,
				   size_t len)
{
	if (debug_mask & mask) {
		ath6kl_dbg(mask, "%s\n", msg);
		print_hex_dump_bytes("", DUMP_PREFIX_OFFSET, buf, len);
	}
}

void ath6kl_dump_registers(struct ath6kl_device *dev,
			   struct ath6kl_irq_proc_registers *irq_proc_reg,
			   struct ath6kl_irq_enable_reg *irq_en_reg);
void dump_cred_dist_stats(struct htc_target *target);
#else
static inline int ath6kl_dbg(enum ATH6K_DEBUG_MASK dbg_mask,
			     const char *fmt, ...)
{
	return 0;
}

static inline void ath6kl_dbg_dump(enum ATH6K_DEBUG_MASK mask,
				   const char *msg, const void *buf,
				   size_t len)
{
}

static inline void ath6kl_dump_registers(struct ath6kl_device *dev,
		struct ath6kl_irq_proc_registers *irq_proc_reg,
		struct ath6kl_irq_enable_reg *irq_en_reg)
{

}
static inline void dump_cred_dist_stats(struct htc_target *target)
{
}
#endif

#endif
