/*
 * memtester version 4
 *
 * Very simple but very effective user-space memory tester.
 * Originally by Simon Kirby <sim@stormix.com> <sim@neato.org>
 * Version 2 by Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Version 3 not publicly released.
 * Version 4 rewrite:
 * Copyright (C) 2007-2009 Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Licensed under the terms of the GNU General Public License version 2 (only).
 * See the file COPYING for details.
 *
 */


#include <linux/kernel.h>
#include <linux/slab.h>

#include "tests.h"


#define EXIT_FAIL_NONSTARTER    0x01
#define EXIT_FAIL_ADDRESSLINES  0x02
#define EXIT_FAIL_OTHERTEST     0x04

//#pragma arm section rwdata="DDR"
useful_data_t useful_data={(2*1024*1024), 133, 333};
//#pragma arm section

struct test tests[17]
= {
    #ifdef TEST_RANDOM
    { "Random Value", test_random_value },
    #endif
    #ifdef TEST_XOR
    { "Compare XOR", test_xor_comparison },
    #endif
    #ifdef TEST_SUB
    { "Compare SUB", test_sub_comparison },
    #endif
    #ifdef TEST_MUL
    { "Compare MUL", test_mul_comparison },
    #endif
    #ifdef TEST_DIV
    { "Compare DIV",test_div_comparison },
    #endif
    #ifdef TEST_OR
    { "Compare OR", test_or_comparison },
    #endif
    #ifdef TEST_AND
    { "Compare AND", test_and_comparison },
    #endif
    #ifdef TEST_SEQINC
    { "Sequential Increment", test_seqinc_comparison },
    #endif
    #ifdef TEST_SOLID_BIT
    { "Solid Bits", test_solidbits_comparison },
    #endif
    #ifdef TEST_BLOCK_SEQ
    { "Block Sequential", test_blockseq_comparison },
    #endif
    #ifdef TEST_CHECK_BOARD
    { "Checkerboard", test_checkerboard_comparison },
    #endif
    #ifdef TEST_BIT_SPREAD
    { "Bit Spread", test_bitspread_comparison },
    #endif
    #ifdef TEST_BIT_FLIP
    { "Bit Flip", test_bitflip_comparison },
    #endif
    #ifdef TEST_ONE
    { "Walking Ones", test_walkbits1_comparison },
    #endif
    #ifdef TEST_ZERO
    { "Walking Zeroes", test_walkbits0_comparison },
    #endif
    { NULL, NULL }
};

int exit_code = 0;

int memtester(void) {
    ul loops, loop, i;
    size_t pagesize, wantraw, wantmb, wantbytes, wantbytes_orig, bufsize,
         halflen, count;
    ptrdiff_t pagesizemask;
    void volatile *buf, *aligned;
    ulv *bufa, *bufb;
    int memshift;
    ul cap;

    

    print("Copyright (C) 2009 Charles Cazabon.\n");
    print("Licensed under the GNU General Public License version 2 (only).\n");
    print("\n");
    pagesize = 1024;
    pagesizemask = (ptrdiff_t) ~(pagesize - 1);
    print("pagesizemask is 0x");
    print_Hex(pagesizemask);
    print("\n");

    if(useful_data.testCap == 0xFFFFFFFF)
    {
        cap = 0x800000 << (((pDDR_Reg->DCR >> 4) & 0x7) 
                             + ((((pDDR_Reg->DCR >> 7) & 0x7)+1) >> ((pDDR_Reg->DCR >> 2) & 0x3)) 
                             + ((pDDR_Reg->DCR >> 11) & 0x3));
    }
    else if(useful_data.testCap == 0)
    {
        cap = (0x1 << 20);
    }
    else
    {
        cap = useful_data.testCap;
    }

    wantraw = cap>>20;
    memshift = 20; /* megabytes */
    
    wantbytes_orig = wantbytes = ((size_t) wantraw << memshift);
    wantmb = (wantbytes_orig >> 20);
    
    loops = 10;

    print("want ");
    print_Dec((ull) wantmb);
    print("MB (");
    print_Dec((ull) wantbytes);
    print(" bytes)\n");
    buf = NULL;

    buf = (void volatile *) kmalloc(wantbytes, GFP_KERNEL);
   // buf = (void volatile *)0x60000000;
    bufsize = wantbytes;
    aligned = buf;

    halflen = bufsize / 2;
    count = halflen / sizeof(ul);
    bufa = (ulv *) aligned;
    bufb = (ulv *) ((size_t) aligned + halflen);

    for(loop=1; ((!loops) || loop <= loops); loop++) {
        print("Loop ");
        print_Dec(loop);
        //if (loops) {
        //    print_Dec(loops);
        //}
        print(":\n");
        print("  Stuck Address: ");
        if (!test_stuck_address(aligned, bufsize / sizeof(ul))) {
             print("ok\n");
        } else {
            exit_code |= EXIT_FAIL_ADDRESSLINES;
            goto error;
        }
        for (i=0;;i++) {
            if (!tests[i].name) break;
            print("  ");
            print(tests[i].name);
            print(": ");
            if (!tests[i].fp(bufa, bufb, count)) {
                print("ok\n");
            } else {
                exit_code |= EXIT_FAIL_OTHERTEST;
                goto error;
            }
        }
        print("\n");
    }
    kfree((const void *)buf);
    print("Done.\n");
    return 0;
error:
    print("failed\n");
    return 1;
}
