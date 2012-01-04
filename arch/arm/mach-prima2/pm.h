/*
 * arch/arm/mach-prima2/pm.h
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 *
 * Licensed under GPLv2 or later.
 */

#ifndef _MACH_PRIMA2_PM_H_
#define _MACH_PRIMA2_PM_H_

#define SIRFSOC_PWR_SLEEPFORCE		0x01

#define SIRFSOC_SLEEP_MODE_MASK         0x3
#define SIRFSOC_DEEP_SLEEP_MODE         0x1

#define SIRFSOC_PWRC_PDN_CTRL           0x0
#define SIRFSOC_PWRC_PON_OFF            0x4
#define SIRFSOC_PWRC_TRIGGER_EN         0x8
#define SIRFSOC_PWRC_PIN_STATUS         0x14
#define SIRFSOC_PWRC_SCRATCH_PAD1       0x18
#define SIRFSOC_PWRC_SCRATCH_PAD2       0x1C

#ifndef __ASSEMBLY__
extern int sirfsoc_finish_suspend(unsigned long);
#endif

#endif

