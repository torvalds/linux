/*
 * Very simple but very effective user-space memory tester.
 * Originally by Simon Kirby <sim@stormix.com> <sim@neato.org>
 * Version 2 by Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Version 3 not publicly released.
 * Version 4 rewrite:
 * Copyright (C) 2004-2010 Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Licensed under the terms of the GNU General Public License version 2 (only).
 * See the file COPYING for details.
 *
 * This file contains the functions for the actual tests, called from the
 * main routine in memtester.c.  See other comments in that file.
 *
 */
#include <linux/kernel.h>
#include <linux/random.h>
#include <linux/slab.h>
#include <plat/sram.h>

//#if (ULONG_MAX == 4294967295UL)
#if 1
    #define rand_ul() random32()
    #define UL_ONEBITS 0xffffffff
    #define UL_LEN 32
    #define CHECKERBOARD1 0x55555555
    #define CHECKERBOARD2 0xaaaaaaaa
    #define UL_BYTE(x) ((x | x << 8 | x << 16 | x << 24))
#elif (ULONG_MAX == 18446744073709551615ULL)
    #define rand64() (((ul) rand32()) << 32 | ((ul) rand32()))
    #define rand_ul() rand64()
    #define UL_ONEBITS 0xffffffffffffffffUL
    #define UL_LEN 64
    #define CHECKERBOARD1 0x5555555555555555
    #define CHECKERBOARD2 0xaaaaaaaaaaaaaaaa
    #define UL_BYTE(x) (((ul)x | (ul)x<<8 | (ul)x<<16 | (ul)x<<24 | (ul)x<<32 | (ul)x<<40 | (ul)x<<48 | (ul)x<<56))
#else
    #error long on this platform is not 32 or 64 bits
#endif

#define TEST_ALL

#ifdef TEST_ALL   // TEST_ALL的时候这些都不动
#define TEST_RANDOM
#define TEST_XOR
#define TEST_SUB
#define TEST_MUL
#define TEST_DIV
#define TEST_OR
#define TEST_AND
#define TEST_SEQINC
#define TEST_SOLID_BIT
#define TEST_BLOCK_SEQ
#define TEST_CHECK_BOARD
#define TEST_BIT_SPREAD
#define TEST_BIT_FLIP
#define TEST_ONE
#define TEST_ZERO
#define TEST_NARROW_WRITES
#else  //这些配置用于增删
//#define TEST_RANDOM
//#define TEST_XOR
//#define TEST_SUB
//#define TEST_MUL
//#define TEST_DIV
//#define TEST_OR
//#define TEST_AND
//#define TEST_SEQINC
//#define TEST_SOLID_BIT
//#define TEST_BLOCK_SEQ
//#define TEST_CHECK_BOARD
//#define TEST_BIT_SPREAD
#define TEST_BIT_FLIP
//#define TEST_ONE
//#define TEST_ZERO
//#define TEST_NARROW_WRITES
#endif


typedef unsigned long ul;
typedef unsigned long long ull;
typedef unsigned long volatile ulv;
typedef unsigned char volatile u8v;
typedef unsigned short volatile u16v;

struct test
{
    char *name;
    int (*fp)(ulv *bufa, ulv *bufb, size_t count);
};

union {
    unsigned char bytes[UL_LEN/8];
    ul val;
} mword8;

union {
    unsigned short u16s[UL_LEN/16];
    ul val;
} mword16;

#define printf(s)       sram_printascii(s)
#define fflush(out)     do {} while (0)
#define putchar(c)      sram_printch(c)

static void print(const char *s)
{
    sram_printascii(s);
}

static void print_Hex(unsigned int hex)
{
    sram_printhex(hex);
}

static void print_Dec (uint32_t n)
{
    if (n >= 10)
    {
        print_Dec(n / 10);
        n %= 10;
    }
    sram_printch((char)(n + '0'));
}

static void print_Dec_3(uint32_t value)
{
    if(value<10)
    {
        print("  ");
    }
    else if(value<100)
    {
        print(" ");
    }
    else
    {
    }
    print_Dec(value);
}

static const char progress[] = "-\\|/";
#define PROGRESSLEN 4
#define PROGRESSOFTEN 2500
#define ONE 0x00000001L

/* Function definitions. */

