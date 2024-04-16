/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

/* Numerology:
 * WXYZ
 * W: width in bytes
 * X: Load=0, Store=1
 * Y: Location 0=preamble,8=loop,9=epilog
 * Z: Location=0,handler=9
 */
	.text
	.global FUNCNAME
	.type FUNCNAME, @function
	.p2align 5
FUNCNAME:
	{
		p0 = cmp.gtu(bytes,#0)
		if (!p0.new) jump:nt .Ldone
		r3 = or(dst,src)
		r4 = xor(dst,src)
	}
	{
		p1 = cmp.gtu(bytes,#15)
		p0 = bitsclr(r3,#7)
		if (!p0.new) jump:nt .Loop_not_aligned_8
		src_dst_sav = combine(src,dst)
	}

	{
		loopcount = lsr(bytes,#3)
		if (!p1) jump .Lsmall
	}
	p3=sp1loop0(.Loop8,loopcount)
.Loop8:
8080:
8180:
	{
		if (p3) memd(dst++#8) = d_dbuf
		d_dbuf = memd(src++#8)
	}:endloop0
8190:
	{
		memd(dst++#8) = d_dbuf
		bytes -= asl(loopcount,#3)
		jump .Lsmall
	}

.Loop_not_aligned_8:
	{
		p0 = bitsclr(r4,#7)
		if (p0.new) jump:nt .Lalign
	}
	{
		p0 = bitsclr(r3,#3)
		if (!p0.new) jump:nt .Loop_not_aligned_4
		p1 = cmp.gtu(bytes,#7)
	}

	{
		if (!p1) jump .Lsmall
		loopcount = lsr(bytes,#2)
	}
	p3=sp1loop0(.Loop4,loopcount)
.Loop4:
4080:
4180:
	{
		if (p3) memw(dst++#4) = w_dbuf
		w_dbuf = memw(src++#4)
	}:endloop0
4190:
	{
		memw(dst++#4) = w_dbuf
		bytes -= asl(loopcount,#2)
		jump .Lsmall
	}

.Loop_not_aligned_4:
	{
		p0 = bitsclr(r3,#1)
		if (!p0.new) jump:nt .Loop_not_aligned
		p1 = cmp.gtu(bytes,#3)
	}

	{
		if (!p1) jump .Lsmall
		loopcount = lsr(bytes,#1)
	}
	p3=sp1loop0(.Loop2,loopcount)
.Loop2:
2080:
2180:
	{
		if (p3) memh(dst++#2) = w_dbuf
		w_dbuf = memuh(src++#2)
	}:endloop0
2190:
	{
		memh(dst++#2) = w_dbuf
		bytes -= asl(loopcount,#1)
		jump .Lsmall
	}

.Loop_not_aligned: /* Works for as small as one byte */
	p3=sp1loop0(.Loop1,bytes)
.Loop1:
1080:
1180:
	{
		if (p3) memb(dst++#1) = w_dbuf
		w_dbuf = memub(src++#1)
	}:endloop0
	/* Done */
1190:
	{
		memb(dst) = w_dbuf
		jumpr r31
		r0 = #0
	}

.Lsmall:
	{
		p0 = cmp.gtu(bytes,#0)
		if (p0.new) jump:nt .Loop_not_aligned
	}
.Ldone:
	{
		r0 = #0
		jumpr r31
	}
	.falign
.Lalign:
1000:
	{
		if (p0.new) w_dbuf = memub(src)
		p0 = tstbit(src,#0)
		if (!p1) jump .Lsmall
	}
1100:
	{
		if (p0) memb(dst++#1) = w_dbuf
		if (p0) bytes = add(bytes,#-1)
		if (p0) src = add(src,#1)
	}
2000:
	{
		if (p0.new) w_dbuf = memuh(src)
		p0 = tstbit(src,#1)
		if (!p1) jump .Lsmall
	}
2100:
	{
		if (p0) memh(dst++#2) = w_dbuf
		if (p0) bytes = add(bytes,#-2)
		if (p0) src = add(src,#2)
	}
4000:
	{
		if (p0.new) w_dbuf = memw(src)
		p0 = tstbit(src,#2)
		if (!p1) jump .Lsmall
	}
4100:
	{
		if (p0) memw(dst++#4) = w_dbuf
		if (p0) bytes = add(bytes,#-4)
		if (p0) src = add(src,#4)
		jump FUNCNAME
	}
	.size FUNCNAME,.-FUNCNAME
