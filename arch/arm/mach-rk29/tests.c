/*
 * Very simple but very effective user-space memory tester.
 * Originally by Simon Kirby <sim@stormix.com> <sim@neato.org>
 * Version 2 by Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Version 3 not publicly released.
 * Version 4 rewrite:
 * Copyright (C) 2007-2009 Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Licensed under the terms of the GNU General Public License version 2 (only).
 * See the file COPYING for details.
 *
 * This file contains the functions for the actual tests, called from the
 * main routine in memtester.c.  See other comments in that file.
 *
 */

#include "tests.h"

char progress[] = "-\\|/";
#define PROGRESSLEN 4
#define PROGRESSOFTEN 2500

int compare_regions(ulv *bufa, ulv *bufb, size_t count) {
    int r = 0;
    size_t i;
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    int n=0;

    for (i = 0; i < count; i++, p1++, p2++) {
        if (*p1 != *p2) {
            {
                print("FAILURE: 0x");
                print_Hex((ul) *p1);
                print(" != 0x");
                print_Hex((ul) *p2);
                print(" at offset 0x");
                print_Hex((ul) i);
                print(".\n");
            }
            /* printf("Skipping to next test..."); */
            r = -1;
            n++;
            if(n>10)
            {
                break;
            }
        }
    }
    return r;
}

int compare_regions_reverse(ulv *bufa, ulv *bufb, size_t count) {
    int r = 0;
    size_t i;
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    int n=0;

    for (i = 0; i < count; i++, p1++, p2++) {
        if (*p1 != ~(*p2)) {
            {
                print("FAILURE: 0x");
                print_Hex((ul) *p1);
                print(" != 0x");
                print_Hex((ul) *p2);
                print(" at offset 0x");
                print_Hex((ul) i);
                print(".\n");
            }
            /* printf("Skipping to next test..."); */
            r = -1;
            n++;
            if(n>10)
            {
                break;
            }
        }
    }
    return r;
}

int test_stuck_address(ulv *bufa, size_t count) {
    ulv *p1 = bufa;
    unsigned int j;
    size_t i;

    print("           ");
    for (j = 0; j < 16; j++) {
        print("\b\b\b\b\b\b\b\b\b\b\b");
        p1 = (ulv *) bufa;
        print("setting ");
        print_Dec_3(j);
        for (i = 0; i < count; i++) {
            *p1 = ((j + i) % 2) == 0 ? (ul) p1 : ~((ul) p1);
            *p1++;
        }
        print("\b\b\b\b\b\b\b\b\b\b\b");
        print("testing ");
        print_Dec_3(j);
        p1 = (ulv *) bufa;
        for (i = 0; i < count; i++, p1++) {
            if (*p1 != (((j + i) % 2) == 0 ? (ul) p1 : ~((ul) p1))) {
                {
                    print("FAILURE: possible bad address line at offset 0x");
                    print_Hex((ul) i);
                    print(".\n");
                }
                print("Skipping to next test...\n");
                return -1;
            }
        }
    }
    print("\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b");
    return 0;
}

#ifdef TEST_RANDOM
int test_random_value(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    
    for (i = 0; i < count; i++) {
        *p1++ = *p2++ = rand_ul();
        if (!(i % PROGRESSOFTEN)) {
        }
    }
    print("\b \b");
    return compare_regions(bufa, bufb, count);
}
#endif

#ifdef TEST_XOR
int test_xor_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ ^= q;
        *p2++ ^= q;
    }
    return compare_regions(bufa, bufb, count);
}
#endif

#ifdef TEST_SUB
int test_sub_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ -= q;
        *p2++ -= q;
    }
    return compare_regions(bufa, bufb, count);
}
#endif

#ifdef TEST_MUL
int test_mul_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ *= q;
        *p2++ *= q;
    }
    return compare_regions(bufa, bufb, count);
}
#endif

#ifdef TEST_DIV
int test_div_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        if (!q) {
            q++;
        }
        *p1++ /= q;
        *p2++ /= q;
    }
    return compare_regions(bufa, bufb, count);
}
#endif

#ifdef TEST_OR
int test_or_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ |= q;
        *p2++ |= q;
    }
    return compare_regions(bufa, bufb, count);
}
#endif

#ifdef TEST_AND
int test_and_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();

    for (i = 0; i < count; i++) {
        *p1++ &= q;
        *p2++ &= q;
    }
    return compare_regions(bufa, bufb, count);
}
#endif

