/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/linkage.h>

SYM_FUNC_START(__hexagon_divsi3)
        {
                p0 = cmp.gt(r0,#-1)
                p1 = cmp.gt(r1,#-1)
                r3:2 = vabsw(r1:0)
        }
        {
                p3 = xor(p0,p1)
                r4 = sub(r2,r3)
                r6 = cl0(r2)
                p0 = cmp.gtu(r3,r2)
        }
        {
                r0 = mux(p3,#-1,#1)
                r7 = cl0(r3)
                p1 = cmp.gtu(r3,r4)
        }
        {
                r0 = mux(p0,#0,r0)
                p0 = or(p0,p1)
                if (p0.new) jumpr:nt r31
                r6 = sub(r7,r6)
        }
        {
                r7 = r6
                r5:4 = combine(#1,r3)
                r6 = add(#1,lsr(r6,#1))
                p0 = cmp.gtu(r6,#4)
        }
        {
                r5:4 = vaslw(r5:4,r7)
                if (!p0) r6 = #3
        }
        {
                loop0(1f,r6)
                r7:6 = vlsrw(r5:4,#1)
                r1:0 = #0
        }
        .falign
1:
        {
                r5:4 = vlsrw(r5:4,#2)
                if (!p0.new) r0 = add(r0,r5)
                if (!p0.new) r2 = sub(r2,r4)
                p0 = cmp.gtu(r4,r2)
        }
        {
                r7:6 = vlsrw(r7:6,#2)
                if (!p0.new) r0 = add(r0,r7)
                if (!p0.new) r2 = sub(r2,r6)
                p0 = cmp.gtu(r6,r2)
        }:endloop0
        {
                if (!p0) r0 = add(r0,r7)
        }
        {
                if (p3) r0 = sub(r1,r0)
                jumpr r31
        }
SYM_FUNC_END(__hexagon_divsi3)
