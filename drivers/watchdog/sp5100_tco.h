/*
 *	sp5100_tco:	TCO timer driver for sp5100 chipsets.
 *
 *	(c) Copyright 2009 Google Inc., All Rights Reserved.
 *
 *	TCO timer driver for sp5100 chipsets
 */

/*
 * Some address definitions for the Watchdog
 */

#define SP5100_WDT_MEM_MAP_SIZE		0x08
#define SP5100_WDT_CONTROL(base)	((base) + 0x00) /* Watchdog Control */
#define SP5100_WDT_COUNT(base)		((base) + 0x04) /* Watchdog Count */

#define SP5100_WDT_START_STOP_BIT	1
#define SP5100_WDT_TRIGGER_BIT		(1 << 7)

#define SP5100_PCI_WATCHDOG_MISC_REG	0x41
#define SP5100_PCI_WATCHDOG_DECODE_EN	(1 << 3)

#define SP5100_PM_IOPORTS_SIZE		0x02

/* These two IO registers are hardcoded and there doesn't seem to be a way to
 * read them from a register.
 */
#define SP5100_IO_PM_INDEX_REG		0xCD6
#define SP5100_IO_PM_DATA_REG		0xCD7

#define SP5100_PM_WATCHDOG_CONTROL	0x69
#define SP5100_PM_WATCHDOG_BASE0	0x6C
#define SP5100_PM_WATCHDOG_BASE1	0x6D
#define SP5100_PM_WATCHDOG_BASE2	0x6E
#define SP5100_PM_WATCHDOG_BASE3	0x6F

#define SP5100_PM_WATCHDOG_FIRED	(1 << 1)
#define SP5100_PM_WATCHDOG_ACTION_RESET	(1 << 2)

#define SP5100_PM_WATCHDOG_DISABLE	1
#define SP5100_PM_WATCHDOG_SECOND_RES	(3 << 1)