#ifdef TEST_SEQINC
int test_seqinc_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    size_t i;
    ul q = rand_ul();
    ul value;

    for (i = 0; i < count; i++) {
        value = (i+q);
        *p1++ = value;
        *p2++ = ~value;
        //*p1++ = *p2++ = (i + q);
    }
    return compare_regions_reverse(bufa, bufb, count);
}
#endif

#ifdef TEST_SOLID_BIT
int test_solidbits_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    ul q;
    size_t i;
    ul value;

    print("           ");
    for (j = 0; j < 64; j++) {
        print("\b\b\b\b\b\b\b\b\b\b\b");
        q = (j % 2) == 0 ? UL_ONEBITS : 0;
        print("setting ");
        print_Dec_3(j);
        p1 = (ulv *) bufa;
        p2 = (ulv *) bufb;
        for (i = 0; i < count; i++) {
            value = (i % 2) == 0 ? q : ~q;
            *p1++ = value;
            *p2++ = ~value;
            //*p1++ = *p2++ = (i % 2) == 0 ? q : ~q;
        }
        print("\b\b\b\b\b\b\b\b\b\b\b");
        print("testing ");
        print_Dec_3(j);
        if (compare_regions_reverse(bufa, bufb, count)) {
            return -1;
        }
    }
    print("\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b");
    return 0;
}
#endif

#ifdef TEST_CHECK_BOARD
int test_checkerboard_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    ul q;
    size_t i;
    ul value;

    print("           ");
    for (j = 0; j < 64; j++) {
        print("\b\b\b\b\b\b\b\b\b\b\b");
        q = (j % 2) == 0 ? CHECKERBOARD1 : CHECKERBOARD2;
        print("setting ");
        print_Dec_3(j);
        p1 = (ulv *) bufa;
        p2 = (ulv *) bufb;
        for (i = 0; i < count; i++) {
            value = (i % 2) == 0 ? q : ~q;
            *p1++ = value;
            *p2++ = ~value;
            //*p1++ = *p2++ = (i % 2) == 0 ? q : ~q;
        }
        print("\b\b\b\b\b\b\b\b\b\b\b");
        print("testing ");
        print_Dec_3(j);
        if (compare_regions_reverse(bufa, bufb, count)) {
            return -1;
        }
    }
    print("\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b");
    return 0;
}
#endif

#ifdef TEST_BLOCK_SEQ
int test_blockseq_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    size_t i;
    ul value;

    print("           ");
    for (j = 0; j < 256; j++) {
        print("\b\b\b\b\b\b\b\b\b\b\b");
        p1 = (ulv *) bufa;
        p2 = (ulv *) bufb;
        print("setting ");
        print_Dec_3(j);
        for (i = 0; i < count; i++) {
            value = (ul) UL_BYTE(j); 
            *p1++ = value;
            *p2++ = ~value;
            //*p1++ = *p2++ = (ul) UL_BYTE(j);
        }
        print("\b\b\b\b\b\b\b\b\b\b\b");
        print("testing ");
        print_Dec_3(j);
        if (compare_regions_reverse(bufa, bufb, count)) {
            return -1;
        }
    }
    print("\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b");
    return 0;
}
#endif

#ifdef TEST_ZERO
int test_walkbits0_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    size_t i;
    ul value;

    print("           ");
    for (j = 0; j < UL_LEN * 2; j++) {
        print("\b\b\b\b\b\b\b\b\b\b\b");
        p1 = (ulv *) bufa;
        p2 = (ulv *) bufb;
        print("setting ");
        print_Dec_3(j);
        for (i = 0; i < count; i++) {
            if (j < UL_LEN) { /* Walk it up. */
                //*p1++ = *p2++ = 0x00000001 << j;
                value = 0x00000001 << j;
                *p1++ = value;
                *p2++ = ~value;
            } else { /* Walk it back down. */
                //*p1++ = *p2++ = 0x00000001 << (UL_LEN * 2 - j - 1);
                value = 0x00000001 << (UL_LEN * 2 - j - 1);
                *p1++ = value;
                *p2++ = ~value;
            }
        }
        print("\b\b\b\b\b\b\b\b\b\b\b");
        print("testing ");
        print_Dec_3(j);
        if (compare_regions_reverse(bufa, bufb, count)) {
            return -1;
        }
    }
    print("\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b");
    return 0;
}
#endif

