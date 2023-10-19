/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/linkage.h>

SYM_FUNC_START(__hexagon_umodsi3)
        {
                r2 = cl0(r0)
                r3 = cl0(r1)
                p0 = cmp.gtu(r1,r0)
        }
        {
                r2 = sub(r3,r2)
                if (p0) jumpr r31
        }
        {
                loop0(1f,r2)
                p1 = cmp.eq(r2,#0)
                r2 = lsl(r1,r2)
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
                jumpr r31
        }
SYM_FUNC_END(__hexagon_umodsi3)
