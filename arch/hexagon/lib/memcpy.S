/*
 * Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

/*
 * Description
 *
 *   library function for memcpy where length bytes are copied from
 *   ptr_in to ptr_out. ptr_out is returned unchanged.
 *   Allows any combination of alignment on input and output pointers
 *   and length from 0 to 2^32-1
 *
 * Restrictions
 *   The arrays should not overlap, the program will produce undefined output
 *   if they do.
 *   For blocks less than 16 bytes a byte by byte copy is performed. For
 *   8byte alignments, and length multiples, a dword copy is performed up to
 *   96bytes
 * History
 *
 *   DJH  5/15/09 Initial version 1.0
 *   DJH  6/ 1/09 Version 1.1 modified ABI to inlcude R16-R19
 *   DJH  7/12/09 Version 1.2 optimized codesize down to 760 was 840
 *   DJH 10/14/09 Version 1.3 added special loop for aligned case, was
 *                            overreading bloated codesize back up to 892
 *   DJH  4/20/10 Version 1.4 fixed Ldword_loop_epilog loop to prevent loads
 *                            occuring if only 1 left outstanding, fixes bug
 *                            # 3888, corrected for all alignments. Peeled off
 *                            1 32byte chunk from kernel loop and extended 8byte
 *                            loop at end to solve all combinations and prevent
 *                            over read.  Fixed Ldword_loop_prolog to prevent
 *                            overread for blocks less than 48bytes. Reduced
 *                            codesize to 752 bytes
 *   DJH  4/21/10 version 1.5 1.4 fix broke code for input block ends not
 *                            aligned to dword boundaries,underwriting by 1
 *                            byte, added detection for this and fixed. A
 *                            little bloat.
 *   DJH  4/23/10 version 1.6 corrected stack error, R20 was not being restored
 *                            always, fixed the error of R20 being modified
 *                            before it was being saved
 * Natural c model
 * ===============
 * void * memcpy(char * ptr_out, char * ptr_in, int length) {
 *   int i;
 *   if(length) for(i=0; i < length; i++) { ptr_out[i] = ptr_in[i]; }
 *   return(ptr_out);
 * }
 *
 * Optimized memcpy function
 * =========================
 * void * memcpy(char * ptr_out, char * ptr_in, int len) {
 *   int i, prolog, kernel, epilog, mask;
 *   u8 offset;
 *   s64 data0, dataF8, data70;
 *
 *   s64 * ptr8_in;
 *   s64 * ptr8_out;
 *   s32 * ptr4;
 *   s16 * ptr2;
 *
 *   offset = ((int) ptr_in) & 7;
 *   ptr8_in = (s64 *) &ptr_in[-offset];   //read in the aligned pointers
 *
 *   data70 = *ptr8_in++;
 *   dataF8 = *ptr8_in++;
 *
 *   data0 = HEXAGON_P_valignb_PPp(dataF8, data70, offset);
 *
 *   prolog = 32 - ((int) ptr_out);
 *   mask  = 0x7fffffff >> HEXAGON_R_cl0_R(len);
 *   prolog = prolog & mask;
 *   kernel = len - prolog;
 *   epilog = kernel & 0x1F;
 *   kernel = kernel>>5;
 *
 *   if (prolog & 1) { ptr_out[0] = (u8) data0; data0 >>= 8; ptr_out += 1;}
 *   ptr2 = (s16 *) &ptr_out[0];
 *   if (prolog & 2) { ptr2[0] = (u16) data0;  data0 >>= 16; ptr_out += 2;}
 *   ptr4 = (s32 *) &ptr_out[0];
 *   if (prolog & 4) { ptr4[0] = (u32) data0;  data0 >>= 32; ptr_out += 4;}
 *
 *   offset = offset + (prolog & 7);
 *   if (offset >= 8) {
 *     data70 = dataF8;
 *     dataF8 = *ptr8_in++;
 *   }
 *   offset = offset & 0x7;
 *
 *   prolog = prolog >> 3;
 *   if (prolog) for (i=0; i < prolog; i++) {
 *       data0 = HEXAGON_P_valignb_PPp(dataF8, data70, offset);
 *       ptr8_out = (s64 *) &ptr_out[0]; *ptr8_out = data0; ptr_out += 8;
 *       data70 = dataF8;
 *       dataF8 = *ptr8_in++;
 *   }
 *   if(kernel) { kernel -= 1; epilog += 32; }
 *   if(kernel) for(i=0; i < kernel; i++) {
 *       data0 = HEXAGON_P_valignb_PPp(dataF8, data70, offset);
 *       ptr8_out = (s64 *) &ptr_out[0]; *ptr8_out = data0; ptr_out += 8;
 *       data70 = *ptr8_in++;
 *
 *       data0 = HEXAGON_P_valignb_PPp(data70, dataF8, offset);
 *       ptr8_out = (s64 *) &ptr_out[0]; *ptr8_out = data0; ptr_out += 8;
 *       dataF8 = *ptr8_in++;
 *
 *       data0 = HEXAGON_P_valignb_PPp(dataF8, data70, offset);
 *       ptr8_out = (s64 *) &ptr_out[0]; *ptr8_out = data0; ptr_out += 8;
 *       data70 = *ptr8_in++;
 *
 *       data0 = HEXAGON_P_valignb_PPp(data70, dataF8, offset);
 *       ptr8_out = (s64 *) &ptr_out[0]; *ptr8_out = data0; ptr_out += 8;
 *       dataF8 = *ptr8_in++;
 *   }
 *   epilogdws = epilog >> 3;
 *   if (epilogdws) for (i=0; i < epilogdws; i++) {
 *       data0 = HEXAGON_P_valignb_PPp(dataF8, data70, offset);
 *       ptr8_out = (s64 *) &ptr_out[0]; *ptr8_out = data0; ptr_out += 8;
 *       data70 = dataF8;
 *       dataF8 = *ptr8_in++;
 *   }
 *   data0 = HEXAGON_P_valignb_PPp(dataF8, data70, offset);
 *
 *   ptr4 = (s32 *) &ptr_out[0];
 *   if (epilog & 4) { ptr4[0] = (u32) data0; data0 >>= 32; ptr_out += 4;}
 *   ptr2 = (s16 *) &ptr_out[0];
 *   if (epilog & 2) { ptr2[0] = (u16) data0; data0 >>= 16; ptr_out += 2;}
 *   if (epilog & 1) { *ptr_out++ = (u8) data0; }
 *
 *   return(ptr_out - length);
 * }
 *
 * Codesize : 784 bytes
 */


