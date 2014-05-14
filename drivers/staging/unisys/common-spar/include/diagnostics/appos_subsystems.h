/* Copyright © 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

/* Please note that this file is to be used ONLY for defining diagnostic
 * subsystem values for the appos (sPAR Linux service partitions) component.
 */
#ifndef __APPOS_SUBSYSTEMS_H__
#define __APPOS_SUBSYSTEMS_H__

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/string.h>
#else
#include <stdio.h>
#include <string.h>
#endif

static inline char *
subsys_unknown_to_s(int subsys, char *s, int n)
{
	snprintf(s, n, "SUBSYS-%-2.2d", subsys);
	s[n - 1] = '\0';
	return s;
}

#define SUBSYS_TO_MASK(subsys)      (1ULL << (subsys))

/* The first SUBSYS_APPOS_MAX subsystems are the same for each AppOS type
 * (IOVM, SMS, etc.) The rest have unique values for each AppOS type.
 */
#define SUBSYS_APPOS_MAX 16

#define	SUBSYS_APPOS_DEFAULT         1	/* or "other" */
#define SUBSYS_APPOS_CHIPSET         2	/* controlvm and other */
					/* low-level sPAR activity */
#define SUBSYS_APPOS_BUS             3	/* sPAR bus */
/* DAK #define SUBSYS_APPOS_DIAG            4  // diagnostics and dump */
#define SUBSYS_APPOS_CHANNELACCESS   5	/* generic channel access */
#define SUBSYS_APPOS_NICCLIENT       6	/* virtual NIC client */
#define SUBSYS_APPOS_HBACLIENT       7	/* virtual HBA client */
#define SUBSYS_APPOS_CONSOLESERIAL   8	/* sPAR virtual serial console */
#define SUBSYS_APPOS_UISLIB          9	/*  */
#define SUBSYS_APPOS_VRTCUPDD       10	/*  */
#define SUBSYS_APPOS_WATCHDOG       11	/* watchdog timer and healthcheck */
#define SUBSYS_APPOS_13             13	/* available */
#define SUBSYS_APPOS_14             14	/* available */
#define SUBSYS_APPOS_15             15	/* available */
#define SUBSYS_APPOS_16             16	/* available */
static inline char *
subsys_generic_to_s(int subsys, char *s, int n)
{
	switch (subsys) {
	case SUBSYS_APPOS_DEFAULT:
		strncpy(s, "APPOS_DEFAULT", n);
		break;
	case SUBSYS_APPOS_CHIPSET:
		strncpy(s, "APPOS_CHIPSET", n);
		break;
	case SUBSYS_APPOS_BUS:
		strncpy(s, "APPOS_BUS", n);
		break;
	case SUBSYS_APPOS_CHANNELACCESS:
		strncpy(s, "APPOS_CHANNELACCESS", n);
		break;
	case SUBSYS_APPOS_NICCLIENT:
		strncpy(s, "APPOS_NICCLIENT", n);
		break;
	case SUBSYS_APPOS_HBACLIENT:
		strncpy(s, "APPOS_HBACLIENT", n);
		break;
	case SUBSYS_APPOS_CONSOLESERIAL:
		strncpy(s, "APPOS_CONSOLESERIAL", n);
		break;
	case SUBSYS_APPOS_UISLIB:
		strncpy(s, "APPOS_UISLIB", n);
		break;
	case SUBSYS_APPOS_VRTCUPDD:
		strncpy(s, "APPOS_VRTCUPDD", n);
		break;
	case SUBSYS_APPOS_WATCHDOG:
		strncpy(s, "APPOS_WATCHDOG", n);
		break;
	case SUBSYS_APPOS_13:
		strncpy(s, "APPOS_13", n);
		break;
	case SUBSYS_APPOS_14:
		strncpy(s, "APPOS_14", n);
		break;
	case SUBSYS_APPOS_15:
		strncpy(s, "APPOS_15", n);
		break;
	case SUBSYS_APPOS_16:
		strncpy(s, "APPOS_16", n);
		break;
	default:
		subsys_unknown_to_s(subsys, s, n);
		break;
	}
	s[n - 1] = '\0';
	return s;
}

/* CONSOLE */

