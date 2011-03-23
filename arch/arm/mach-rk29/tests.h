/*
 * Very simple yet very effective memory tester.
 * Originally by Simon Kirby <sim@stormix.com> <sim@neato.org>
 * Version 2 by Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Version 3 not publicly released.
 * Version 4 rewrite:
 * Copyright (C) 2007-2009 Charles Cazabon <charlesc-memtester@pyropus.ca>
 * Licensed under the terms of the GNU General Public License version 2 (only).
 * See the file COPYING for details.
 *
 * This file contains the declarations for the functions for the actual tests,
 * called from the main routine in memtester.c.  See other comments in that 
 * file.
 *
 */

#include <linux/kernel.h>
#include <mach/rk29_iomap.h>

#include <linux/random.h>

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
#endif


typedef unsigned long ul;
typedef unsigned long long ull;
typedef unsigned long volatile ulv;
/* DDR Controller register struct */
typedef volatile struct DDR_REG_Tag
{
    volatile unsigned int CCR;                    //Controller Configuration Register
    volatile unsigned int DCR;                    //DRAM Configuration Register
    volatile unsigned int IOCR;                   //IO Configuration Register
    volatile unsigned int CSR;                    //Controller Status Register
    volatile unsigned int DRR;                    //DRAM Refresh Register
    volatile unsigned int TPR[3];                 //SDRAM Timing Parameters Registers
    volatile unsigned int DLLCR;                  //Global DLL Control  Register
    volatile unsigned int DLLCR09[10];            //DDR Control  Register 0-9
    volatile unsigned int RSLR[4];                //Rank System Latency  Register 0-3
    volatile unsigned int RDGR[4];                //Rank DQS Gating  Register 0-3
    volatile unsigned int DQTR[9];                //DQ Timing  Register 0-8
    volatile unsigned int DQSTR;                  //DQS Timing  Register
    volatile unsigned int DQSBTR;                 //DQS_b Timing  Register
    volatile unsigned int ODTCR;                  //ODT Configuration  Register
    volatile unsigned int DTR[2];                 //Data Training Register 0-1
    volatile unsigned int DTAR;                   //Data Training Address  Register
    volatile unsigned int ZQCR[3];                //SDRAM ZQ Control Register and SDRAM ZQCS Control Register 0-2
    volatile unsigned int ZQSR;                   //SDRAM ZQ Status Register
    volatile unsigned int TPR3;                   //SDRAM Timing Parameters Register 3
    volatile unsigned int ALPMR;                  //Automatic Low Power Mode Register
    volatile unsigned int Reserved[0x7c-0x30];    
    volatile unsigned int MR;                     //Mode Register
    volatile unsigned int EMR;                    //Extended Mode Register
    volatile unsigned int EMR2;                   //Extended Mode Register 2
    volatile unsigned int EMR3;                   //Extended Mode Register 3
    //Memory Management Unit Registers
    volatile unsigned int HPCR[32];               //Host Port Configuration Register 0-31
    volatile unsigned int PQCR[8];                //Priority Queue Configuration Register 0-7
    volatile unsigned int MMGCR;                  //Memory Manager General Configuration Register
}DDR_REG_T, *pDDR_REG_T;

typedef struct tagGPIO_IOMUX
{
    volatile unsigned int GPIOL_IOMUX;
    volatile unsigned int GPIOH_IOMUX;
}GPIO_IOMUX_T;

//GRF Registers
typedef volatile struct tagREG_FILE
{
    volatile unsigned int GRF_GPIO_DIR[6]; 
    volatile unsigned int GRF_GPIO_DO[6];
    volatile unsigned int GRF_GPIO_EN[6];
    GPIO_IOMUX_T GRF_GPIO_IOMUX[6];
    volatile unsigned int GRF_GPIO_PULL[7];
    volatile unsigned int GRF_UOC_CON[2];
    volatile unsigned int GRF_USB_CON;
    volatile unsigned int GRF_CPU_CON[2];
    volatile unsigned int GRF_CPU_STATUS;
    volatile unsigned int GRF_MEM_CON;
    volatile unsigned int GRF_MEM_STATUS[3];
    volatile unsigned int GRF_SOC_CON[5];
    volatile unsigned int GRF_OS_REG[4];
} REG_FILE, *pREG_FILE;

//CRU Registers
typedef volatile struct tagCRU_REG 
{
    volatile unsigned int CRU_APLL_CON; 
    volatile unsigned int CRU_DPLL_CON;
    volatile unsigned int CRU_CPLL_CON;
    volatile unsigned int CRU_PPLL_CON;
    volatile unsigned int CRU_MODE_CON;
    volatile unsigned int CRU_CLKSEL_CON[18];
    volatile unsigned int CRU_CLKGATE_CON[4];
    volatile unsigned int CRU_SOFTRST_CON[3];
} CRU_REG, *pCRU_REG;

#define pDDR_Reg 	    ((pDDR_REG_T)RK29_DDRC_BASE)
#define pGRF_Reg        ((pREG_FILE)RK29_GRF_BASE)
#define pSCU_Reg        ((pCRU_REG)RK29_CRU_BASE)

struct test
{
    char *name;
    int (*fp)(ulv *bufa, ulv *bufb, size_t count);
};

typedef struct useful_data_tag
{
    unsigned int testCap;  //测试的容量
    unsigned int WriteFreq;
    unsigned int ReadFreq;
}useful_data_t;
extern void printascii(const char *s);
extern void print_Dec(unsigned int n);
extern void print_Hex(unsigned int hex);
extern void print(const char *s);
extern void print_Dec_3(unsigned int value);


/* Function declaration. */

int test_stuck_address(unsigned long volatile *bufa, size_t count);
int test_random_value(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_xor_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_sub_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_mul_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_div_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_or_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_and_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_seqinc_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_solidbits_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_checkerboard_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_blockseq_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_walkbits0_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_walkbits1_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_bitspread_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_bitflip_comparison(unsigned long volatile *bufa, unsigned long volatile *bufb, size_t count);
int test_simple_comparison(ulv *bufa, ulv *bufb, size_t count);

