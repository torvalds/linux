#ifndef _ASM_HPET_H
#define _ASM_HPET_H

#ifdef CONFIG_RS780_HPET

#define HPET_MMAP_SIZE		1024

#define HPET_ID			0x000
#define HPET_PERIOD		0x004
#define HPET_CFG		0x010
#define HPET_STATUS		0x020
#define HPET_COUNTER	0x0f0

#define HPET_Tn_CFG(n)		(0x100 + 0x20 * n)
#define HPET_Tn_CMP(n)		(0x108 + 0x20 * n)
#define HPET_Tn_ROUTE(n)	(0x110 + 0x20 * n)

#define HPET_T0_IRS		0x001
#define HPET_T1_IRS		0x002
#define HPET_T3_IRS		0x004

#define HPET_T0_CFG		0x100
#define HPET_T0_CMP		0x108
#define HPET_T0_ROUTE	0x110
#define HPET_T1_CFG		0x120
#define HPET_T1_CMP		0x128
#define HPET_T1_ROUTE	0x130
#define HPET_T2_CFG		0x140
#define HPET_T2_CMP		0x148
#define HPET_T2_ROUTE	0x150

#define HPET_ID_REV			0x000000ff
#define HPET_ID_NUMBER		0x00001f00
#define HPET_ID_64BIT		0x00002000
#define HPET_ID_LEGSUP		0x00008000
#define HPET_ID_VENDOR		0xffff0000
#define HPET_ID_NUMBER_SHIFT	8
#define HPET_ID_VENDOR_SHIFT	16

#define HPET_CFG_ENABLE		0x001
#define HPET_CFG_LEGACY		0x002
#define HPET_LEGACY_8254		2
#define HPET_LEGACY_RTC		8

#define HPET_TN_LEVEL		0x0002
#define HPET_TN_ENABLE		0x0004
#define HPET_TN_PERIODIC	0x0008
#define HPET_TN_PERIODIC_CAP	0x0010
#define HPET_TN_64BIT_CAP	0x0020
#define HPET_TN_SETVAL		0x0040
#define HPET_TN_32BIT		0x0100
#define HPET_TN_ROUTE		0x3e00
#define HPET_TN_FSB			0x4000
#define HPET_TN_FSB_CAP		0x8000
#define HPET_TN_ROUTE_SHIFT	9

/* Max HPET Period is 10^8 femto sec as in HPET spec */
#define HPET_MAX_PERIOD		100000000UL
/*
 * Min HPET period is 10^5 femto sec just for safety. If it is less than this,
 * then 32 bit HPET counter wrapsaround in less than 0.5 sec.
 */
#define HPET_MIN_PERIOD		100000UL

#define HPET_ADDR		0x20000
#define HPET_MMIO_ADDR	0x90000e0000020000
#define HPET_FREQ		14318780
#define HPET_COMPARE_VAL	((HPET_FREQ + HZ / 2) / HZ)
#define HPET_T0_IRQ		0

extern void __init setup_hpet_timer(void);
#endif /* CONFIG_RS780_HPET */
#endif /* _ASM_HPET_H */
