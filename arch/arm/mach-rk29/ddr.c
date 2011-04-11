/*
 * arch/arm/mach-rk29/ddr.c
 *
 * Function Driver for DDR controller
 *
 * Copyright (C) 2011 Fuzhou Rockchip Electronics Co.,Ltd
 * Author: 
 * hcy@rock-chips.com
 * yk@rock-chips.com
 */
 
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/irqflags.h>
#include <linux/string.h>
#include <linux/version.h>
#include <linux/compiler.h>
#include <linux/platform_device.h>
#include <linux/time.h>
#include <linux/hrtimer.h>
#include <linux/earlysuspend.h>
#include <linux/random.h> 
#include <linux/crc32.h>
#include <linux/clk.h>

#include <mach/rk29_iomap.h>
#include <mach/memory.h>
#include <mach/sram.h>
#include <mach/ddr.h>

#include <asm/delay.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>
#include <asm/io.h>

#ifdef CONFIG_DDR_SDRAM_FREQ
#define DDR_FREQ          (CONFIG_DDR_SDRAM_FREQ)
#else
#define DDR_FREQ 400
#endif

#ifdef CONFIG_DDR_TYPE
#define DDR3_TYPE CONFIG_DDR_TYPE

#ifdef CONFIG_DDR_TYPE_DDR3_800D
#define DDR3_TYPE DDR3_800D
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_800E
#define DDR3_TYPE DDR3_800E
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1066E
#define DDR3_TYPE DDR3_1066E
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1066F
#define DDR3_TYPE DDR3_1066F
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1066G
#define DDR3_TYPE DDR3_1066G
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1333F
#define DDR3_TYPE DDR3_1333F
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1333G
#define DDR3_TYPE DDR3_1333G
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1333H
#define DDR3_TYPE DDR3_1333H
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1333J
#define DDR3_TYPE DDR3_1333J
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1600G
#define DDR3_TYPE DDR3_1600G
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1600H
#define DDR3_TYPE DDR3_1600H
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1600J
#define DDR3_TYPE DDR3_1600J
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1866J
#define DDR3_TYPE DDR3_1866J
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1866K
#define DDR3_TYPE DDR3_1866K
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1866L
#define DDR3_TYPE DDR3_1866L
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_1866M
#define DDR3_TYPE DDR3_1866M
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_2133K
#define DDR3_TYPE DDR3_2133K
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_2133L
#define DDR3_TYPE DDR3_2133L
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_2133M
#define DDR3_TYPE DDR3_2133M
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_2133N
#define DDR3_TYPE DDR3_2133N
#endif

#ifdef CONFIG_DDR_TYPE_DDR3_DEFAULT
#define DDR3_TYPE DDR3_DEFAULT
#endif

#if 0
#ifdef CONFIG_DDR_TYPE_DDRII
#define DDR3_TYPE DDR_DDRII
#endif

#ifdef CONFIG_DDR_TYPE_LPDDR
#define DDR3_TYPE DDR_LPDDR
#endif
#endif
#else
#define DDR3_TYPE   DDR3_DEFAULT
#endif

#define ddr_print(x...) printk( "DDR DEBUG: " x )

// controller base address
#define pDDR_Reg 	    ((pDDR_REG_T)RK29_DDRC_BASE)
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
#define pGRF_Reg        ((pREG_FILE)RK29_GRF_BASE)
#define pSCU_Reg        ((pCRU_REG)RK29_CRU_BASE)


// save_sp  must be static global variable  

static unsigned long save_sp;


uint32_t ddrDataTraining[16];

DDR_CONFIG_T    ddrConfig[3][10] = {
    //row, bank, col, config
    {
        //DDR2
        // x16
        {15, 8, 10, (DIO_16 | DSIZE_4Gb)},
        {14, 8, 10, (DIO_16 | DSIZE_2Gb)},
        {13, 8, 10, (DIO_16 | DSIZE_1Gb)},
        {13, 4, 10, (DIO_16 | DSIZE_512Mb)},
        {13, 4, 9,  (DIO_16 | DSIZE_256Mb)},
        // x8
        {16, 8, 10, (DIO_8  | DSIZE_4Gb)},
        {15, 8, 10, (DIO_8  | DSIZE_2Gb)},
        {14, 8, 10, (DIO_8  | DSIZE_1Gb)},
        {14, 4, 10, (DIO_8  | DSIZE_512Mb)},
        {13, 4, 10, (DIO_8  | DSIZE_256Mb)},
    },
    {
        //DDR3
        // x16
        {16, 8, 10, (DIO_16 | DSIZE_8Gb)},
        {15, 8, 10, (DIO_16 | DSIZE_4Gb)},
        {14, 8, 10, (DIO_16 | DSIZE_2Gb)},
        {13, 8, 10, (DIO_16 | DSIZE_1Gb)},
        {12, 8, 10, (DIO_16 | DSIZE_512Mb)},
        // x8
        {16, 8, 11, (DIO_8 | DSIZE_8Gb)},
        {16, 8, 10, (DIO_8 | DSIZE_4Gb)},
        {15, 8, 10, (DIO_8 | DSIZE_2Gb)},
        {14, 8, 10, (DIO_8 | DSIZE_1Gb)},
        {13, 8, 10, (DIO_8 | DSIZE_512Mb)},
    },
    {
    		//mobile DDR
        // x32
        {14, 4, 10, (DIO_32 | DSIZE_2Gb)},
        {13, 4, 10, (DIO_32 | DSIZE_1Gb)},
        {13, 4, 9,  (DIO_32 | DSIZE_512Mb)},
        {12, 4, 9,  (DIO_32 | DSIZE_256Mb)},
        // x16
        {14, 4, 11, (DIO_16 | DSIZE_2Gb)},
        {14, 4, 10, (DIO_16 | DSIZE_1Gb)},
        {13, 4, 10, (DIO_16 | DSIZE_512Mb)},
        {13, 4, 9,  (DIO_16 | DSIZE_256Mb)},
        {12, 4, 9,  (DIO_16 | DSIZE_128Mb)},
        {0,  0, 0,  0},
    }
};

