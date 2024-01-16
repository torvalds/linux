/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RapidIO architecture support
 *
 * Copyright 2005 MontaVista Software, Inc.
 * Matt Porter <mporter@kernel.crashing.org>
 */

#ifndef ASM_PPC_RIO_H
#define ASM_PPC_RIO_H

#ifdef CONFIG_FSL_RIO
extern int fsl_rio_mcheck_exception(struct pt_regs *);
#else
static inline int fsl_rio_mcheck_exception(struct pt_regs *regs) {return 0; }
#endif

#endif				/* ASM_PPC_RIO_H */