static int compare_regions(ulv *bufa, ulv *bufb, size_t count) {
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

static int compare_regions_reverse(ulv *bufa, ulv *bufb, size_t count) {
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

static int test_stuck_address(ulv *bufa, size_t count) {
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
static int test_random_value(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_xor_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_sub_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_mul_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_div_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_or_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_and_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_seqinc_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_solidbits_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_checkerboard_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_blockseq_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
static int test_walkbits0_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
                //*p1++ = *p2++ = ONE << j;
                value = ONE << j;
                *p1++ = value;
                *p2++ = ~value;
            } else { /* Walk it back down. */
                //*p1++ = *p2++ = ONE << (UL_LEN * 2 - j - 1);
                value = ONE << (UL_LEN * 2 - j - 1);
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
static int test_walkbits1_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
                //*p1++ = *p2++ = UL_ONEBITS ^ (ONE << j);
                value = UL_ONEBITS ^ (ONE << j);
                *p1++ = value;
                *p2++ = ~value;
            } else { /* Walk it back down. */
                //*p1++ = *p2++ = UL_ONEBITS ^ (ONE << (UL_LEN * 2 - j - 1));
                value = UL_ONEBITS ^ (ONE << (UL_LEN * 2 - j - 1));
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
static int test_bitspread_comparison(ulv *bufa, ulv *bufb, size_t count) {
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
                //    ? (ONE << j) | (ONE << (j + 2))
                //    : UL_ONEBITS ^ ((ONE << j)
                //                    | (ONE << (j + 2)));
                value = (i % 2 == 0)
                    ? (ONE << j) | (ONE << (j + 2))
                    : UL_ONEBITS ^ ((ONE << j)
                                    | (ONE << (j + 2)));
                *p1++ =  value;
                *p2++ = ~value;
            } else { /* Walk it back down. */
                //*p1++ = *p2++ = (i % 2 == 0)
                //    ? (ONE << (UL_LEN * 2 - 1 - j)) | (ONE << (UL_LEN * 2 + 1 - j))
                //    : UL_ONEBITS ^ (ONE << (UL_LEN * 2 - 1 - j)
                //                    | (ONE << (UL_LEN * 2 + 1 - j)));
                value = (i % 2 == 0)
                    ? (ONE << (UL_LEN * 2 - 1 - j)) | (ONE << (UL_LEN * 2 + 1 - j))
                    : UL_ONEBITS ^ (ONE << (UL_LEN * 2 - 1 - j)
                                    | (ONE << (UL_LEN * 2 + 1 - j)));
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
static int test_bitflip_comparison(ulv *bufa, ulv *bufb, size_t count) {
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    unsigned int j, k;
    ul q;
    size_t i;
    ul value;

    print("           ");
    for (k = 0; k < UL_LEN; k++) {
        q = ONE << k;
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

#ifdef TEST_NARROW_WRITES    
static int test_8bit_wide_random(ulv* bufa, ulv* bufb, size_t count) {
    u8v *p1, *t;
    ulv *p2;
    int attempt;
    unsigned int b, j = 0;
    size_t i;

    putchar(' ');
    fflush(stdout);
    for (attempt = 0; attempt < 2;  attempt++) {
        if (attempt & 1) {
            p1 = (u8v *) bufa;
            p2 = bufb;
        } else {
            p1 = (u8v *) bufb;
            p2 = bufa;
        }
        for (i = 0; i < count; i++) {
            t = mword8.bytes;
            *p2++ = mword8.val = rand_ul();
            for (b=0; b < UL_LEN/8; b++) {
                *p1++ = *t++;
            }
            if (!(i % PROGRESSOFTEN)) {
                putchar('\b');
                putchar(progress[++j % PROGRESSLEN]);
                fflush(stdout);
            }
        }
        if (compare_regions(bufa, bufb, count)) {
            return -1;
        }
    }
    printf("\b \b");
    fflush(stdout);
    return 0;
}

static int test_16bit_wide_random(ulv* bufa, ulv* bufb, size_t count) {
    u16v *p1, *t;
    ulv *p2;
    int attempt;
    unsigned int b, j = 0;
    size_t i;

    putchar( ' ' );
    fflush( stdout );
    for (attempt = 0; attempt < 2; attempt++) {
        if (attempt & 1) {
            p1 = (u16v *) bufa;
            p2 = bufb;
        } else {
            p1 = (u16v *) bufb;
            p2 = bufa;
        }
        for (i = 0; i < count; i++) {
            t = mword16.u16s;
            *p2++ = mword16.val = rand_ul();
            for (b = 0; b < UL_LEN/16; b++) {
                *p1++ = *t++;
            }
            if (!(i % PROGRESSOFTEN)) {
                putchar('\b');
                putchar(progress[++j % PROGRESSLEN]);
                fflush(stdout);
            }
        }
        if (compare_regions(bufa, bufb, count)) {
            return -1;
        }
    }
    printf("\b \b");
    fflush(stdout);
    return 0;
}
#endif

#define EXIT_FAIL_NONSTARTER    0x01
#define EXIT_FAIL_ADDRESSLINES  0x02
#define EXIT_FAIL_OTHERTEST     0x04

static struct test tests[]
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
#ifdef TEST_NARROW_WRITES    
    { "8-bit Writes", test_8bit_wide_random },
    { "16-bit Writes", test_16bit_wide_random },
#endif
    { NULL, NULL }
};

static int exit_code = 0;
static ul cap = 2*1024*1024;

int memtester(void) {
    ul loops, loop, i;
    size_t pagesize, wantraw, wantmb, wantbytes, wantbytes_orig, bufsize,
         halflen, count;
    ptrdiff_t pagesizemask;
    void volatile *buf, *aligned;
    ulv *bufa, *bufb;
    int memshift;

    print("Copyright (C) 2010 Charles Cazabon.\n");
    print("Licensed under the GNU General Public License version 2 (only).\n");
    print("\n");
    pagesize = 1024;
    pagesizemask = (ptrdiff_t) ~(pagesize - 1);
    print("pagesizemask is 0x");
    print_Hex(pagesizemask);
    print("\n");

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

#include <linux/module.h>
static int set_cap(const char *val, const struct kernel_param *kp)
{
	int ret;

	ret = param_set_ulong(val, kp);
	if (ret < 0)
		return ret;

	memtester();

	return 0;
}

static struct kernel_param_ops cap_param_ops = {
	.set = set_cap,
	.get = param_get_ulong,
};

module_param_cb(cap, &cap_param_ops, &cap, 0644);
