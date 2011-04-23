/*
 * arch/arm/mach-at91/include/mach/cpu.h
 *
 * Copyright (C) 2006 SAN People
 * Copyright (C) 2011 Jean-Christophe PLAGNIOL-VILLARD <plagnioj@jcrosoft.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __MACH_CPU_H__
#define __MACH_CPU_H__

#define ARCH_ID_AT91RM9200	0x09290780
#define ARCH_ID_AT91SAM9260	0x019803a0
#define ARCH_ID_AT91SAM9261	0x019703a0
#define ARCH_ID_AT91SAM9263	0x019607a0
#define ARCH_ID_AT91SAM9G10	0x019903a0
#define ARCH_ID_AT91SAM9G20	0x019905a0
#define ARCH_ID_AT91SAM9RL64	0x019b03a0
#define ARCH_ID_AT91SAM9G45	0x819b05a0
#define ARCH_ID_AT91SAM9G45MRL	0x819b05a2	/* aka 9G45-ES2 & non ES lots */
#define ARCH_ID_AT91SAM9G45ES	0x819b05a1	/* 9G45-ES (Engineering Sample) */
#define ARCH_ID_AT91SAM9X5	0x819a05a0
#define ARCH_ID_AT91CAP9	0x039A03A0

#define ARCH_ID_AT91SAM9XE128	0x329973a0
#define ARCH_ID_AT91SAM9XE256	0x329a93a0
#define ARCH_ID_AT91SAM9XE512	0x329aa3a0

#define ARCH_ID_AT91M40800	0x14080044
#define ARCH_ID_AT91R40807	0x44080746
#define ARCH_ID_AT91M40807	0x14080745
#define ARCH_ID_AT91R40008	0x44000840

#define ARCH_EXID_AT91SAM9M11	0x00000001
#define ARCH_EXID_AT91SAM9M10	0x00000002
#define ARCH_EXID_AT91SAM9G46	0x00000003
#define ARCH_EXID_AT91SAM9G45	0x00000004

#define ARCH_EXID_AT91SAM9G15	0x00000000
#define ARCH_EXID_AT91SAM9G35	0x00000001
#define ARCH_EXID_AT91SAM9X35	0x00000002
#define ARCH_EXID_AT91SAM9G25	0x00000003
#define ARCH_EXID_AT91SAM9X25	0x00000004

#define ARCH_FAMILY_AT91X92	0x09200000
#define ARCH_FAMILY_AT91SAM9	0x01900000
#define ARCH_FAMILY_AT91SAM9XE	0x02900000

/* PMC revision */
#define ARCH_REVISION_CAP9_B	0x399
#define ARCH_REVISION_CAP9_C	0x601

/* RM9200 type */
#define ARCH_REVISON_9200_BGA	(0 << 0)
#define ARCH_REVISON_9200_PQFP	(1 << 0)

enum at91_soc_type {
	/* 920T */
	AT91_SOC_RM9200,

	/* CAP */
	AT91_SOC_CAP9,

	/* SAM92xx */
	AT91_SOC_SAM9260, AT91_SOC_SAM9261, AT91_SOC_SAM9263,

	/* SAM9Gxx */
	AT91_SOC_SAM9G10, AT91_SOC_SAM9G20, AT91_SOC_SAM9G45,

	/* SAM9RL */
	AT91_SOC_SAM9RL,

	/* SAM9X5 */
	AT91_SOC_SAM9X5,

	/* Unknown type */
	AT91_SOC_NONE
};

enum at91_soc_subtype {
	/* RM9200 */
	AT91_SOC_RM9200_BGA, AT91_SOC_RM9200_PQFP,

	/* CAP9 */
	AT91_SOC_CAP9_REV_B, AT91_SOC_CAP9_REV_C,

	/* SAM9260 */
	AT91_SOC_SAM9XE,

	/* SAM9G45 */
	AT91_SOC_SAM9G45ES, AT91_SOC_SAM9M10, AT91_SOC_SAM9G46, AT91_SOC_SAM9M11,

	/* SAM9X5 */
	AT91_SOC_SAM9G15, AT91_SOC_SAM9G35, AT91_SOC_SAM9X35,
	AT91_SOC_SAM9G25, AT91_SOC_SAM9X25,

	/* Unknown subtype */
	AT91_SOC_SUBTYPE_NONE
};

struct at91_socinfo {
	unsigned int type, subtype;
	unsigned int cidr, exid;
};

extern struct at91_socinfo at91_soc_initdata;
const char *at91_get_soc_type(struct at91_socinfo *c);
const char *at91_get_soc_subtype(struct at91_socinfo *c);

static inline int at91_soc_is_detected(void)
{
	return at91_soc_initdata.type != AT91_SOC_NONE;
}

