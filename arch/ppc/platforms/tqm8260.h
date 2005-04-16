/*
 * TQM8260 board specific definitions
 *
 * Copyright (c) 2001 Wolfgang Denk (wd@denx.de)
 */

#ifndef __TQM8260_PLATFORM
#define __TQM8260_PLATFORM

#include <linux/config.h>

#include <asm/ppcboot.h>

#define CPM_MAP_ADDR		((uint)0xFFF00000)
#define PHY_INTERRUPT		25

/* For our show_cpuinfo hooks. */
#define CPUINFO_VENDOR		"IN2 Systems"
#define CPUINFO_MACHINE		"TQM8260 PowerPC"

#define BOOTROM_RESTART_ADDR	((uint)0x40000104)

#endif	/* __TQM8260_PLATFORM */
