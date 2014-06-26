
#include <linux/string.h>
#include <linux/resume-trace.h>
#include <linux/workqueue.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/syscalls.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/freezer.h>
#include <linux/vmalloc.h>


//#include	"..\common\config.h"

//#define TEST_VALUE 0xAAAAAAAA//0x55555555





#define TEST_VALUE 0x55555555
#define UL_ONEBITS 0xffffffff
//#define TEST_DEBUG_EN   0
#define UL_BYTE(x) ((x | x << 8 | x << 16 | x << 24))
#define BUF_SIZE     (256 * 1024)  //(256 * 1024)

//static char memtest_buf[BUF_SIZE]  __attribute__((aligned(4096)));

#define printf(fmt, arg...) \
		printk(KERN_EMERG fmt, ##arg)


//char* pTestmem=(char *)CACHE_FIFO_STAR_ADDR0;

typedef unsigned long ul;
typedef unsigned long volatile ulv;

static int compare_regions(ulv *bufa, ulv *bufb, ul count) 
{
    int r = 0;
    ul i;
    ulv *p1 = bufa;
    ulv *p2 = bufb;
  //  int n=0;

    for (i = 0; i < count; i++, p1++, p2++) 
    {
        if (*p1 != *p2) 
        {
            r = -1;
          //  n++;
         //   if(n>10)
         //   {
            break;
         //   }
        }
    }
    return r;
}

static int compare_regions_reverse(ulv *bufa, ulv *bufb, ul count) 
{
    int r = 0;
    ul i;
    ulv *p1 = bufa;
    ulv *p2 = bufb;
  //  int n=0;

    for (i = 0; i < count; i++, p1++, p2++) 
    {
        if (*p1 != ~(*p2)) 
        {
            r = -1;
           // n++;
           // if(n>10)
           // {
                break;
           // }
        }
    }
    return r;
}

static int test_stuck_address(ulv *bufa, ul count) 
{
    ulv *p1 = bufa;
    ul j;
    ul i;

    for (j = 0; j < 16; j++)
    {
        p1 = (ulv *) bufa;
        for (i = 0; i < count; i++) 
        {
            *p1 = (((j + i) % 2) == 0 ? (ul) p1 : ~((ul) p1));
            *p1++;
        }
        p1 = (ulv *) bufa;
        for (i = 0; i < count; i++, p1++) 
        {
            if (*p1 != (((j + i) % 2) == 0 ? (ul) p1 : ~((ul) p1))) 
            {
                return -1;
            }
        }
    }
    return 0;
}

static int test_random_value(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul i;
    
    for (i = 0; i < count; i++) 
    {
        *p1++ = *p2++ = TEST_VALUE;
    }
    return compare_regions(bufa, bufb, count);
}

static int test_xor_comparison(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul i;
    ul q = TEST_VALUE;

    for (i = 0; i < count; i++) 
    {
        *p1++ ^= q;
        *p2++ ^= q;
    }
    return compare_regions(bufa, bufb, count);
}

static int test_sub_comparison(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul i;
    ul q = TEST_VALUE;

    for (i = 0; i < count; i++) 
    {
        *p1++ -= q;
        *p2++ -= q;
    }
    return compare_regions(bufa, bufb, count);
}

static int test_mul_comparison(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul i;
    ul q = TEST_VALUE;

    for (i = 0; i < count; i++) 
    {
        *p1++ *= q;
        *p2++ *= q;
    }
    return compare_regions(bufa, bufb, count);
}

static int test_div_comparison(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul i;
    ul q = TEST_VALUE;

    for (i = 0; i < count; i++) 
    {
        if (!q) 
        {
            q++;
        }
        *p1++ /= q;
        *p2++ /= q;
    }
    return compare_regions(bufa, bufb, count);
}

static int test_or_comparison(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul i;
    ul q = TEST_VALUE;

    for (i = 0; i < count; i++) 
    {
        *p1++ |= q;
        *p2++ |= q;
    }
    return compare_regions(bufa, bufb, count);
}

static int test_and_comparison(ulv *bufa, ulv *bufb, ul count)
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul i;
    ul q = TEST_VALUE;

    for (i = 0; i < count; i++) 
    {
        *p1++ &= q;
        *p2++ &= q;
    }
    return compare_regions(bufa, bufb, count);
}

static int test_seqinc_comparison(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul i;
    ul q = TEST_VALUE;
    ul value;

    for (i = 0; i < count; i++) 
    {
        value = (i+q);
        *p1++ = value;
        *p2++ = ~value;
    }
    return compare_regions_reverse(bufa, bufb, count);
}

static int test_solidbits_comparison(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul j;
    ul q;
    ul i;
    ul value;

    for (j = 0; j < 64; j++) 
    {
        q = ((j % 2) == 0 ? UL_ONEBITS : 0);
        p1 = (ulv *) bufa;
        p2 = (ulv *) bufb;
        for (i = 0; i < count; i++) 
        {
            value = ((i % 2) == 0 ? q : ~q);
            *p1++ = value;
            *p2++ = ~value;
        }
        if (compare_regions_reverse(bufa, bufb, count))
        {
            return -1;
        }
    }
    return 0;
}

static int test_blockseq_comparison(ulv *bufa, ulv *bufb, ul count) 
{
    ulv *p1 = bufa;
    ulv *p2 = bufb;
    ul j;
    ul i;
    ul value;

    for (j = 0; j < 256; j++) 
    {
        p1 = (ulv *) bufa;
        p2 = (ulv *) bufb;
        for (i = 0; i < count; i++) 
        {
            value = (ul) UL_BYTE(j); 
            *p1++ = value;
            *p2++ = ~value;
        }
        if (compare_regions_reverse(bufa, bufb, count))
        {
            return -1;
        }
    }
    return 0;
}

