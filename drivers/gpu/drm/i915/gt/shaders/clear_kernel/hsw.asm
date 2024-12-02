// SPDX-License-Identifier: MIT
/*
 * Copyright © 2020 Intel Corporation
 */

/*
 * Kernel for PAVP buffer clear.
 *
 *	1. Clear all 64 GRF registers assigned to the kernel with designated value;
 *	2. Write 32x16 block of all "0" to render target buffer which indirectly clears
 *	   512 bytes of Render Cache.
 */

/* Store designated "clear GRF" value */
mov(1)          f0.1<1>UW       g1.2<0,1,0>UW                   { align1 1N };

/**
 * Curbe Format
 *
 * DW 1.0 - Block Offset to write Render Cache
 * DW 1.1 [15:0] - Clear Word
 * DW 1.2 - Delay iterations
 * DW 1.3 - Enable Instrumentation (only for debug)
 * DW 1.4 - Rsvd (intended for context ID)
 * DW 1.5 - [31:16]:SliceCount, [15:0]:SubSlicePerSliceCount
 * DW 1.6 - Rsvd MBZ (intended for Enable Wait on Total Thread Count)
 * DW 1.7 - Rsvd MBZ (inteded for Total Thread Count)
 *
 * Binding Table
 *
 * BTI 0: 2D Surface to help clear L3 (Render/Data Cache)
 * BTI 1: Wait/Instrumentation Buffer
 *  Size : (SliceCount * SubSliceCount  * 16 EUs/SubSlice) rows * (16 threads/EU) cols (Format R32_UINT)
 *         Expected to be initialized to 0 by driver/another kernel
 *  Layout:
 *          RowN: Histogram for EU-N: (SliceID*SubSlicePerSliceCount + SSID)*16 + EUID [assume max 16 EUs / SS]
 *          Col-k[DW-k]: Threads Executed on ThreadID-k for EU-N
 */
add(1)          g1.2<1>UD       g1.2<0,1,0>UD   0x00000001UD    { align1 1N }; /* Loop count to delay kernel: Init to (g1.2 + 1) */
cmp.z.f0.0(1)   null<1>UD       g1.3<0,1,0>UD   0x00000000UD    { align1 1N };
(+f0.0) jmpi(1) 352D                                            { align1 WE_all 1N };

/**
 * State Register has info on where this thread is running
 *	IVB: sr0.0 :: [15:13]: MBZ, 12: HSID (Half-Slice ID), [11:8]EUID, [2:0] ThreadSlotID
 *	HSW: sr0.0 :: 15: MBZ, [14:13]: SliceID, 12: HSID (Half-Slice ID), [11:8]EUID, [2:0] ThreadSlotID
 */
mov(8)          g3<1>UD         0x00000000UD                    { align1 1Q };
shr(1)          g3<1>D          sr0<0,1,0>D     12D             { align1 1N };
and(1)          g3<1>D          g3<0,1,0>D      1D              { align1 1N }; /* g3 has HSID */
shr(1)          g3.1<1>D        sr0<0,1,0>D     13D             { align1 1N };
and(1)          g3.1<1>D        g3.1<0,1,0>D    3D              { align1 1N }; /* g3.1 has sliceID */
mul(1)          g3.5<1>D        g3.1<0,1,0>D    g1.10<0,1,0>UW  { align1 1N };
add(1)          g3<1>D          g3<0,1,0>D      g3.5<0,1,0>D    { align1 1N }; /* g3 = sliceID * SubSlicePerSliceCount + HSID */
shr(1)          g3.2<1>D        sr0<0,1,0>D     8D              { align1 1N };
and(1)          g3.2<1>D        g3.2<0,1,0>D    15D             { align1 1N }; /* g3.2 = EUID */
mul(1)          g3.4<1>D        g3<0,1,0>D      16D             { align1 1N };
add(1)          g3.2<1>D        g3.2<0,1,0>D    g3.4<0,1,0>D    { align1 1N }; /* g3.2 now points to EU row number (Y-pixel = V address )  in instrumentation surf */

mov(8)          g5<1>UD         0x00000000UD                    { align1 1Q };
and(1)          g3.3<1>D        sr0<0,1,0>D     7D              { align1 1N };
mul(1)          g3.3<1>D        g3.3<0,1,0>D    4D              { align1 1N };