#define ptr_out		R0	/*  destination  pounter  */
#define ptr_in		R1	/*  source pointer  */
#define len		R2	/*  length of copy in bytes  */

#define data70		R13:12	/*  lo 8 bytes of non-aligned transfer  */
#define dataF8		R11:10	/*  hi 8 bytes of non-aligned transfer  */
#define ldata0		R7:6	/*  even 8 bytes chunks  */
#define ldata1		R25:24	/*  odd 8 bytes chunks  */
#define data1		R7	/*  lower 8 bytes of ldata1  */
#define data0		R6	/*  lower 8 bytes of ldata0  */

#define ifbyte		p0	/*  if transfer has bytes in epilog/prolog  */
#define ifhword		p0	/*  if transfer has shorts in epilog/prolog  */
#define ifword		p0	/*  if transfer has words in epilog/prolog  */
#define noprolog	p0	/*  no prolog, xfer starts at 32byte  */
#define nokernel	p1	/*  no 32byte multiple block in the transfer  */
#define noepilog	p0	/*  no epilog, xfer ends on 32byte boundary  */
#define align		p2	/*  alignment of input rel to 8byte boundary  */
#define kernel1		p0	/*  kernel count == 1  */

#define dalign		R25	/*  rel alignment of input to output data  */
#define star3		R16	/*  number bytes in prolog - dwords  */
#define rest		R8	/*  length - prolog bytes  */
#define back		R7	/*  nr bytes > dword boundary in src block  */
#define epilog		R3	/*  bytes in epilog  */
#define inc		R15:14	/*  inc kernel by -1 and defetch ptr by 32  */
#define kernel		R4	/*  number of 32byte chunks in kernel  */
#define ptr_in_p_128	R5	/*  pointer for prefetch of input data  */
#define mask		R8	/*  mask used to determine prolog size  */
#define shift		R8	/*  used to work a shifter to extract bytes  */
#define shift2		R5	/*  in epilog to workshifter to extract bytes */
#define prolog		R15	/*  bytes in  prolog  */
#define epilogdws	R15	/*  number dwords in epilog  */
#define shiftb		R14	/*  used to extract bytes  */
#define offset		R9	/*  same as align in reg  */
#define ptr_out_p_32	R17	/*  pointer to output dczero  */
#define align888	R14	/*  if simple dword loop can be used  */
#define len8		R9	/*  number of dwords in length  */
#define over		R20	/*  nr of bytes > last inp buf dword boundary */

