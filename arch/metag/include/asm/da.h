/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Meta DA JTAG debugger control.
 *
 * Copyright 2012 Imagination Technologies Ltd.
 */

#ifndef _METAG_DA_H_
#define _METAG_DA_H_

#ifdef CONFIG_METAG_DA

#include <linux/init.h>
#include <linux/types.h>

extern bool _metag_da_present;

/**
 * metag_da_enabled() - Find whether a DA is currently enabled.
 *
 * Returns:	true if a DA was detected, false if not.
 */
static inline bool metag_da_enabled(void)
{
	return _metag_da_present;
}

/**
 * metag_da_probe() - Try and detect a connected DA.
 *
 * This is used at start up to detect whether a DA is active.
 *
 * Returns:	0 on detection, -err otherwise.
 */
int __init metag_da_probe(void);

#else /* !CONFIG_METAG_DA */

#define metag_da_enabled() false
#define metag_da_probe() do {} while (0)

#endif

#endif /* _METAG_DA_H_ */