mov(8)          g4<1>UD         g0<8,8,1>UD                     { align1 1Q }; /* Initialize message header with g0 */
mov(1)          g4<1>UD         g3.3<0,1,0>UD                   { align1 1N }; /* Block offset */
mov(1)          g4.1<1>UD       g3.2<0,1,0>UD                   { align1 1N }; /* Block offset */
mov(1)          g4.2<1>UD       0x00000003UD                    { align1 1N }; /* Block size (1 row x 4 bytes) */
and(1)          g4.3<1>UD       g4.3<0,1,0>UW   0xffffffffUD    { align1 1N };

/* Media block read to fetch current value at specified location in instrumentation buffer */
sendc(8)        g5<1>UD         g4<8,8,1>F      0x02190001

                            render MsgDesc: media block read MsgCtrl = 0x0 Surface = 1 mlen 1 rlen 1 { align1 1Q };
add(1)          g5<1>D          g5<0,1,0>D      1D              { align1 1N };

/* Media block write for updated value at specified location in instrumentation buffer */
sendc(8)        g5<1>UD         g4<8,8,1>F      0x040a8001
                            render MsgDesc: media block write MsgCtrl = 0x0 Surface = 1 mlen 2 rlen 0 { align1 1Q };

/* Delay thread for specified parameter */
add.nz.f0.0(1)  g1.2<1>UD       g1.2<0,1,0>UD   -1D             { align1 1N };
(+f0.0) jmpi(1) -32D                                            { align1 WE_all 1N };

/* Store designated "clear GRF" value */
mov(1)          f0.1<1>UW       g1.2<0,1,0>UW                   { align1 1N };

/* Initialize looping parameters */
mov(1)          a0<1>D          0D                              { align1 1N }; /* Initialize a0.0:w=0 */
mov(1)          a0.4<1>W        127W                            { align1 1N }; /* Loop count. Each loop contains 16 GRF's */

/* Write 32x16 all "0" block */
mov(8)          g2<1>UD         g0<8,8,1>UD                     { align1 1Q };
mov(8)          g127<1>UD       g0<8,8,1>UD                     { align1 1Q };
mov(2)          g2<1>UD         g1<2,2,1>UW                     { align1 1N };
mov(1)          g2.2<1>UD       0x000f000fUD                    { align1 1N }; /* Block size (16x16) */
and(1)          g2.3<1>UD       g2.3<0,1,0>UW   0xffffffefUD    { align1 1N };
mov(16)         g3<1>UD         0x00000000UD                    { align1 1H };
mov(16)         g4<1>UD         0x00000000UD                    { align1 1H };
mov(16)         g5<1>UD         0x00000000UD                    { align1 1H };
mov(16)         g6<1>UD         0x00000000UD                    { align1 1H };
mov(16)         g7<1>UD         0x00000000UD                    { align1 1H };
mov(16)         g8<1>UD         0x00000000UD                    { align1 1H };
mov(16)         g9<1>UD         0x00000000UD                    { align1 1H };
mov(16)         g10<1>UD        0x00000000UD                    { align1 1H };
sendc(8)        null<1>UD       g2<8,8,1>F      0x120a8000
                            render MsgDesc: media block write MsgCtrl = 0x0 Surface = 0 mlen 9 rlen 0 { align1 1Q };
add(1)          g2<1>UD         g1<0,1,0>UW     0x0010UW        { align1 1N };
sendc(8)        null<1>UD       g2<8,8,1>F      0x120a8000
                            render MsgDesc: media block write MsgCtrl = 0x0 Surface = 0 mlen 9 rlen 0 { align1 1Q };

/* Now, clear all GRF registers */
add.nz.f0.0(1)  a0.4<1>W        a0.4<0,1,0>W    -1W             { align1 1N };
mov(16)         g[a0]<1>UW      f0.1<0,1,0>UW                   { align1 1H };
add(1)          a0<1>D          a0<0,1,0>D      32D             { align1 1N };
(+f0.0) jmpi(1) -64D                                            { align1 WE_all 1N };

/* Terminante the thread */
sendc(8)        null<1>UD       g127<8,8,1>F    0x82000010
                            thread_spawner MsgDesc: mlen 1 rlen 0           { align1 1Q EOT };