uint32_t __sramdata ddr3_cl_cwl[22][3]={
/*   0~330           330~400         400~533        speed
* tCK  >3             2.5~3          1.875~2.5
*    cl<<16, cwl    cl<<16, cwl     cl<<16, cwl              */
    {((5<<16)|5),   ((5<<16)|5),    0        }, //DDR3_800D
    {((5<<16)|5),   ((6<<16)|5),    0        }, //DDR3_800E
    
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6) }, //DDR3_1066E
    {((5<<16)|5),   ((6<<16)|5),    ((7<<16)|6) }, //DDR3_1066F
    {((5<<16)|5),   ((6<<16)|5),    ((8<<16)|6) }, //DDR3_1066G
    
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6) }, //DDR3_1333F
    {((5<<16)|5),   ((5<<16)|5),    ((7<<16)|6) }, //DDR3_1333G
    {((5<<16)|5),   ((6<<16)|5),    ((7<<16)|6) }, //DDR3_1333H
    {((5<<16)|5),   ((6<<16)|5),    ((8<<16)|6) }, //DDR3_1333J
    
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6) }, //DDR3_1600G
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6) }, //DDR3_1600H
    {((5<<16)|5),   ((5<<16)|5),    ((7<<16)|6) }, //DDR3_1600J
    {((5<<16)|5),   ((6<<16)|5),    ((7<<16)|6) }, //DDR3_1600K
    
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6) }, //DDR3_1866J
    {((5<<16)|5),   ((5<<16)|5),    ((7<<16)|6) }, //DDR3_1866K
    {((6<<16)|5),   ((6<<16)|5),    ((7<<16)|6) }, //DDR3_1866L
    {((6<<16)|5),   ((6<<16)|5),    ((8<<16)|6) }, //DDR3_1866M
    
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6) }, //DDR3_2133K
    {((5<<16)|5),   ((5<<16)|5),    ((6<<16)|6) }, //DDR3_2133L
    {((5<<16)|5),   ((5<<16)|5),    ((7<<16)|6) }, //DDR3_2133M
    {((6<<16)|5),   ((6<<16)|5),    ((7<<16)|6) },  //DDR3_2133N

    {((6<<16)|5),   ((6<<16)|5),    ((8<<16)|6) } //DDR3_DEFAULT

}; 
uint32_t __sramdata ddr3_tRC_tFAW[22]={
/**    tRC    tFAW   */
    ((50<<16)|50), //DDR3_800D
    ((53<<16)|50), //DDR3_800E
    
    ((49<<16)|50), //DDR3_1066E
    ((51<<16)|50), //DDR3_1066F
    ((53<<16)|50), //DDR3_1066G
    
    ((47<<16)|45), //DDR3_1333F
    ((48<<16)|45), //DDR3_1333G
    ((50<<16)|45), //DDR3_1333H
    ((51<<16)|45), //DDR3_1333J
    
    ((45<<16)|40), //DDR3_1600G
    ((47<<16)|40), //DDR3_1600H
    ((48<<16)|40), //DDR3_1600J
    ((49<<16)|40), //DDR3_1600K
    
    ((45<<16)|35), //DDR3_1866J
    ((46<<16)|35), //DDR3_1866K
    ((47<<16)|35), //DDR3_1866L
    ((48<<16)|35), //DDR3_1866M
    
    ((44<<16)|35), //DDR3_2133K
    ((45<<16)|35), //DDR3_2133L
    ((46<<16)|35), //DDR3_2133M
    ((47<<16)|35), //DDR3_2133N
    
    ((53<<16)|50)  //DDR3_DEFAULT
};
static __sramdata uint32_t mem_type;
static __sramdata uint32_t capability;  // one chip cs capability

//DDR2
static __sramdata uint32_t tRFC;
static __sramdata uint32_t tRFPRD;
static __sramdata uint32_t tRTP;
static __sramdata uint32_t tWTR;
static __sramdata uint32_t tRAS;
static __sramdata uint32_t tRRD;
static __sramdata uint32_t tRC;
static __sramdata uint32_t tFAW;
//Mobile-DDR
static __sramdata uint32_t tXS;
static __sramdata uint32_t tXP;
//DDR3
static __sramdata uint32_t tWR;
static __sramdata uint32_t tWR_MR0;
static __sramdata uint32_t cl;
static __sramdata uint32_t cwl;

static __sramdata uint32_t cpu_freq;
static __sramdata uint32_t ddr_freq;



/****************************************************************************
Internal sram us delay function
Cpu highest frequency is 1.2 GHz
1 cycle = 1/1.2 ns
1 us = 1000 ns = 1000 * 1.2 cycles = 1200 cycles
*****************************************************************************/
//static 
void __sramlocalfunc delayus(uint32_t us)
{	
     uint32_t count;
     if(cpu_freq == 24)
         count = us * 6;//533;
     else
        count = us*200;
     while(count--)  // 3 cycles
	 	barrier();

}

