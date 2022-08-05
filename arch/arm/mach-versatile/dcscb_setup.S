/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Created by:  Dave Martin, 2012-06-22
 * Copyright:   (C) 2012-2013  Linaro Limited
 */

#include <linux/linkage.h>


ENTRY(dcscb_power_up_setup)

	cmp	r0, #0			@ check affinity level
	beq	2f

/*
 * Enable cluster-level coherency, in preparation for turning on the MMU.
 * The ACTLR SMP bit does not need to be set here, because cpu_resume()
 * already restores that.
 *
 * A15/A7 may not require explicit L2 invalidation on reset, dependent
 * on hardware integration decisions.
 * For now, this code assumes that L2 is either already invalidated,
 * or invalidation is not required.
 */

	b	cci_enable_port_for_self

2:	@ Implementation-specific local CPU setup operations should go here,
	@ if any.  In this case, there is nothing to do.

	bx	lr

ENDPROC(dcscb_power_up_setup)