#ifdef TEST_ONE
int test_walkbits1_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    size_t i;
    ul value;

    print("           ");
    for (j = 0; j < UL_LEN * 2; j++) {
        print("\b\b\b\b\b\b\b\b\b\b\b");
        p1 = (ulv *) bufa;
        p2 = (ulv *) bufb;
        print("setting ");
        print_Dec_3(j);
        for (i = 0; i < count; i++) {
            if (j < UL_LEN) { /* Walk it up. */
                //*p1++ = *p2++ = UL_ONEBITS ^ (0x00000001 << j);
                value = UL_ONEBITS ^ (0x00000001 << j);
                *p1++ = value;
                *p2++ = ~value;
            } else { /* Walk it back down. */
                //*p1++ = *p2++ = UL_ONEBITS ^ (0x00000001 << (UL_LEN * 2 - j - 1));
                value = UL_ONEBITS ^ (0x00000001 << (UL_LEN * 2 - j - 1));
                *p1++ = value;
                *p2++ = ~value;
            }
        }
        print("\b\b\b\b\b\b\b\b\b\b\b");
        print("testing ");
        print_Dec_3(j);
        if (compare_regions_reverse(bufa, bufb, count)) {
            return -1;
        }
    }
    print("\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b");
    return 0;
}
#endif

#ifdef TEST_BIT_SPREAD
int test_bitspread_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j;
    size_t i;
    ul value;

    print("           ");
    for (j = 0; j < UL_LEN * 2; j++) {
        print("\b\b\b\b\b\b\b\b\b\b\b");
        p1 = (ulv *) bufa;
        p2 = (ulv *) bufb;
        print("setting ");
        print_Dec_3(j);
        for (i = 0; i < count; i++) {
            if (j < UL_LEN) { /* Walk it up. */
                //*p1++ = *p2++ = (i % 2 == 0)
                //    ? (0x00000001 << j) | (0x00000001 << (j + 2))
                //    : UL_ONEBITS ^ ((0x00000001 << j)
                //                    | (0x00000001 << (j + 2)));
                value = (i % 2 == 0)
                    ? (0x00000001 << j) | (0x00000001 << (j + 2))
                    : UL_ONEBITS ^ ((0x00000001 << j)
                                    | (0x00000001 << (j + 2)));
                *p1++ =  value;
                *p2++ = ~value;
            } else { /* Walk it back down. */
                //*p1++ = *p2++ = (i % 2 == 0)
                //    ? (0x00000001 << (UL_LEN * 2 - 1 - j)) | (0x00000001 << (UL_LEN * 2 + 1 - j))
                //    : UL_ONEBITS ^ (0x00000001 << (UL_LEN * 2 - 1 - j)
                //                    | (0x00000001 << (UL_LEN * 2 + 1 - j)));
                value = (i % 2 == 0)
                    ? (0x00000001 << (UL_LEN * 2 - 1 - j)) | (0x00000001 << (UL_LEN * 2 + 1 - j))
                    : UL_ONEBITS ^ (0x00000001 << (UL_LEN * 2 - 1 - j)
                                    | (0x00000001 << (UL_LEN * 2 + 1 - j)));
                *p1++ = value;
                *p2++ = ~value;
            }
        }
        print("\b\b\b\b\b\b\b\b\b\b\b");
        print("testing ");
        print_Dec_3(j);
        if (compare_regions_reverse(bufa, bufb, count)) {
            return -1;
        }
    }
    print("\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b");
    return 0;
}
#endif

#ifdef TEST_BIT_FLIP
int test_bitflip_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j, k;
    ul q;
    size_t i;
    ul value;

    print("           ");
    for (k = 0; k < UL_LEN; k++) {
        q = 0x00000001 << k;
        for (j = 0; j < 8; j++) {
            print("\b\b\b\b\b\b\b\b\b\b\b");
            q = ~q;
            print("setting ");
            print_Dec_3(k * 8 + j);
            p1 = (ulv *) bufa;
            p2 = (ulv *) bufb;
            for (i = 0; i < count; i++) {
                //*p1++ = *p2++ = (i % 2) == 0 ? q : ~q;
                value = (i % 2) == 0 ? q : ~q;
                *p1++ = value;
                *p2++ = ~value;
            }
            print("\b\b\b\b\b\b\b\b\b\b\b");
            print("testing ");
            print_Dec_3(k * 8 + j);
            if (compare_regions_reverse(bufa, bufb, count)) {
                return -1;
            }
        }
    }
    print("\b\b\b\b\b\b\b\b\b\b\b           \b\b\b\b\b\b\b\b\b\b\b");
    return 0;
}
#endif