#ifdef CONFIG_ARCH_AT91RM9200
#define cpu_is_at91rm9200()	(at91_soc_initdata.type == AT91_SOC_RM9200)
#define cpu_is_at91rm9200_bga()	(at91_soc_initdata.subtype == AT91_SOC_RM9200_BGA)
#define cpu_is_at91rm9200_pqfp() (at91_soc_initdata.subtype == AT91_SOC_RM9200_PQFP)
#else
#define cpu_is_at91rm9200()	(0)
#define cpu_is_at91rm9200_bga()	(0)
#define cpu_is_at91rm9200_pqfp() (0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9260
#define cpu_is_at91sam9xe()	(at91_soc_initdata.subtype == AT91_SOC_SAM9XE)
#define cpu_is_at91sam9260()	(at91_soc_initdata.type == AT91_SOC_SAM9260)
#else
#define cpu_is_at91sam9xe()	(0)
#define cpu_is_at91sam9260()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9G20
#define cpu_is_at91sam9g20()	(at91_soc_initdata.type == AT91_SOC_SAM9G20)
#else
#define cpu_is_at91sam9g20()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9261
#define cpu_is_at91sam9261()	(at91_soc_initdata.type == AT91_SOC_SAM9261)
#else
#define cpu_is_at91sam9261()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9G10
#define cpu_is_at91sam9g10()	(at91_soc_initdata.type == AT91_SOC_SAM9G10)
#else
#define cpu_is_at91sam9g10()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9263
#define cpu_is_at91sam9263()	(at91_soc_initdata.type == AT91_SOC_SAM9263)
#else
#define cpu_is_at91sam9263()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9RL
#define cpu_is_at91sam9rl()	(at91_soc_initdata.type == AT91_SOC_SAM9RL)
#else
#define cpu_is_at91sam9rl()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9G45
#define cpu_is_at91sam9g45()	(at91_soc_initdata.type == AT91_SOC_SAM9G45)
#define cpu_is_at91sam9g45es()	(at91_soc_initdata.subtype == AT91_SOC_SAM9G45ES)
#define cpu_is_at91sam9m10()	(at91_soc_initdata.subtype == AT91_SOC_SAM9M10)
#define cpu_is_at91sam9g46()	(at91_soc_initdata.subtype == AT91_SOC_SAM9G46)
#define cpu_is_at91sam9m11()	(at91_soc_initdata.subtype == AT91_SOC_SAM9M11)
#else
#define cpu_is_at91sam9g45()	(0)
#define cpu_is_at91sam9g45es()	(0)
#define cpu_is_at91sam9m10()	(0)
#define cpu_is_at91sam9g46()	(0)
#define cpu_is_at91sam9m11()	(0)
#endif

#ifdef CONFIG_ARCH_AT91SAM9X5
#define cpu_is_at91sam9x5()	(at91_soc_initdata.type == AT91_SOC_SAM9X5)
#define cpu_is_at91sam9g15()	(at91_soc_initdata.subtype == AT91_SOC_SAM9G15)
#define cpu_is_at91sam9g35()	(at91_soc_initdata.subtype == AT91_SOC_SAM9G35)
#define cpu_is_at91sam9x35()	(at91_soc_initdata.subtype == AT91_SOC_SAM9X35)
#define cpu_is_at91sam9g25()	(at91_soc_initdata.subtype == AT91_SOC_SAM9G25)
#define cpu_is_at91sam9x25()	(at91_soc_initdata.subtype == AT91_SOC_SAM9X25)
#else
#define cpu_is_at91sam9x5()	(0)
#define cpu_is_at91sam9g15()	(0)
#define cpu_is_at91sam9g35()	(0)
#define cpu_is_at91sam9x35()	(0)
#define cpu_is_at91sam9g25()	(0)
#define cpu_is_at91sam9x25()	(0)
#endif

#ifdef CONFIG_ARCH_AT91CAP9
#define cpu_is_at91cap9()	(at91_soc_initdata.type == AT91_SOC_CAP9)
#define cpu_is_at91cap9_revB()	(at91_soc_initdata.subtype == AT91_SOC_CAP9_REV_B)
#define cpu_is_at91cap9_revC()	(at91_soc_initdata.subtype == AT91_SOC_CAP9_REV_C)
#else
#define cpu_is_at91cap9()	(0)
#define cpu_is_at91cap9_revB()	(0)
#define cpu_is_at91cap9_revC()	(0)
#endif

/*
 * Since this is ARM, we will never run on any AVR32 CPU. But these
 * definitions may reduce clutter in common drivers.
 */
#define cpu_is_at32ap7000()	(0)

#endif /* __MACH_CPU_H__ */
