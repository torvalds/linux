/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * arch/powerpc/platforms/embedded6xx/usbgecko_udbg.h
 *
 * udbg serial input/output routines for the USB Gecko adapter.
 * Copyright (C) 2008-2009 The GameCube Linux Team
 * Copyright (C) 2008,2009 Albert Herranz
 */

#ifndef __USBGECKO_UDBG_H
#define __USBGECKO_UDBG_H

#ifdef CONFIG_USBGECKO_UDBG

extern void __init ug_udbg_init(void);

#else

static inline void __init ug_udbg_init(void)
{
}

#endif /* CONFIG_USBGECKO_UDBG */

void __init udbg_init_usbgecko(void);

#endif /* __USBGECKO_UDBG_H */
