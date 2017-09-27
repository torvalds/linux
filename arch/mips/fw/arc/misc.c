/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Miscellaneous ARCS PROM routines.
 *
 * Copyright (C) 1996 David S. Miller (davem@davemloft.net)
 * Copyright (C) 1999 Ralf Baechle (ralf@gnu.org)
 * Copyright (C) 1999 Silicon Graphics, Inc.
 */
#include <linux/compiler.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/irqflags.h>

#include <asm/bcache.h>

#include <asm/fw/arc/types.h>
#include <asm/sgialib.h>
#include <asm/bootinfo.h>

VOID __noreturn
ArcHalt(VOID)
{
	bc_disable();
	local_irq_disable();
	ARC_CALL0(halt);

	unreachable();
}

VOID __noreturn
ArcPowerDown(VOID)
{
	bc_disable();
	local_irq_disable();
	ARC_CALL0(pdown);

	unreachable();
}

/* XXX is this a soft reset basically? XXX */
VOID __noreturn
ArcRestart(VOID)
{
	bc_disable();
	local_irq_disable();
	ARC_CALL0(restart);

	unreachable();
}

VOID __noreturn
ArcReboot(VOID)
{
	bc_disable();
	local_irq_disable();
	ARC_CALL0(reboot);

	unreachable();
}

VOID __noreturn
ArcEnterInteractiveMode(VOID)
{
	bc_disable();
	local_irq_disable();
	ARC_CALL0(imode);

	unreachable();
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