#define ptr_in_p_128kernel	R5:4	/*  packed fetch pointer & kernel cnt */

	.section .text
	.p2align 4
        .global memcpy
        .type memcpy, @function
memcpy:
{
	p2 = cmp.eq(len, #0);		/*  =0 */
	align888 = or(ptr_in, ptr_out);	/*  %8 < 97 */
	p0 = cmp.gtu(len, #23);		/*  %1, <24 */
	p1 = cmp.eq(ptr_in, ptr_out);	/*  attempt to overwrite self */
}
{
	p1 = or(p2, p1);
	p3 = cmp.gtu(len, #95);		/*  %8 < 97 */
	align888 = or(align888, len);	/*  %8 < 97 */
	len8 = lsr(len, #3);		/*  %8 < 97 */
}
{
	dcfetch(ptr_in);		/*  zero/ptrin=ptrout causes fetch */
	p2 = bitsclr(align888, #7);	/*  %8 < 97  */
	if(p1) jumpr r31;		/*  =0  */
}
{
	p2 = and(p2,!p3);			/*  %8 < 97  */
	if (p2.new) len = add(len, #-8);	/*  %8 < 97  */
	if (p2.new) jump:NT .Ldwordaligned; 	/*  %8 < 97  */
}
{
	if(!p0) jump .Lbytes23orless;	/*  %1, <24  */
	mask.l = #LO(0x7fffffff);
	/*  all bytes before line multiples of data  */
	prolog = sub(#0, ptr_out);
}
{
	/*  save r31 on stack, decrement sp by 16  */
	allocframe(#24);
	mask.h = #HI(0x7fffffff);
	ptr_in_p_128 = add(ptr_in, #32);
	back = cl0(len);
}
{
	memd(sp+#0) = R17:16;		/*  save r16,r17 on stack6  */
	r31.l = #LO(.Lmemcpy_return);	/*  set up final return pointer  */
	prolog &= lsr(mask, back);
	offset = and(ptr_in, #7);
}
{
	memd(sp+#8) = R25:24;		/*  save r25,r24 on stack  */
	dalign = sub(ptr_out, ptr_in);
	r31.h = #HI(.Lmemcpy_return);	/*  set up final return pointer  */
}
{
	/*  see if there if input buffer end if aligned  */
	over = add(len, ptr_in);
	back = add(len, offset);
	memd(sp+#16) = R21:20;		/*  save r20,r21 on stack  */
}
{
	noprolog = bitsclr(prolog, #7);
	prolog = and(prolog, #31);
	dcfetch(ptr_in_p_128);
	ptr_in_p_128 = add(ptr_in_p_128, #32);
}
{
	kernel = sub(len, prolog);
	shift = asl(prolog, #3);
	star3 = and(prolog, #7);
	ptr_in = and(ptr_in, #-8);
}
{
	prolog = lsr(prolog, #3);
	epilog = and(kernel, #31);
	ptr_out_p_32 = add(ptr_out, prolog);
	over = and(over, #7);
}
{
	p3 = cmp.gtu(back, #8);
	kernel = lsr(kernel, #5);
	dcfetch(ptr_in_p_128);
	ptr_in_p_128 = add(ptr_in_p_128, #32);
}
{
	p1 = cmp.eq(prolog, #0);
	if(!p1.new) prolog = add(prolog, #1);
	dcfetch(ptr_in_p_128);	/*  reserve the line 64bytes on  */
	ptr_in_p_128 = add(ptr_in_p_128, #32);
}
{
	nokernel = cmp.eq(kernel,#0);
	dcfetch(ptr_in_p_128);	/* reserve the line 64bytes on  */
	ptr_in_p_128 = add(ptr_in_p_128, #32);
	shiftb = and(shift, #8);
}
{
	dcfetch(ptr_in_p_128);		/*  reserve the line 64bytes on  */
	ptr_in_p_128 = add(ptr_in_p_128, #32);
	if(nokernel) jump .Lskip64;
	p2 = cmp.eq(kernel, #1);	/*  skip ovr if kernel == 0  */
}
{
	dczeroa(ptr_out_p_32);
	/*  don't advance pointer  */
	if(!p2) ptr_out_p_32 = add(ptr_out_p_32, #32);
}
{
	dalign = and(dalign, #31);
	dczeroa(ptr_out_p_32);
}
.Lskip64:
{
	data70 = memd(ptr_in++#16);
	if(p3) dataF8 = memd(ptr_in+#8);
	if(noprolog) jump .Lnoprolog32;
	align = offset;
}
/*  upto initial 7 bytes  */
{
	ldata0 = valignb(dataF8, data70, align);
	ifbyte = tstbit(shift,#3);
	offset = add(offset, star3);
}
{
	if(ifbyte) memb(ptr_out++#1) = data0;
	ldata0 = lsr(ldata0, shiftb);
	shiftb = and(shift, #16);
	ifhword = tstbit(shift,#4);
}
{
	if(ifhword) memh(ptr_out++#2) = data0;
	ldata0 = lsr(ldata0, shiftb);
	ifword = tstbit(shift,#5);
	p2 = cmp.gtu(offset, #7);
}
{
	if(ifword) memw(ptr_out++#4) = data0;
	if(p2) data70 = dataF8;
	if(p2) dataF8 = memd(ptr_in++#8);	/*  another 8 bytes  */
	align = offset;
}
.Lnoprolog32:
{
	p3 = sp1loop0(.Ldword_loop_prolog, prolog)
	rest = sub(len, star3);	/*  whats left after the loop  */
	p0 = cmp.gt(over, #0);
}
	if(p0) rest = add(rest, #16);
.Ldword_loop_prolog:
{
	if(p3) memd(ptr_out++#8) = ldata0;
	ldata0 = valignb(dataF8, data70, align);
	p0 = cmp.gt(rest, #16);
}
{
	data70 = dataF8;
	if(p0) dataF8 = memd(ptr_in++#8);
	rest = add(rest, #-8);
}:endloop0
.Lkernel:
{
	/*  kernel is at least 32bytes  */
	p3 = cmp.gtu(kernel, #0);
	/*  last itn. remove edge effects  */
	if(p3.new) kernel = add(kernel, #-1);
	/*  dealt with in last dword loop  */
	if(p3.new) epilog = add(epilog, #32);
}
{
	nokernel = cmp.eq(kernel, #0);		/*  after adjustment, recheck */
	if(nokernel.new) jump:NT .Lepilog;	/*  likely not taken  */
	inc = combine(#32, #-1);
	p3 = cmp.gtu(dalign, #24);
}
{
	if(p3) jump .Lodd_alignment;
}
{
	loop0(.Loword_loop_25to31, kernel);
	kernel1 = cmp.gtu(kernel, #1);
	rest = kernel;
}
	.falign
.Loword_loop_25to31:
{
	dcfetch(ptr_in_p_128);	/*  prefetch 4 lines ahead  */
	if(kernel1) ptr_out_p_32 = add(ptr_out_p_32, #32);
}
{
	dczeroa(ptr_out_p_32);	/*  reserve the next 32bytes in cache  */
	p3 = cmp.eq(kernel, rest);
}
{
	/*  kernel -= 1  */
	ptr_in_p_128kernel = vaddw(ptr_in_p_128kernel, inc);
	/*  kill write on first iteration  */
	if(!p3) memd(ptr_out++#8) = ldata1;
	ldata1 = valignb(dataF8, data70, align);
	data70 = memd(ptr_in++#8);
}
{
	memd(ptr_out++#8) = ldata0;
	ldata0 = valignb(data70, dataF8, align);
	dataF8 = memd(ptr_in++#8);
}
{
	memd(ptr_out++#8) = ldata1;
	ldata1 = valignb(dataF8, data70, align);
	data70 = memd(ptr_in++#8);
}
{
	memd(ptr_out++#8) = ldata0;
	ldata0 = valignb(data70, dataF8, align);
	dataF8 = memd(ptr_in++#8);
	kernel1 = cmp.gtu(kernel, #1);
}:endloop0
{
	memd(ptr_out++#8) = ldata1;
	jump .Lepilog;
}
.Lodd_alignment:
{
	loop0(.Loword_loop_00to24, kernel);
	kernel1 = cmp.gtu(kernel, #1);
	rest = add(kernel, #-1);
}
	.falign
.Loword_loop_00to24:
{
	dcfetch(ptr_in_p_128);	/*  prefetch 4 lines ahead  */
	ptr_in_p_128kernel = vaddw(ptr_in_p_128kernel, inc);
	if(kernel1) ptr_out_p_32 = add(ptr_out_p_32, #32);
}
{
	dczeroa(ptr_out_p_32);	/*  reserve the next 32bytes in cache  */
}
{
	memd(ptr_out++#8) = ldata0;
	ldata0 = valignb(dataF8, data70, align);
	data70 = memd(ptr_in++#8);
}
{
	memd(ptr_out++#8) = ldata0;
	ldata0 = valignb(data70, dataF8, align);
	dataF8 = memd(ptr_in++#8);
}
{
	memd(ptr_out++#8) = ldata0;
	ldata0 = valignb(dataF8, data70, align);
	data70 = memd(ptr_in++#8);
}
{
	memd(ptr_out++#8) = ldata0;
	ldata0 = valignb(data70, dataF8, align);
	dataF8 = memd(ptr_in++#8);
	kernel1 = cmp.gtu(kernel, #1);
}:endloop0
.Lepilog:
{
	noepilog = cmp.eq(epilog,#0);
	epilogdws = lsr(epilog, #3);
	kernel = and(epilog, #7);
}
{
	if(noepilog) jumpr r31;
	if(noepilog) ptr_out = sub(ptr_out, len);
	p3 = cmp.eq(epilogdws, #0);
	shift2 = asl(epilog, #3);
}
{
	shiftb = and(shift2, #32);
	ifword = tstbit(epilog,#2);
	if(p3) jump .Lepilog60;
	if(!p3) epilog = add(epilog, #-16);
}
{
	loop0(.Ldword_loop_epilog, epilogdws);
	/*  stop criteria is lsbs unless = 0 then its 8  */
	p3 = cmp.eq(kernel, #0);
	if(p3.new) kernel= #8;
	p1 = cmp.gt(over, #0);
}
	/*  if not aligned to end of buffer execute 1 more iteration  */
	if(p1) kernel= #0;
.Ldword_loop_epilog:
{
	memd(ptr_out++#8) = ldata0;
	ldata0 = valignb(dataF8, data70, align);
	p3 = cmp.gt(epilog, kernel);
}
{
	data70 = dataF8;
	if(p3) dataF8 = memd(ptr_in++#8);
	epilog = add(epilog, #-8);
}:endloop0
/* copy last 7 bytes */
.Lepilog60:
{
	if(ifword) memw(ptr_out++#4) = data0;
	ldata0 = lsr(ldata0, shiftb);
	ifhword = tstbit(epilog,#1);
	shiftb = and(shift2, #16);
}
{
	if(ifhword) memh(ptr_out++#2) = data0;
	ldata0 = lsr(ldata0, shiftb);
	ifbyte = tstbit(epilog,#0);
	if(ifbyte.new) len = add(len, #-1);
}
{
	if(ifbyte) memb(ptr_out) = data0;
	ptr_out = sub(ptr_out, len);	/*  return dest pointer  */
        jumpr r31;
}
/*  do byte copy for small n  */
.Lbytes23orless:
{
	p3 = sp1loop0(.Lbyte_copy, len);
	len = add(len, #-1);
}
.Lbyte_copy:
{
	data0 = memb(ptr_in++#1);
	if(p3) memb(ptr_out++#1) = data0;
}:endloop0
{
	memb(ptr_out) = data0;
	ptr_out = sub(ptr_out, len);
	jumpr r31;
}
/*  do dword copies for aligned in, out and length  */
.Ldwordaligned:
{
	p3 = sp1loop0(.Ldword_copy, len8);
}
.Ldword_copy:
{
	if(p3) memd(ptr_out++#8) = ldata0;
	ldata0 = memd(ptr_in++#8);
}:endloop0
{
	memd(ptr_out) = ldata0;
	ptr_out = sub(ptr_out, len);
	jumpr r31;	/*  return to function caller  */
}
.Lmemcpy_return:
	r21:20 = memd(sp+#16);	/*  restore r20+r21  */
{
	r25:24 = memd(sp+#8);	/*  restore r24+r25  */
	r17:16 = memd(sp+#0);	/*  restore r16+r17  */
}
	deallocframe;	/*  restore r31 and incrment stack by 16  */
	jumpr r31
