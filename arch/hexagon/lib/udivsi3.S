/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/linkage.h>

SYM_FUNC_START(__hexagon_udivsi3)
        {
                r2 = cl0(r0)
                r3 = cl0(r1)
                r5:4 = combine(#1,#0)
                p0 = cmp.gtu(r1,r0)
        }
        {
                r6 = sub(r3,r2)
                r4 = r1
                r1:0 = combine(r0,r4)
                if (p0) jumpr r31
        }
        {
                r3:2 = vlslw(r5:4,r6)
                loop0(1f,r6)
        }
        .falign
1:
        {
                p0 = cmp.gtu(r2,r1)
                if (!p0.new) r1 = sub(r1,r2)
                if (!p0.new) r0 = add(r0,r3)
                r3:2 = vlsrw(r3:2,#1)
        }:endloop0
        {
                p0 = cmp.gtu(r2,r1)
                if (!p0.new) r0 = add(r0,r3)
                jumpr r31
        }
SYM_FUNC_END(__hexagon_udivsi3)
