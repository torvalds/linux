/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2021, The Linux Foundation. All rights reserved.
 */

#include <linux/linkage.h>

SYM_FUNC_START(__hexagon_memcpy_likely_aligned_min32bytes_mult8bytes)
        {
                p0 = bitsclr(r1,#7)
                p0 = bitsclr(r0,#7)
                if (p0.new) r5:4 = memd(r1)
                if (p0.new) r7:6 = memd(r1+#8)
        }
        {
                if (!p0) jump:nt .Lmemcpy_call
                if (p0) r9:8 = memd(r1+#16)
                if (p0) r11:10 = memd(r1+#24)
                p0 = cmp.gtu(r2,#64)
        }
        {
                if (p0) jump:nt .Lmemcpy_call
                if (!p0) memd(r0) = r5:4
                if (!p0) memd(r0+#8) = r7:6
                p0 = cmp.gtu(r2,#32)
        }
        {
                p1 = cmp.gtu(r2,#40)
                p2 = cmp.gtu(r2,#48)
                if (p0) r13:12 = memd(r1+#32)
                if (p1.new) r15:14 = memd(r1+#40)
        }
        {
                memd(r0+#16) = r9:8
                memd(r0+#24) = r11:10
        }
        {
                if (p0) memd(r0+#32) = r13:12
                if (p1) memd(r0+#40) = r15:14
                if (!p2) jumpr:t r31
        }
        {
                p0 = cmp.gtu(r2,#56)
                r5:4 = memd(r1+#48)
                if (p0.new) r7:6 = memd(r1+#56)
        }
        {
                memd(r0+#48) = r5:4
                if (p0) memd(r0+#56) = r7:6
                jumpr r31
        }

.Lmemcpy_call:
        jump memcpy

SYM_FUNC_END(__hexagon_memcpy_likely_aligned_min32bytes_mult8bytes)