#define FT_FAILED (-1)
int Test_mem(void *aligned, unsigned long b_size) 
{
    ulv *bufa, *bufb;
    ul halflen, count;
   // int i;

    halflen = b_size / 2;        
    count = halflen / sizeof(ul);
    //aligned = memtest_buf;
    bufa = (ulv *)aligned;
    bufb = (ulv *)((ul)aligned + halflen);

    if (test_stuck_address(aligned, (BUF_SIZE) / sizeof(ul)))
    {   
         printf("test_stuck_address_ error\n");
          return FT_FAILED;//-1;
    }   
    if(test_random_value(bufa, bufb, count))
    {   
         printf("test_random_value_ error\n");
          return FT_FAILED;//-1;
    } 
    if(test_xor_comparison(bufa, bufb, count))
    {   
         printf("test_xor_comparison_ error\n");
          return FT_FAILED;//-1;
    }
    if(test_sub_comparison(bufa, bufb, count))
    {    
         printf("test_sub_comparison_ error\n");
         return FT_FAILED;//-1;
    }
    //     printf("test_sub_comparison_ OK\n");
    if(test_mul_comparison(bufa, bufb, count))
    {    
         printf("test_mul_comparison_ error\n");
         return FT_FAILED;//-1;
    }
    if(test_div_comparison(bufa, bufb, count))
    {    
         printf("test_div_comparison_ error\n");
         return FT_FAILED;//-1;
    }
    if(test_or_comparison(bufa, bufb, count))
    {   
         printf("test_or_comparison_ error\n");
          return FT_FAILED;//-1;
    }
    if(test_and_comparison(bufa, bufb, count))
    {    
         printf("test_and_comparison_ error\n");
         return FT_FAILED;//-1;
    }
    //     printf("test_and_comparison_ OK\n");
    if(test_seqinc_comparison(bufa, bufb, count))
    {    
         printf("test_seqinc_comparison_ error\n");
         return FT_FAILED;//-1;
    }
    //printf("test_seqinc_comparison_ OK\n");
    
    if(test_solidbits_comparison(bufa, bufb, count))
    {              
         printf("test_solidbits_comparison_ error\n");
         return FT_FAILED;//-1;  
    }
    //printf("test_solidbits_comparison_ OK\n");
    if(test_blockseq_comparison(bufa, bufb, count))
    {    
         printf("test_blockseq_comparison_ error\n");
         return FT_FAILED;//-1;
    }

    return 0;
}

#if 0//def TEST_CPU123
uint8 Test_mem123(char offset)
{
    void volatile *aligned;
    ulv *bufa, *bufb;
    ul halflen, count;
    int i;

    halflen = BUF_SIZE / 2;
    count = halflen / sizeof(ul);
    aligned = pTestmem + (offset << 20);
    bufa = (ulv *)aligned;
    bufb = (ulv *)((ul)aligned + halflen);

    if (test_stuck_address(aligned, (BUF_SIZE) / sizeof(ul)))
    {   
         printf("cpu%d test_stuck_address_ error\n",(offset+1));
          return FT_FAILED;//-1;
    }   
    if(test_random_value(bufa, bufb, count))
    {   
         printf("cpu%d test_random_value_ error\n",(offset+1));
          return FT_FAILED;//-1;
    } 
    if(test_xor_comparison(bufa, bufb, count))
    {   
         printf("cpu%d test_xor_comparison_ error\n",(offset+1));
          return FT_FAILED;//-1;
    }
    if(test_sub_comparison(bufa, bufb, count))
    {    
         printf("cpu%d test_sub_comparison_ error\n",(offset+1));
         return FT_FAILED;//-1;
    }
    //     printf("test_sub_comparison_ OK\n");
    if(test_mul_comparison(bufa, bufb, count))
    {    
         printf("cpu%d test_mul_comparison_ error\n",(offset+1));
         return FT_FAILED;//-1;
    }
    if(test_div_comparison(bufa, bufb, count))
    {    
         printf("cpu%d test_div_comparison_ error\n",(offset+1));
         return FT_FAILED;//-1;
    }
    if(test_or_comparison(bufa, bufb, count))
    {   
         printf("cpu%d test_or_comparison_ error\n",(offset+1));
          return FT_FAILED;//-1;
    }
    if(test_and_comparison(bufa, bufb, count))
    {    
         printf("cpu%d test_and_comparison_ error\n",(offset+1));
         return FT_FAILED;//-1;
    }
    //     printf("test_and_comparison_ OK\n");
    if(test_seqinc_comparison(bufa, bufb, count))
    {    
         printf("cpu%d test_seqinc_comparison_ error\n",(offset+1));
         return FT_FAILED;//-1;
    }
    //printf("test_seqinc_comparison_ OK\n");
    
    if(test_solidbits_comparison(bufa, bufb, count))
    {              
         printf("cpu%d test_solidbits_comparison_ error\n",(offset+1));
         return FT_FAILED;//-1;  
    }
    //printf("test_solidbits_comparison_ OK\n");
    if(test_blockseq_comparison(bufa, bufb, count))
    {    
         printf("cpu%d test_blockseq_comparison_ error\n",(offset+1));
         return FT_FAILED;//-1;
    }
	printf("cpu%d memory test ok\n",(offset+1));
    return FT_SUCCESS;
}
#endif