static uint32_t __sramlocalfunc ddr_get_parameter(uint32_t nMHz)
{
    uint32_t tmp;
    uint32_t tmp1;
    uint32_t ret = 0;
    if(nMHz>533)
    {
        ret = -1;
        goto out;
    }
    if(mem_type == DDR3)
    {
        /* 
         * tREFI, average periodic refresh interval, 7.8us max
         */
        tRFPRD = ((59*nMHz) >> 3) & 0x3FFF;  // 62/8 = 7.75us

        if(capability <= 0x4000000)         // 512Mb 90ns
        {
            tmp = (90*nMHz+999)/1000;
        }
        else if(capability <= 0x8000000)    // 1Gb 110ns
        {
            tmp = (110*nMHz+999)/1000;
        }
        else if(capability <= 0x10000000)   // 2Gb 160ns
        {
            tmp = (160*nMHz+999)/1000;
        }
        else if(capability <= 0x20000000)   // 4Gb 300ns
        {
            tmp = (300*nMHz+999)/1000;
        }
        else    // 8Gb  350ns
        {
            tmp = (350*nMHz+999)/1000;
        }
        /*
         * tRFC DRR[7:0]
         */
        if(tmp > 0xff)
        {
            ret = -2;
            goto out;
        }
        else 
            tRFC = tmp;
        
        /*
         * tRTP = max(4nCK,7.5ns), TPR0[4:2], valid values are 2~6
         * tWTR = max(4nCK,7.5ns), TPR0[7:5], valid values are 1~6
         * clock must <533MHz, then tRTP=tWTR=4
         */
        tRTP = 4;
        tWTR = 4;

        /*
         * tRAS = 33/37.5~9*tREFI, 
         * TPR0[20:16], valid values are 2~31
         */
        tRAS = (38*nMHz+999)/1000;
            
        /*
         * tRRD = max(4nCK, 7.5ns), DDR3-1066(1K), DDR3-1333(2K), DDR3-1600(2K)
         *        max(4nCK, 10ns), DDR3-800(1K,2K), DDR3-1066(2K)
         *        max(4nCK, 6ns), DDR3-1333(1K), DDR3-1600(1K)
         *  
         * TPR0[24:21], valid values are 1~8
         */
        tRRD = (10*nMHz+999)/1000;
        
        /*
         * tRC  TPR0[30:25], valid values are 2~42
         */
        tRC = ((ddr3_tRC_tFAW[DDR3_TYPE]>>16)*nMHz+999)/1000;
        
        /*
         * tFAW TPR1[8:3], valid values are 2~31
         */
        tFAW = ((ddr3_tRC_tFAW[DDR3_TYPE]&0x0ff)*nMHz+999)/1000;
        
        /*
         * tXS TPR2[9:0], valid values are 2~1023
         * MAX(tXS, tXSDLL) for DDR3, tXSDLL = tDLLK = 512CK
         */
        tXS = 512;
        
        /*
         *       max(tXP, tXPDLL) for DDR3
         * tXP = max(3nCK, 7.5ns), DDR3-800, DDR3-1066
         *       max(3nCK, 6ns), DDR3-1333, DDR3-1600, DDR3-1866, DDR3-2133
         * tXPDLL = max(10nCK, 24ns)
         * TPR2[14:10], valid values are 2~31
         */
        if(nMHz <= 330)
        {
            tmp = 0;
            tXP = 10;
        }
        else if(nMHz<=400)
        {
            tmp = 1;
            tXP = 10;
        }
        else    // 533 MHz
        {
            tmp = 2;
            tXP = 13;
        }
        
        /*
         * tWR TPR3[14:11], valid values are 2~12, 15ns
         */
        tWR = (15*nMHz+999)/1000;
        if(tWR<9)
            tWR_MR0 = tWR - 4;
        else
            tWR_MR0 = tWR>>1;

        cl = ddr3_cl_cwl[DDR3_TYPE][tmp] >> 16;
        cwl = ddr3_cl_cwl[DDR3_TYPE][tmp] & 0x0ff; 
    }
    else if(mem_type == DDRII)
    {
        /* 
         * tREFI, average periodic refresh interval, 7.8us max
         */
        tRFPRD = ((59*nMHz) >> 3) & 0x3FFF;  // 62/8 = 7.75us
        
        if(capability <= 0x2000000)  // 256Mb
        {
            tmp = (75*nMHz/1000) + ((((75*nMHz)%1000) > 0) ? 1:0);
        }
        else if(capability <= 0x4000000) // 512Mb
        {
            tmp = (105*nMHz/1000) + ((((105*nMHz)%1000) > 0) ? 1:0);
        }
        else if(capability <= 0x8000000)  // 1Gb
        {
            tmp = (128*nMHz/1000) + ((((128*nMHz)%1000) > 0) ? 1:0);
        }
        else if(capability <= 0x10000000)  // 2Gb
        {
        		tmp = (195*nMHz/1000) + ((((195*nMHz)%1000) > 0) ? 1:0);
        }
        else  // 4Gb
        {
            tmp = (328*nMHz/1000) + ((((328*nMHz)%1000) > 0) ? 1:0);
        }
        //tRFC = 75ns(256Mb)/105ns(512Mb)/127.5ns(1Gb)/195ns(2Gb)/327.5ns(4Gb)
        tmp = (tmp > 0xFF) ? 0xFF : tmp;
        tRFC = tmp;
        // tRTP = 7.5ns
        tmp = (8*nMHz/1000) + ((((8*nMHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 6) ? 6 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRTP = tmp;
        //tWTR = 10ns(DDR2-400), 7.5ns (DDR2-533/667/800)
        if(nMHz <= 200)
        {
            tmp = (10*nMHz/1000) + ((((10*nMHz)%1000) > 0) ? 1:0);
        }
        else
        {
            tmp = (8*nMHz/1000) + ((((8*nMHz)%1000) > 0) ? 1:0);
        }
        tmp = (tmp > 6) ? 6 : tmp;
        tmp = (tmp < 1) ? 1 : tmp;
        tWTR = tmp;
        //tRAS_min = 45ns
        tmp = (45*nMHz/1000) + ((((45*nMHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 31) ? 31 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRAS = tmp;
        // tRRD = 10ns
        tmp = (10*nMHz/1000) + ((((10*nMHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 8) ? 8 : tmp;
        tmp = (tmp < 1) ? 1 : tmp;
        tRRD = tmp;
        if(nMHz <= 200)  //tRC = 65ns
        {
            tmp = (65*nMHz/1000) + ((((65*nMHz)%1000) > 0) ? 1:0);
        }
        else //tRC = 60ns
        {
            tmp = (60*nMHz/1000) + ((((60*nMHz)%1000) > 0) ? 1:0);
        }
        tmp = (tmp > 42) ? 42 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRC = tmp;
        //tFAW = 50ns
        tmp = (50*nMHz/1000) + ((((50*nMHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 31) ? 31 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tFAW = tmp;
        if(nMHz <= 333)
        {
            tXP = 7;
        }
        else
        {
            tXP = 8;
        }
	// don't need to modify tXS, tWR, tWR_MR0, cwl
	
        if(nMHz <= 266)
        {
            cl =  4;
        }
        else if((nMHz > 266) && (nMHz <= 333))
        {
            cl =  5;
        }
        else if((nMHz > 333) && (nMHz <= 400))
        {
            cl =  6;
        }
        else // > 400MHz
        {
            cl =  7;
        } 
        cwl = cl -1;
    }
    else
    {
        /*
         * mobile DDR timing USE 3-3-3
         */
        
        /* 
         * tREFI, average periodic refresh interval, 7.8us max
         */
        tRFPRD = ((59*nMHz) >> 3) & 0x3FFF;  // 62/8 = 7.75us

        /*
         *tRFC = 80ns(128Mb,256Mb)
         *       110ns(512Mb)
         *       140ns(1Gb,2Gb)
         */
        if(capability <= 0x2000000)  // 256Mb
        {
            tmp = (80*nMHz/1000) + ((((80*nMHz)%1000) > 0) ? 1:0);
        }
        else if(capability <= 0x4000000) // 512Mb
        {
            tmp = (110*nMHz/1000) + ((((110*nMHz)%1000) > 0) ? 1:0);
        }
        else  // 1Gb,2Gb
        {
            tmp = (140*nMHz/1000) + ((((140*nMHz)%1000) > 0) ? 1:0);
        }
        tmp = (tmp > 0xFF) ? 0xFF : tmp;
        tRFC = tmp;
        /* 
         * tWTR = 1 tCK(100MHz,133MHz)
         *        2 tCK(166MHz,185MHz)
         */
        if(nMHz < 133)
        {
            tWTR = 1;
        }
        else
        {
            tWTR = 2;
        }
        /* 
         * tRAS = 50ns(100MHz)
         *        45ns(133MHz)
         *        42ns(166MHz)
         *        42ns(185MHz)
         *        40ns(200MHz)
         */
        if(nMHz<100)
        {
            tmp = (50*nMHz/1000) + ((((50*nMHz)%1000) > 0) ? 1:0);
        }
        else if(nMHz<133)
        {
            tmp = (45*nMHz/1000) + ((((45*nMHz)%1000) > 0) ? 1:0);
        }
        else if(nMHz<185)
        {
            tmp = (42*nMHz/1000) + ((((42*nMHz)%1000) > 0) ? 1:0);
        }
        else
        {
            tmp = (40*nMHz/1000) + ((((40*nMHz)%1000) > 0) ? 1:0);
        }
        tmp1 = tmp;
        tmp = (tmp > 31) ? 31 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRAS = tmp;
        /*
         * tRC = tRAS + tRP
         */
        tmp1 += 3;
        tmp1 = (tmp1 > 42) ? 42 : tmp1;
        tmp1 = (tmp1 < 2) ? 2 : tmp1;
        tRC = tmp1;
        /* 
         * tRRD = 15ns(100MHz)
         *        15ns(133MHz)
         *        12ns(166MHz)
         *        10.8ns(185MHz)
         *        10ns(200MHz)
         */
        if(nMHz<133)
        {
            tmp = (15*nMHz/1000) + ((((15*nMHz)%1000) > 0) ? 1:0);
        }
        else if(nMHz<166)
        {
            tmp = (12*nMHz/1000) + ((((12*nMHz)%1000) > 0) ? 1:0);
        }
        else if(nMHz<185)
        {
            tmp = (11*nMHz/1000) + ((((11*nMHz)%1000) > 0) ? 1:0);
        }
        else
        {
            tmp = (10*nMHz/1000) + ((((10*nMHz)%1000) > 0) ? 1:0);
        }
        tmp = (tmp > 8) ? 8 : tmp;
        tmp = (tmp < 1) ? 1 : tmp;
        tRRD = tmp;
        /*
         * tXS = tXSR = 200ns
         */
        tmp = (200*nMHz/1000) + ((((200*nMHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 0x3FF) ? 0x3FF : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tXS = tmp;
        /*
         * tXP = 25ns
         */
        tmp = (25*nMHz/1000) + ((((25*nMHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 0x1F) ? 0x1F : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tXP = tmp;
        
        /*
         * mobile DDR CL always = 3
         */
        cl = 3;
    }
out:
    return ret;
}
static uint32_t __sramlocalfunc ddr_update_timing(void)
{
    uint32_t value;

    pDDR_Reg->DRR = TRFC(tRFC) | TRFPRD(tRFPRD) | RFBURST(1);
    if(mem_type == DDR3)
    {
        value = pDDR_Reg->TPR[0];
        value &= ~((0x7<<2)|(0x7<<5)|(0x1F<<16)|(0xF<<21)|(0x3F<<25));
        pDDR_Reg->TPR[0] = value | TRTP(tRTP) | TWTR(tWTR) | TRAS(tRAS) | TRRD(tRRD) | TRC(tRC);
        value = pDDR_Reg->TPR[1];
        value &= ~(0x3F<<3);
        pDDR_Reg->TPR[1] = value | TFAW(tFAW);
        // ddr3 tCKE should be tCKESR=tCKE+1nCK
        pDDR_Reg->TPR[2] = TXS(tXS) | TXP(tXP) | TCKE(4);//0x198c8;//       
        
    }
    else if(mem_type == DDRII)
    {
        value = pDDR_Reg->TPR[0];
        value &= ~((0x7<<2)|(0x7<<5)|(0x1F<<16)|(0xF<<21)|(0x3F<<25));
        pDDR_Reg->TPR[0] = value | TRTP(tRTP) | TWTR(tWTR) | TRAS(tRAS) | TRRD(tRRD) | TRC(tRC);
        value = pDDR_Reg->TPR[1];
        value &= ~(0x3F<<3);
        pDDR_Reg->TPR[1] = value | TFAW(tFAW);
        value = pDDR_Reg->TPR[2];
        value &= ~(0x1F<<10);
        pDDR_Reg->TPR[2] = value | TXP(tXP);
    }
    else
    {
        /*
         * mobile DDR timing USE 3-3-3
         */
        value = pDDR_Reg->TPR[0];
        value &= ~((0x7<<2)|(0x7<<5)|(0x1F<<16)|(0xF<<21)|(0x3F<<25));
        pDDR_Reg->TPR[0] = value | TRTP(2) | TWTR(tWTR) | TRAS(tRAS) | TRRD(tRRD) | TRC(tRC);
        pDDR_Reg->TPR[2] = TXS(tXS) | TXP(tXP) | TCKE(2);
    }
    return 0;
}

static uint32_t __sramlocalfunc ddr_update_mr(void)
{
    uint32_t value;
    value = pDDR_Reg->TPR[0];
    value &= ~(0x0FF<<8);
    pDDR_Reg->TPR[0] = value | TRP(cl) | TRCD(cl);
    value = pDDR_Reg->TPR3;
    value &= ~((0xF<<3)|(0xF<<7)|(0xF<<11));
	
    if(mem_type == DDR3)
    {
        pDDR_Reg->TPR3 = value | CL(cl) | CWL(cwl) | WR(tWR);
        pDDR_Reg->MR = DDR3_BL8 | DDR3_CL(cl) | DDR3_MR0_WR(tWR_MR0)/*15 ns*/;
        delayus(1);
        pDDR_Reg->EMR2 = DDR3_MR2_CWL(cwl);
    }
    else if(mem_type == DDRII)
    {
        pDDR_Reg->TPR3 = value | CL(cl) | CWL(cwl) | WR(cl);
        //set mode register cl
        value = pDDR_Reg->MR;
        value &= ~((0x7<<4)|(0x7<<9));
        pDDR_Reg->MR = value | DDR_CL(cl) | DDR2_WR(cl);       
    }
    else
    {
        /*
         * mobile DDR CL always = 3
         */
    }
    return 0;
}
static __sramdata uint32_t clkr;
static __sramdata uint32_t clkf;
static __sramdata uint32_t clkod;
static __sramdata uint32_t pllband = 0;
static uint32_t __sramlocalfunc ddr_set_pll(uint32_t nMHz, uint32_t set)
{
    uint32_t ret = 0;
    uint32_t temp;
    
    if(nMHz == 24)
    {
        ret = 24;
        goto out;
    }
    
    if(!set)
    {
        pllband = 0;
        if (nMHz<38)
        {
            nMHz = 38;
            clkr = 1;
            clkod = 3;
        }
        else if(nMHz <= 75 )
        {
            clkr = 1;
            clkod = 3;
        }
        else if (nMHz <= 150)
        {
            clkr = 2;
            clkod = 2;
        }
        else if (nMHz <= 300)
        {
            clkr = 2;
            clkod = 1;
        }
        else if(nMHz <= 600)
        {
            clkr = 2;
            clkod = 0;
        }
        else
        {
            clkr = 2;
            clkod = 0;
            pllband = (0x01u<<16);
        }
        temp = nMHz*clkr*(1<<clkod);
        clkf = temp/24;
        //if(temp%24)
        //    clkf += 1;
        ret = ((24*clkf)>>(clkr-1))>>clkod;
    }
    else
    {
         // ddr slow
        pSCU_Reg->CRU_MODE_CON &=~(0x3<<6);
        
        pSCU_Reg->CRU_DPLL_CON |= (0x1 << 15); //power down pll
        delayus(1);  //delay at least 500ns
        pSCU_Reg->CRU_DPLL_CON = pllband | (0x1<<15) |(PLL_CLKR(clkr))|(PLL_CLKF(clkf))|(PLL_CLKOD(clkod));

        delayus(1);  //delay at least 500ns
        pSCU_Reg->CRU_DPLL_CON &= ~(0x1<<15);
        delayus(2000); // 7.2us*140=1.008ms

        // ddr pll normal
        pSCU_Reg->CRU_MODE_CON |= 0x1<<6;

        // ddr_pll_clk: clk_ddr=1:1 	
        temp = pSCU_Reg->CRU_CLKSEL_CON[7];
        temp &= ~(0x1F<<24);
        pSCU_Reg->CRU_CLKSEL_CON[7] = temp;
        delayus(1);
    }
out:
    return ret;
}


void __sramlocalfunc ddr_selfrefresh_enter(void)
{
    /* 1. disables all host ports
       2. Flushes the MCTL pipelines (including current automatically-scheduled refreshes)
       3. Disables automatic scheduling of refreshes
       4. Issues a Precharge All command
       5. Waits for tRP clocks (programmable in TPR0 register)
       6. Issues a Self-Refresh command
       7. Clears the self-clearing bit DCR.EXE and waits for Mode Exit command
     */
    
    pDDR_Reg->CCR &= ~HOSTEN;  //disable host port
    //pDDR_Reg->CCR |= FLUSH;    //flush
    delayus(1);
    pDDR_Reg->DCR = (pDDR_Reg->DCR & (~((0x1<<24) | (0x1<<13) | (0xF<<27) | (0x1<<31)))) | ((0x1<<13) | (0x2<<27) | (0x1<<31));  //enter Self Refresh
    delayus(10); 
    pSCU_Reg->CRU_SOFTRST_CON[0] |= (0x1F<<19);  //reset DLL
    pDDR_Reg->CCR |= ITMRST;   //ITM reset
    delayus(1);
}
void __sramlocalfunc ddr_selfrefresh_exit(void)
{
    /* 1. Exits self-refresh when a Mode-Exit command is received
       2. Waits for tXS clocks (programmable in TPR2 register)
       3. Issues a Refresh command
       4. Waits for tRFC clocks (programmable in DRR)
       5. Clears the self-clearing bit DCR.EXE
       6. Re-enables all host ports
     */
    delayus(10); 
    pSCU_Reg->CRU_SOFTRST_CON[0] &= ~(0x1F<<19);
    pDDR_Reg->CCR &= ~ITMRST;   //ITM reset
    delayus(500); 
    pDDR_Reg->DCR = (pDDR_Reg->DCR & (~((0x1<<24) | (0x1<<13) | (0xF<<27) | (0x1<<31)))) | ((0x1<<13) | (0x7<<27) | (0x1<<31)); //exit    
    delayus(10);
    ddr_update_mr();
    delayus(1);

refresh:
    pDDR_Reg->DRR |= RD;
    delayus(1);
    pDDR_Reg->CCR |= DTT;
    delayus(15);
    pDDR_Reg->DRR = TRFC(tRFC) | TRFPRD(tRFPRD) | RFBURST(8);
    delayus(10);
    pDDR_Reg->DRR = TRFC(tRFC) | TRFPRD(tRFPRD) | RFBURST(1);
    delayus(1);
    if(pDDR_Reg->CSR & 0x100000)
    {
        pDDR_Reg->CSR &= ~0x100000;
        goto refresh;
    }
    pDDR_Reg->CCR |= HOSTEN;  //enable host port
}

uint32_t __sramfunc ddr_change_freq(uint32_t nMHz)
{
    uint32_t ret;
	volatile u32 n;	
    unsigned long flags;
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;

    if((pSCU_Reg->CRU_MODE_CON & 0x03) == 0x03)
        cpu_freq = 24;
    else 
        cpu_freq = clk_get_rate(clk_get(NULL,"core"))/1000000;
    
    ret = ddr_set_pll(nMHz, 0);
    ddr_get_parameter(ret);
    /** 1. Make sure there is no host access */
#if 1
    local_irq_save(flags);
    flush_cache_all();
    __cpuc_flush_kern_all();
    __cpuc_flush_user_all();
    DDR_SAVE_SP(save_sp);
    n=temp[0];
    barrier();
    n=temp[1024];
    barrier();
    n=temp[1024*2];
    barrier();
    n=temp[1024*3];
    barrier();
    n= pDDR_Reg->CCR;
    n= pSCU_Reg->CRU_SOFTRST_CON[0];
    dsb();
#endif
    
    /** 2. ddr enter self-refresh mode or precharge power-down mode */
    ddr_selfrefresh_enter();
    delayus(10);
    /** 3. change frequence  */
    ddr_set_pll(ret, 1);
    ddr_freq = ret;
    #if 1
    if(nMHz<100)
    {
        pDDR_Reg->DLLCR09[0] |= (0x1<<31);
        pDDR_Reg->DLLCR09[1] |= (0x1<<31);
        pDDR_Reg->DLLCR09[2] |= (0x1<<31);
        pDDR_Reg->DLLCR09[3] |= (0x1<<31);
        pDDR_Reg->DLLCR09[9] |= (0x1<<31);
        pDDR_Reg->DLLCR &= ~(0x1<<23);
    }
    else if(nMHz<=200)
    {
        pDDR_Reg->DLLCR09[0] |= (0x1<<31);
        pDDR_Reg->DLLCR09[1] |= (0x1<<31);
        pDDR_Reg->DLLCR09[2] |= (0x1<<31);
        pDDR_Reg->DLLCR09[3] |= (0x1<<31);
        pDDR_Reg->DLLCR09[9] |= (0x1<<31);
        pDDR_Reg->DLLCR |= (0x1<<23);
    }
    else
    {
        pDDR_Reg->DLLCR09[0] &= ~(0x1<<31);
        pDDR_Reg->DLLCR09[1] &= ~(0x1<<31);
        pDDR_Reg->DLLCR09[2] &= ~(0x1<<31);
        pDDR_Reg->DLLCR09[3] &= ~(0x1<<31);
        pDDR_Reg->DLLCR09[9] &= ~(0x1<<31);
        pDDR_Reg->DLLCR &= ~(0x1<<23);
    }
    #endif

    /** 4. update timing&parameter  */
    ddr_update_timing();

    /** 5. Issues a Mode Exit command   */
    ddr_selfrefresh_exit();
	dsb(); 
    
    DDR_RESTORE_SP(save_sp);
    local_irq_restore(flags);
    return ret;
}
void __sramfunc ddr_suspend(void)
{
    uint32_t n;
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;

    cpu_freq = 24;
	
    /** 1. Make sure there is no host access */
    flush_cache_all();
    __cpuc_flush_kern_all();
    __cpuc_flush_user_all();

    n=temp[0];
    barrier();
    n=temp[1024];
    barrier();
    n=temp[1024*2];
    barrier();
    n=temp[1024*3];
    barrier();
    n= pDDR_Reg->CCR;
    n= pSCU_Reg->CRU_SOFTRST_CON[0];
    dsb();

    ddr_selfrefresh_enter();

    pSCU_Reg->CRU_CLKGATE_CON[3] |= (0x1<<16)   //close DDR GPU AXI clock
                                    | (0x1<<13) //close DDR VDPU AXI clock
                                    | (0x1<<11) //close DDR VEPU AXI clock
                                    | (0x1<<1); //close DDR LCDC AXI clock
    pSCU_Reg->CRU_CLKGATE_CON[1] |= (0x1<<6);   //close DDR PERIPH AXI clock
    pSCU_Reg->CRU_CLKGATE_CON[0] |= (0x1<<18);  //close DDR PHY clock
    
    delayus(1);
    // ddr slow
    pSCU_Reg->CRU_MODE_CON &=~(0x3<<6);
    delayus(1);	
    pSCU_Reg->CRU_DPLL_CON |= (0x1 << 15);  //power down DPLL
    delayus(1);  //delay at least 500ns
    
    pDDR_Reg->DLLCR09[0] |= (0x1<<31);
    pDDR_Reg->DLLCR09[1] |= (0x1<<31);
    pDDR_Reg->DLLCR09[2] |= (0x1<<31);
    pDDR_Reg->DLLCR09[3] |= (0x1<<31);
    pDDR_Reg->DLLCR09[9] |= (0x1<<31);
    pDDR_Reg->DLLCR &= ~(0x1<<23);
    
}
void __sramfunc ddr_resume(void)
{
    uint32_t value;
 
    if(ddr_freq<100)
    {
        pDDR_Reg->DLLCR09[0] |= (0x1<<31);
        pDDR_Reg->DLLCR09[1] |= (0x1<<31);
        pDDR_Reg->DLLCR09[2] |= (0x1<<31);
        pDDR_Reg->DLLCR09[3] |= (0x1<<31);
        pDDR_Reg->DLLCR09[9] |= (0x1<<31);
        pDDR_Reg->DLLCR &= ~(0x1<<23);
    }
    else if(ddr_freq<=200)
    {
        pDDR_Reg->DLLCR09[0] |= (0x1<<31);
        pDDR_Reg->DLLCR09[1] |= (0x1<<31);
        pDDR_Reg->DLLCR09[2] |= (0x1<<31);
        pDDR_Reg->DLLCR09[3] |= (0x1<<31);
        pDDR_Reg->DLLCR09[9] |= (0x1<<31);
        pDDR_Reg->DLLCR |= (0x1<<23);
    }
    else
    {
        pDDR_Reg->DLLCR09[0] &= ~(0x1<<31);
        pDDR_Reg->DLLCR09[1] &= ~(0x1<<31);
        pDDR_Reg->DLLCR09[2] &= ~(0x1<<31);
        pDDR_Reg->DLLCR09[3] &= ~(0x1<<31);
        pDDR_Reg->DLLCR09[9] &= ~(0x1<<31);
        pDDR_Reg->DLLCR &= ~(0x1<<23);
    }
    delayus(1);
    
#if 1
     pSCU_Reg->CRU_DPLL_CON &= ~(0x1 << 15);  //power on DPLL   
    //   while(!(pGRF_Reg->GRF_SOC_CON[0] & (1<<28)));
     delayus(500); // 7.2us*140=1.008ms // Ëø¶¨pll
    // ddr pll normal
    value = pSCU_Reg->CRU_MODE_CON;
    value &=~(0x3<<6);
    value |= 0x1<<6;
    pSCU_Reg->CRU_MODE_CON = value;
    delayus(1);    
    pSCU_Reg->CRU_CLKGATE_CON[3] &= ~((0x1<<16)     //close DDR GPU AXI clock
                                    | (0x1<<13)     //close DDR VDPU AXI clock
                                    | (0x1<<11)     //close DDR VEPU AXI clock
                                    | (0x1<<1));    //close DDR LCDC AXI clock
    pSCU_Reg->CRU_CLKGATE_CON[1] &= ~(0x1<<6);      //close DDR PERIPH AXI clock
    pSCU_Reg->CRU_CLKGATE_CON[0] &= ~(0x1<<18);     //enable DDR PHY clock
    delayus(1);   
#endif

    ddr_selfrefresh_exit();
	dsb(); 
}
static void inline ddr_change_host_priority(void)
{
    /*
        DMC AXI host N priority
        00:higheset priority
        01:second high priority
        10:third high priority
        
        GRF_MEM_CON[1:0]: CPU       (host 0)
                   [3:2]: PERI      (host 1)
                   [5:4]: DISPLAY   (host 2)
                   [7:6]: GPU       (host 3)
                   [9:8]: VCODEC    (host 4)
    */
    pGRF_Reg->GRF_MEM_CON = (pGRF_Reg->GRF_MEM_CON & ~0x3FF) | ((2<<0)|(1<<2)|(0<<4)|(1<<6)|(2<<8));
}

static int __init ddr_probe(void)
{
    volatile uint32_t value = 0;
	uint32_t          addr;
    uint32_t          bw;
    uint32_t          col = 0;
    uint32_t          row = 0;
    uint32_t          bank = 0;
    uint32_t          n;

    ddr_print("version 1.00 20110407 \n");

    mem_type = (pDDR_Reg->DCR & 0x3);

    // caculate aglined physical address 
    addr =  __pa((unsigned long)ddrDataTraining);
    if(addr&0x1F)
    {
        addr += (32-(addr&0x1F));
    }
    addr -= 0x60000000;
    // caculate data width
    bw = ((((pDDR_Reg->DCR >> 7) & 0x7)+1)>>1);
    // find out col£¬row£¬bank
    for(n=0;n<10; n++)
    {
        if(ddrConfig[(pDDR_Reg->DCR & 0x3)][n].config == (pDDR_Reg->DCR & 0x7c))
        {
            col = ddrConfig[(pDDR_Reg->DCR & 0x3)][n].col;
            row = ddrConfig[(pDDR_Reg->DCR & 0x3)][n].row;
            bank = ddrConfig[(pDDR_Reg->DCR & 0x3)][n].bank;
            bank >>= 2;  // 8=>3, 4=>2
            bank += 1;
            break;
        }
    }
    if(n == 10)
    {
        //ASSERT
    }

    // according different address mapping, caculate DTAR register value
    value = pDDR_Reg->DTAR;
    value &= ~(0x7FFFFFFF);
    switch(pDDR_Reg->DCR & (0x3<<14))
    {
        case AMAP_RBRC:
            value |= (addr>>bw) & ((0x1<<col)-1);  // col
            value |= ((addr>>(bw+col)) & ((0x1<<row)-1)) << 12;  // row
            value |= ((addr>>(bw+col+row)) & ((0x1<<bank)-1)) << 28;  // bank
            break;
        case AMAP_RRBC:
            value |= (addr>>bw) & ((0x1<<col)-1);  // col
            value |= ((addr>>(bw+col+bank)) & ((0x1<<row)-1)) << 12;  // row
            value |= ((addr>>(bw+col)) & ((0x1<<bank)-1)) << 28;  // bank
            break;
        case AMAP_BRRC:
            value |= (addr>>bw) & ((0x1<<col)-1);  // col
            if((pDDR_Reg->DCR >> 11) & 0x3)
            {
                value |= ((addr>>(bw+col+1)) & ((0x1<<row)-1)) << 12;  // row
                value |= ((addr>>(bw+col+row+1)) & ((0x1<<bank)-1)) << 28;  // bank
            }
            else
            {
                value |= ((addr>>(bw+col)) & ((0x1<<row)-1)) << 12;  // row
                value |= ((addr>>(bw+col+row)) & ((0x1<<bank)-1)) << 28;  // bank
            }
            break;
        case AMAP_FIX:    // can not support AMAP_FIX mode 
        default:
            break;
    }
    pDDR_Reg->DTAR = value;

    if((mem_type == DDRII) || (mem_type == DDR3))
    {
        pDDR_Reg->ALPMR = LPPERIOD_POWER_DOWN(0xFF) | AUTOPD;
    }
    else
    {
        pDDR_Reg->ALPMR = LPPERIOD_CLK_STOP(0xFF) | LPPERIOD_POWER_DOWN(0xFF) | AUTOCS | AUTOPD;
    }
    ddr_change_host_priority();
    //get capability per chip, not total size, used for calculate tRFC
    capability = 0x800000 << ((pDDR_Reg->DCR >> 4) & 0x7);
    value = ((((pDDR_Reg->DCR >> 7) & 0x7)+1) >> ((pDDR_Reg->DCR >> 2) & 0x3))+((pDDR_Reg->DCR >> 11) & 0x3);
    if(mem_type == DDR3)
    {
        ddr_print("DDR3 Device\n");
    }
    else if(mem_type == DDRII)
    {
        ddr_print("DDR2 Device\n");
    }
    else
    {
        ddr_print("LPDDR Device\n");
    }
    ddr_print("%d CS, ROW=%d, Bank=%d, COL=%d, Total Capability=%dMB\n", 
                (((pDDR_Reg->DCR >> 11) & 0x3) + 1), \
                 row, (0x1<<bank), col, \
                 ((capability<<value)>>20));

    value = ddr_change_freq(DDR_FREQ);
    ddr_print("init success!!! freq=%dMHz\n", value);
    ddr_print("CSR:0x%x, RSLR0:0x%x, RSLR1:0x%x, RDGR0:0x%x, RDGR1:0x%x\n", 
                        pDDR_Reg->CSR, 
                        pDDR_Reg->RSLR[0], pDDR_Reg->RSLR[1], 
                        pDDR_Reg->RDGR[0], pDDR_Reg->RDGR[1]);
    return 0;
}
core_initcall_sync(ddr_probe);
