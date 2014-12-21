/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012-2013 Cavium Inc., All Rights Reserved.
 */
#ifndef __LINUX_OCTEON_CRYPTO_H
#define __LINUX_OCTEON_CRYPTO_H

#include <linux/sched.h>

extern unsigned long octeon_crypto_enable(struct octeon_cop2_state *state);
extern void octeon_crypto_disable(struct octeon_cop2_state *state,
				  unsigned long flags);

#endif /* __LINUX_OCTEON_CRYPTO_H */