#define SUBSYS_CONSOLE_VIDEO        (SUBSYS_APPOS_MAX + 1)	/* 17 */
#define SUBSYS_CONSOLE_KBDMOU       (SUBSYS_APPOS_MAX + 2)	/* 18 */
#define SUBSYS_CONSOLE_04           (SUBSYS_APPOS_MAX + 4)
#define SUBSYS_CONSOLE_05           (SUBSYS_APPOS_MAX + 5)
#define SUBSYS_CONSOLE_06           (SUBSYS_APPOS_MAX + 6)
#define SUBSYS_CONSOLE_07           (SUBSYS_APPOS_MAX + 7)
#define SUBSYS_CONSOLE_08           (SUBSYS_APPOS_MAX + 8)
#define SUBSYS_CONSOLE_09           (SUBSYS_APPOS_MAX + 9)
#define SUBSYS_CONSOLE_10           (SUBSYS_APPOS_MAX + 10)
#define SUBSYS_CONSOLE_11           (SUBSYS_APPOS_MAX + 11)
#define SUBSYS_CONSOLE_12           (SUBSYS_APPOS_MAX + 12)
#define SUBSYS_CONSOLE_13           (SUBSYS_APPOS_MAX + 13)
#define SUBSYS_CONSOLE_14           (SUBSYS_APPOS_MAX + 14)
#define SUBSYS_CONSOLE_15           (SUBSYS_APPOS_MAX + 15)
#define SUBSYS_CONSOLE_16           (SUBSYS_APPOS_MAX + 16)
#define SUBSYS_CONSOLE_17           (SUBSYS_APPOS_MAX + 17)
#define SUBSYS_CONSOLE_18           (SUBSYS_APPOS_MAX + 18)
#define SUBSYS_CONSOLE_19           (SUBSYS_APPOS_MAX + 19)
#define SUBSYS_CONSOLE_20           (SUBSYS_APPOS_MAX + 20)
#define SUBSYS_CONSOLE_21           (SUBSYS_APPOS_MAX + 21)
#define SUBSYS_CONSOLE_22           (SUBSYS_APPOS_MAX + 22)
#define SUBSYS_CONSOLE_23           (SUBSYS_APPOS_MAX + 23)
#define SUBSYS_CONSOLE_24           (SUBSYS_APPOS_MAX + 24)
#define SUBSYS_CONSOLE_25           (SUBSYS_APPOS_MAX + 25)
#define SUBSYS_CONSOLE_26           (SUBSYS_APPOS_MAX + 26)
#define SUBSYS_CONSOLE_27           (SUBSYS_APPOS_MAX + 27)
#define SUBSYS_CONSOLE_28           (SUBSYS_APPOS_MAX + 28)
#define SUBSYS_CONSOLE_29           (SUBSYS_APPOS_MAX + 29)
#define SUBSYS_CONSOLE_30           (SUBSYS_APPOS_MAX + 30)
#define SUBSYS_CONSOLE_31           (SUBSYS_APPOS_MAX + 31)
#define SUBSYS_CONSOLE_32           (SUBSYS_APPOS_MAX + 32)
#define SUBSYS_CONSOLE_33           (SUBSYS_APPOS_MAX + 33)
#define SUBSYS_CONSOLE_34           (SUBSYS_APPOS_MAX + 34)
#define SUBSYS_CONSOLE_35           (SUBSYS_APPOS_MAX + 35)
#define SUBSYS_CONSOLE_36           (SUBSYS_APPOS_MAX + 36)
#define SUBSYS_CONSOLE_37           (SUBSYS_APPOS_MAX + 37)
#define SUBSYS_CONSOLE_38           (SUBSYS_APPOS_MAX + 38)
#define SUBSYS_CONSOLE_39           (SUBSYS_APPOS_MAX + 39)
#define SUBSYS_CONSOLE_40           (SUBSYS_APPOS_MAX + 40)
#define SUBSYS_CONSOLE_41           (SUBSYS_APPOS_MAX + 41)
#define SUBSYS_CONSOLE_42           (SUBSYS_APPOS_MAX + 42)
#define SUBSYS_CONSOLE_43           (SUBSYS_APPOS_MAX + 43)
#define SUBSYS_CONSOLE_44           (SUBSYS_APPOS_MAX + 44)
#define SUBSYS_CONSOLE_45           (SUBSYS_APPOS_MAX + 45)
#define SUBSYS_CONSOLE_46           (SUBSYS_APPOS_MAX + 46)

