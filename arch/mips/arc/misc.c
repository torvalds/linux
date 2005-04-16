/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Miscellaneous ARCS PROM routines.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>

#include <asm/bcache.h>

#include <asm/arc/types.h>
#include <asm/sgialib.h>
#include <asm/bootinfo.h>
#include <asm/system.h>

extern void *sgiwd93_host;
extern void reset_wd33c93(void *instance);

VOID
ArcHalt(VOID)
{
	bc_disable();
	local_irq_disable();
#ifdef CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	ARC_CALL0(halt);
never:	goto never;
}

VOID
ArcPowerDown(VOID)
{
	bc_disable();
	local_irq_disable();
#ifdef CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	ARC_CALL0(pdown);
never:	goto never;
}

/* XXX is this a soft reset basically? XXX */
VOID
ArcRestart(VOID)
{
	bc_disable();
	local_irq_disable();
#ifdef CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	ARC_CALL0(restart);
never:	goto never;
}

VOID
ArcReboot(VOID)
{
	bc_disable();
	local_irq_disable();
#ifdef CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	ARC_CALL0(reboot);
never:	goto never;
}

VOID
ArcEnterInteractiveMode(VOID)
{
	bc_disable();
	local_irq_disable();
#ifdef CONFIG_SCSI_SGIWD93
	reset_wd33c93(sgiwd93_host);
#endif
	ARC_CALL0(imode);
never:	goto never;
}

LONG
ArcSaveConfiguration(VOID)
{
	return ARC_CALL0(cfg_save);
}

struct linux_sysid *
ArcGetSystemId(VOID)
{
	return (struct linux_sysid *) ARC_CALL0(get_sysid);
}

VOID __init
ArcFlushAllCaches(VOID)
{
	ARC_CALL0(cache_flush);
}

DISPLAY_STATUS * __init ArcGetDisplayStatus(ULONG FileID)
{
	return (DISPLAY_STATUS *) ARC_CALL1(GetDisplayStatus, FileID);
}
