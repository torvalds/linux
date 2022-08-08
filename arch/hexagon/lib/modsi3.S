/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/linkage.h>

SYM_FUNC_START(__hexagon_modsi3)
        {
                p2 = cmp.ge(r0,#0)
                r2 = abs(r0)
                r1 = abs(r1)
        }
        {
                r3 = cl0(r2)
                r4 = cl0(r1)
                p0 = cmp.gtu(r1,r2)
        }
        {
                r3 = sub(r4,r3)
                if (p0) jumpr r31
        }
        {
                p1 = cmp.eq(r3,#0)
                loop0(1f,r3)
                r0 = r2
                r2 = lsl(r1,r3)
        }
        .falign
1:
        {
                p0 = cmp.gtu(r2,r0)
                if (!p0.new) r0 = sub(r0,r2)
                r2 = lsr(r2,#1)
                if (p1) r1 = #0
        }:endloop0
        {
                p0 = cmp.gtu(r2,r0)
                if (!p0.new) r0 = sub(r0,r1)
                if (p2) jumpr r31
        }
        {
                r0 = neg(r0)
                jumpr r31
        }
SYM_FUNC_END(__hexagon_modsi3)