static inline char *
subsys_console_to_s(int subsys, char *s, int n)
{
	switch (subsys) {
	case SUBSYS_CONSOLE_VIDEO:
		strncpy(s, "CONSOLE_VIDEO", n);
		break;
	case SUBSYS_CONSOLE_KBDMOU:
		strncpy(s, "CONSOLE_KBDMOU", n);
		break;
	case SUBSYS_CONSOLE_04:
		strncpy(s, "CONSOLE_04", n);
		break;
	case SUBSYS_CONSOLE_05:
		strncpy(s, "CONSOLE_05", n);
		break;
	case SUBSYS_CONSOLE_06:
		strncpy(s, "CONSOLE_06", n);
		break;
	case SUBSYS_CONSOLE_07:
		strncpy(s, "CONSOLE_07", n);
		break;
	case SUBSYS_CONSOLE_08:
		strncpy(s, "CONSOLE_08", n);
		break;
	case SUBSYS_CONSOLE_09:
		strncpy(s, "CONSOLE_09", n);
		break;
	case SUBSYS_CONSOLE_10:
		strncpy(s, "CONSOLE_10", n);
		break;
	case SUBSYS_CONSOLE_11:
		strncpy(s, "CONSOLE_11", n);
		break;
	case SUBSYS_CONSOLE_12:
		strncpy(s, "CONSOLE_12", n);
		break;
	case SUBSYS_CONSOLE_13:
		strncpy(s, "CONSOLE_13", n);
		break;
	case SUBSYS_CONSOLE_14:
		strncpy(s, "CONSOLE_14", n);
		break;
	case SUBSYS_CONSOLE_15:
		strncpy(s, "CONSOLE_15", n);
		break;
	case SUBSYS_CONSOLE_16:
		strncpy(s, "CONSOLE_16", n);
		break;
	case SUBSYS_CONSOLE_17:
		strncpy(s, "CONSOLE_17", n);
		break;
	case SUBSYS_CONSOLE_18:
		strncpy(s, "CONSOLE_18", n);
		break;
	case SUBSYS_CONSOLE_19:
		strncpy(s, "CONSOLE_19", n);
		break;
	case SUBSYS_CONSOLE_20:
		strncpy(s, "CONSOLE_20", n);
		break;
	case SUBSYS_CONSOLE_21:
		strncpy(s, "CONSOLE_21", n);
		break;
	case SUBSYS_CONSOLE_22:
		strncpy(s, "CONSOLE_22", n);
		break;
	case SUBSYS_CONSOLE_23:
		strncpy(s, "CONSOLE_23", n);
		break;
	case SUBSYS_CONSOLE_24:
		strncpy(s, "CONSOLE_24", n);
		break;
	case SUBSYS_CONSOLE_25:
		strncpy(s, "CONSOLE_25", n);
		break;
	case SUBSYS_CONSOLE_26:
		strncpy(s, "CONSOLE_26", n);
		break;
	case SUBSYS_CONSOLE_27:
		strncpy(s, "CONSOLE_27", n);
		break;
	case SUBSYS_CONSOLE_28:
		strncpy(s, "CONSOLE_28", n);
		break;
	case SUBSYS_CONSOLE_29:
		strncpy(s, "CONSOLE_29", n);
		break;
	case SUBSYS_CONSOLE_30:
		strncpy(s, "CONSOLE_30", n);
		break;
	case SUBSYS_CONSOLE_31:
		strncpy(s, "CONSOLE_31", n);
		break;
	case SUBSYS_CONSOLE_32:
		strncpy(s, "CONSOLE_32", n);
		break;
	case SUBSYS_CONSOLE_33:
		strncpy(s, "CONSOLE_33", n);
		break;
	case SUBSYS_CONSOLE_34:
		strncpy(s, "CONSOLE_34", n);
		break;
	case SUBSYS_CONSOLE_35:
		strncpy(s, "CONSOLE_35", n);
		break;
	case SUBSYS_CONSOLE_36:
		strncpy(s, "CONSOLE_36", n);
		break;
	case SUBSYS_CONSOLE_37:
		strncpy(s, "CONSOLE_37", n);
		break;
	case SUBSYS_CONSOLE_38:
		strncpy(s, "CONSOLE_38", n);
		break;
	case SUBSYS_CONSOLE_39:
		strncpy(s, "CONSOLE_39", n);
		break;
	case SUBSYS_CONSOLE_40:
		strncpy(s, "CONSOLE_40", n);
		break;
	case SUBSYS_CONSOLE_41:
		strncpy(s, "CONSOLE_41", n);
		break;
	case SUBSYS_CONSOLE_42:
		strncpy(s, "CONSOLE_42", n);
		break;
	case SUBSYS_CONSOLE_43:
		strncpy(s, "CONSOLE_43", n);
		break;
	case SUBSYS_CONSOLE_44:
		strncpy(s, "CONSOLE_44", n);
		break;
	case SUBSYS_CONSOLE_45:
		strncpy(s, "CONSOLE_45", n);
		break;
	case SUBSYS_CONSOLE_46:
		strncpy(s, "CONSOLE_46", n);
		break;
	default:
		subsys_unknown_to_s(subsys, s, n);
		break;
	}
	s[n - 1] = '\0';
	return s;
}

#endif
