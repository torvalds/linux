/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HPET_H
#define _ASM_X86_HPET_H

#include <linux/msi.h>

#ifdef CONFIG_HPET_TIMER

#define HPET_MMAP_SIZE		1024

#define HPET_ID			0x000
#define HPET_PERIOD		0x004
#define HPET_CFG		0x010
#define HPET_STATUS		0x020
#define HPET_COUNTER		0x0f0

#define HPET_Tn_CFG(n)		(0x100 + 0x20 * n)
#define HPET_Tn_CMP(n)		(0x108 + 0x20 * n)
#define HPET_Tn_ROUTE(n)	(0x110 + 0x20 * n)

#define HPET_T0_CFG		0x100
#define HPET_T0_CMP		0x108
#define HPET_T0_ROUTE		0x110
#define HPET_T1_CFG		0x120
#define HPET_T1_CMP		0x128
#define HPET_T1_ROUTE		0x130
#define HPET_T2_CFG		0x140
#define HPET_T2_CMP		0x148
#define HPET_T2_ROUTE		0x150

#define HPET_ID_REV		0x000000ff
#define HPET_ID_NUMBER		0x00001f00
#define HPET_ID_64BIT		0x00002000
#define HPET_ID_LEGSUP		0x00008000
#define HPET_ID_VENDOR		0xffff0000
#define	HPET_ID_NUMBER_SHIFT	8
#define HPET_ID_VENDOR_SHIFT	16

#define HPET_CFG_ENABLE		0x001
#define HPET_CFG_LEGACY		0x002
#define	HPET_LEGACY_8254	2
#define	HPET_LEGACY_RTC		8

#define HPET_TN_LEVEL		0x0002
#define HPET_TN_ENABLE		0x0004
#define HPET_TN_PERIODIC	0x0008
#define HPET_TN_PERIODIC_CAP	0x0010
#define HPET_TN_64BIT_CAP	0x0020
#define HPET_TN_SETVAL		0x0040
#define HPET_TN_32BIT		0x0100
#define HPET_TN_ROUTE		0x3e00
#define HPET_TN_FSB		0x4000
#define HPET_TN_FSB_CAP		0x8000
#define HPET_TN_ROUTE_SHIFT	9

/* Max HPET Period is 10^8 femto sec as in HPET spec */
#define HPET_MAX_PERIOD		100000000UL
/*
 * Min HPET period is 10^5 femto sec just for safety. If it is less than this,
 * then 32 bit HPET counter wrapsaround in less than 0.5 sec.
 */
#define HPET_MIN_PERIOD		100000UL

/* hpet memory map physical address */
extern unsigned long hpet_address;
extern unsigned long force_hpet_address;
extern bool boot_hpet_disable;
extern u8 hpet_blockid;
extern bool hpet_force_user;
extern bool hpet_msi_disable;
extern int is_hpet_enabled(void);
extern int hpet_enable(void);
extern void hpet_disable(void);
extern unsigned int hpet_readl(unsigned int a);
extern void force_hpet_resume(void);

struct irq_data;
struct hpet_dev;
struct irq_domain;

extern void hpet_msi_unmask(struct irq_data *data);
extern void hpet_msi_mask(struct irq_data *data);
extern void hpet_msi_write(struct hpet_dev *hdev, struct msi_msg *msg);
extern void hpet_msi_read(struct hpet_dev *hdev, struct msi_msg *msg);
extern struct irq_domain *hpet_create_irq_domain(int hpet_id);
extern int hpet_assign_irq(struct irq_domain *domain,
			   struct hpet_dev *dev, int dev_num);

#ifdef CONFIG_HPET_EMULATE_RTC

#include <linux/interrupt.h>

typedef irqreturn_t (*rtc_irq_handler)(int interrupt, void *cookie);
extern int hpet_mask_rtc_irq_bit(unsigned long bit_mask);
extern int hpet_set_rtc_irq_bit(unsigned long bit_mask);
extern int hpet_set_alarm_time(unsigned char hrs, unsigned char min,
			       unsigned char sec);
extern int hpet_set_periodic_freq(unsigned long freq);
extern int hpet_rtc_dropped_irq(void);
extern int hpet_rtc_timer_init(void);
extern irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id);
extern int hpet_register_irq_handler(rtc_irq_handler handler);
extern void hpet_unregister_irq_handler(rtc_irq_handler handler);

#endif /* CONFIG_HPET_EMULATE_RTC */

#else /* CONFIG_HPET_TIMER */

static inline int hpet_enable(void) { return 0; }
static inline int is_hpet_enabled(void) { return 0; }
#define hpet_readl(a) 0
#define default_setup_hpet_msi	NULL

#endif
#endif /* _ASM_X86_HPET_H */
