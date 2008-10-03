/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright 2006 (c) Andriy Skulysh <askulysh@gmail.com>
 *
 */
#ifndef __ASM_SH_PM_H
#define __ASM_SH_PM_H

extern u8 wakeup_start;
extern u8 wakeup_end;

void pm_enter(void);

#endif
