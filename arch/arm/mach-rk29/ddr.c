/*
 * arch/arm/mach-rk29/ddr.c
 *
 * Function Driver for DDR controller
 *
 * Copyright (C) 2011 Fuzhou Rockchip Electronics Co.,Ltd
 * Author: 
 * hcy@rock-chips.com
 * yk@rock-chips.com
 * v2.01 
 * disable DFTCMP
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

#define DDR_BYPASS_EN  1

#define ddr_print(x...) printk( "DDR DEBUG: " x )

//CCR;                    //Controller Configuration Register                            
#define ECCEN                           	(1)
#define NOMRWR                          	(1<<1)
#define HOSTEN                          	(1<<2)
#define RRB                             		(1<<13)
#define DQSCFG                          	(1<<14)
#define DFTLM_NO                        (0<<15)
#define DFTLM_90                        (1<<15)
#define DFTLM_180                       (2<<15)
#define DFTLM_270                       (3<<15)
#define DFTCMP                          	(1<<17)
#define FLUSH                           	(1<<27)
#define ITMRST                          	(1<<28)
#define IB                              		(1<<29)
#define DTT                             		(1<<30)
#define IT                              		(1<<31)
//DCR;                    //DRAM Configuration Register                                  
#define DDRII                           		(0)
#define DDR3                            		(1)
#define Mobile_DDR                      	(2)
                                        
#define DIO_8                           	(1<<2)
#define DIO_16                          	(2<<2)
#define DIO_32                          	(3<<2)
                                        
#define DSIZE_64Mb                      	(0<<4)
#define DSIZE_128Mb                     	(1<<4)
#define DSIZE_256Mb                     	(2<<4)
#define DSIZE_512Mb                     	(3<<4)
#define DSIZE_1Gb                       	(4<<4)
#define DSIZE_2Gb                       	(5<<4)
#define DSIZE_4Gb                       	(6<<4)
#define DSIZE_8Gb                       	(7<<4)
                                        
#define SIO(n)                          (((n)-1)<<7)
#define PIO                             		(1<<10)
#define RANKS(n)                        ((((n)-1)&0x3)<<11)
#define RNKALL                          	(1<<13)
                                        
#define AMAP_RBRC                       	(0<<14)  //rank,bank,row,column
#define AMAP_RRBC                       	(1<<14)  //rank,row,bank,column
#define AMAP_BRRC                       	(2<<14)  //bank,row,rank,column
#define AMAP_FIX                        	(3<<14)  //Fixed address

#define PDQ(n)                          (((n)&0x7)<<16)
#define MPRDQ                           	(1<<19)
#define MVAR                            		(1<<20)
#define RDIMM                           	(1<<21)
#define DO_INIT                         	(1<<24)
#define EXE_RANK(n)                     (((n)&3)<<25)

#define CMD_NOP                         	(0<<27)
#define CMD_ClockStop                   	(1<<27)
#define CMD_SelfRefresh                	(2<<27)
#define CMD_Refresh                     	(3<<27)
#define CMD_DDR3_Reset                (4<<27)
#define CMD_PrechargeAll              	(5<<27)
#define CMD_DeepPowerDown               			(6<<27)
#define CMD_SDRAM_Mode_Exit             		(7<<27)
#define CMD_SDRAM_ZQ_Calibration_Short  	(0xB<<27)
#define CMD_SDRAM_ZQ_Calibration_Long   	(0xC<<27)
#define CMD_PowerDown                   			(0xE<<27)
#define CMD_SDRAM_NOP                   			(0xF<<27)

#define EXE                             		(1<<31)

//IOCR;                   //IO Configuration Register   
#define DQ_ODT                          	(1)
#define DQS_ODT                         	(1<<1)
#define TESTEN                          	(1<<2)
#define DQ_RTT_DIS                      	(0<<3)
#define DQ_RTT_150                      	(1<<3)
#define DQ_RTT_75                       	(2<<3)
#define DQ_RTT_50                       	(3<<3)
#define DQS_RTT_DIS                     	(0<<5)
#define DQS_RTT_150                     	(1<<5)
#define DQS_RTT_75                      	(2<<5)
#define DQS_RTT_50                      	(3<<5)
#define DQ_DS_FULL                      	(1<<7)
#define DQ_DS_REDUCE                   (0<<7)
#define DQS_DS_FULL                     	(1<<8)
#define DQS_DS_REDUCE                	(0<<8)
#define ADD_DS_FULL                     	(1<<9)
#define ADD_DS_REDUCE                	(0<<9)
#define CK_DS_FULL                      	(1<<10)
#define CK_DS_REDUCE                   	(0<<10)

#define AUTO_CMD_IOPD(n)                (((n)&0x3)<<18)
#define AUTO_DATA_IOPD(n)               (((n)&0x3)<<22)
#define RTTOH(n)                        (((n)&0x7)<<26)
#define RTTOE                           	(1<<29)
#define DQRTT                           	(1<<30)
#define DQSRTT                         	(1<<31)

//CSR;                    //Controller Status Register   
#define DFTERR                          	(1<<18)
#define ECCERR                          	(1<<19)
#define DTERR                           	(1<<20)
#define DTIERR                          	(1<<21)
#define ECCSEC                          	(1<<22)
#define TQ                              		(1<<23)

//DRR;                    //DRAM Refresh Register       
#define TRFC(n)                         ((n)&0xFF)
#define TRFPRD(n)                       (((n)&0xFFFF)<<8)
#define RFBURST(n)                      ((((n)-1)&0xF)<<24)
#define RD                              		(1<<31)

//TPR[3];                 //SDRAM Timing Parameters Registers   
//TPR0
#define TMRD(n)                         ((n)&0x3)
#define TRTP(n)                         (((n)&0x7)<<2)
#define TWTR(n)                         (((n)&0x7)<<5)
#define TRP(n)                          (((n)&0xF)<<8)
#define TRCD(n)                         (((n)&0xF)<<12)
#define TRAS(n)                         (((n)&0x1F)<<16)
#define TRRD(n)                         (((n)&0xF)<<21)
#define TRC(n)                          (((n)&0x3F)<<25)
#define TCCD                            		(1<<31)
//TPR1                                  
#define TAOND_2                         	(0)
#define TAOND_3                         	(1)
#define TAOND_4                         	(2)
#define TAOND_5                         	(3)
#define TRTW                            	(1<<2)
#define TFAW(n)                         (((n)&0x3F)<<3)
#define TMOD(n)                         (((n)&0x3)<<9)
#define TRTODT                          (1<<11)
#define TRNKRTR(n)                      ((((n)-1)&0x3)<<12)
#define TRNKWTW(n)                      (((n)&0x3)<<14)
//TPR2                                  
#define TXS(n)                          ((n)&0x3FF)
#define TXP(n)                          (((n)&0x1F)<<10)
#define TCKE(n)                         (((n)&0xF)<<15)

//DLLCR;                  //Global DLL Control  Register                                 
//DLLCR09[10];            //DDR Control  Register 0-9                                    
#define DD                              		(1<<31)

//RSLR[4];                //Rank System Latency  Register 0-3                            
#define SL(n,value)                     (((value)&0x7)<<((n)*3))

//RDGR[4];                //Rank DQS Gating  Register 0-3                                
#define DQSSEL(n, value)                (((value)&0x3)<<((n)*2))

//DQTR[9];                //DQ Timing  Register 0-8    
#define DQDLY_DQS(n, value)             (((value)&0x3)<<((n)*4))
#define DQDLY_DQSb(n, value)            (((value)&0x3)<<(((n)*4)+2))

//DQSTR;                  //DQS Timing  Register                                         
//DQSBTR;                 //DQS_b Timing  Register                                       
#define DQSDLY(n, value)                (((value)&0x7)<<((n)*3))

//ODTCR;                  //ODT Configuration  Register                                  
#define RDODT(rank, value)              (((value)&0xF)<<((rank)*4))
#define WRODT(rank, value)              (((value)&0xF)<<(((rank)*4)+16))

//DTR[2];                 //Data Training Register 0-1                                   
//DTAR;                   //Data Training Address  Register                              
#define DTCOL(col)                      ((col)&0xFFF)
#define DTROW(row)                      (((row)&0xFFFF)<<12)
#define DTBANK(bank)                    (((bank)&0x7)<<28)
#define DTMPR                           	(1<<31)

//ZQCR[3];                //SDRAM ZQ Control Register and SDRAM ZQCS Control Register 0-2
//ZQCR0
#define ZQDATA(n)                       ((n)&0xFFFFF)
#define ZPROG_OUT(n)                    (((n)&0xF)<<20)
#define ZPROG_ODT(n)                    (((n)&0xF)<<24)
#define ZQDEN                           	(1<<28)
#define ZQCLK                           	(1<<29)
#define NOICAL                          	(1<<30)
#define ZQCAL                           	(1<<31)
//ZQCR1                                 
#define ZQ_CALPRD(n)                    ((n)&0x7FFF)
#define ZQ_CAL_EN                       	(1<<31)
//ZQCR2                                 
#define ZQCS_CALPRD(n)                  ((n)&0x7FFF)
#define ZQCS_CAL_EN                     	(1<<31)

//ZQSR;                   //SDRAM ZQ Status Register                                     
//TPR3;                   //SDRAM Timing Parameters Register 3                           
#define BL2                             		(0)
#define BL4                             		(1)
#define BL8                             		(2)
#define BL16                            		(3)
#define BLOTF_EN                        	(1<<2)
#define CL(n)                           (((n)&0xF)<<3)
#define CWL(n)                          (((n)&0xF)<<7)
#define WR(n)                           (((n)&0xF)<<11)
#define AL(n)                           (((n)&0xF)<<15)

//ALPMR;                  //Automatic Low Power Mode Register                            
#define LPPERIOD_CLK_STOP(n)            ((n)&0xFF)
#define LPPERIOD_POWER_DOWN(n)          (((n)&0xFF)<<8)
#define AUTOCS                          	(1<<25)
#define AUTOPD                          	(1<<26)

//Reserved[0x7c-0x30];                                                                   
//MR;                     //Mode Register                                                
#define DDR_BL4           			(2)
#define DDR_BL8           			(3)
#define DDR_CL(n)         (((n)&0x7)<<4)
#define DDR2_WR(n)        ((((n)-1)&0x7)<<9)

//mr0 for ddr3
#define DDR3_BL8          (0)
#define DDR3_BC4_8        (1)
#define DDR3_BC4          (2)
#define DDR3_BL(n)        (n)
#define DDR3_CL(n)        ((((n-4)&0x7)<<4)|(((n-4)&0x8)>>1))
#define DDR3_MR0_CL(n)    ((((n-4)&0x7)<<4)|(((n-4)&0x8)>>1))
#define DDR3_MR0_WR(n)    (((n)&0x7)<<9)
#define DDR3_MR0_DLL_RESET    (1<<8)
#define DDR3_MR0_DLL_NOR   (0<<8)

//mr1 for ddr3
#define DDR3_MR1_AL(n)  ((n&0x7)<<3)
#define DDR3_MR1_DIC(n) (((n&1)<<1)|((n&2)<<4))
#define DDR3_MR1_RTT_NOM(n) (((n&1)<<2)|((n&2)<<5)|((n&4)<<7))

//mr2 for ddr3
#define DDR3_MR2_CWL(n) (((n-5)&0x7)<<3)

//EMR;                    //Extended Mode Register      
#define DDR2_STR_FULL     		(0)
#define DDR2_STR_REDUCE   		(1<<1)
#define DDR2_AL(n)        (((n)&0x7)<<3)
#define DDR2_ODT_DIS      		(0)
#define DDR2_ODT_150      		(0x40)
#define DDR2_ODT_75       		(0x4)
#define DDR2_ODT_50       		(0x44)

//EMR2;                   //Extended Mode Register 2                                     
//EMR3;                   //Extended Mode Register 3                                     
//y Management Unit Registers                                                            
//HPCR[32];               //Host Port Configuration Register 0-31                        
#define HPBL(n)                         ((n)&0xFF)
#define HCBP                            		(1<<8)
#define HNPC                            		(1<<9)

//PQCR[8];                //Priority Queue Configuration Register 0-7                    
#define TOUT(n)                         ((n)&0xFF)
#define TOUT_MUL_1                      	(0<<8)
#define TOUT_MUL_16                     (1<<8)
#define TOUT_MUL_64                     (2<<8)
#define TOUT_MUL_256                   (3<<8)
#define LPQS(n)                         (((n)&0x3)<<10)
#define PQBL(n)                         (((n)&0xFF)<<12)
#define SWAIT(n)                        (((n)&0x1F)<<20)
#define INTRPT(n)                       (((n)&0x7)<<25)
#define APQS                            		(1<<28)

#define PLL_CLKR(i)	((((i) - 1) & 0x1f) << 10)

#define PLL_CLKF(i)	((((i) - 1) & 0x7f) << 3)

#define PLL_CLKOD(i)	(((i) & 0x03) << 1)

//MMGCR;                  //Memory Manager General Configuration Register                
#define PORT0_NORMAL_PRIO        	(0)
#define PORT0_HIGH_PRIO             	(2)

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



typedef struct DDR_CONFIG_Tag
{
    unsigned int row;
    unsigned int bank;
    unsigned int col;
    unsigned int config;
}DDR_CONFIG_T;

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

// controller base address
#define pDDR_Reg 	    ((pDDR_REG_T)RK29_DDRC_BASE)
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
static __sramdata uint32_t mem_type;    // 0:DDR2, 1:DDR3, 2:LPDDR
static __sramdata uint32_t ddr_type;    // used for ddr3 only
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
static __sramdata volatile uint32_t ddr_stop;


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
        if(ddr_type > DDR3_DEFAULT){
            ret = -1;
            goto out;
        }
        
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
            ret = -3;
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
        tRC = ((ddr3_tRC_tFAW[ddr_type]>>16)*nMHz+999)/1000;
        
        /*
         * tFAW TPR1[8:3], valid values are 2~31
         */
        tFAW = ((ddr3_tRC_tFAW[ddr_type]&0x0ff)*nMHz+999)/1000;
        
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

        cl = ddr3_cl_cwl[ddr_type][tmp] >> 16;
        cwl = ddr3_cl_cwl[ddr_type][tmp] & 0x0ff; 
        if(cl == 0)
            ret = -4;
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
        else if(nMHz <= 500)
        {
            clkr = 2;
            clkod = 1;
        }
        else
        {
            clkr = 2;
            clkod = 0;
        }
            pllband = (0x01u<<16);
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
    pDDR_Reg->ALPMR &= ~(AUTOPD);
    pDDR_Reg->CCR &= ~HOSTEN;  //disable host port
    pDDR_Reg->CCR |= FLUSH;    //flush
    delayus(1);
    pDDR_Reg->DCR = (pDDR_Reg->DCR & (~((0x1<<24) | (0x1<<13) | (0xF<<27) | (0x1<<31)))) | ((0x1<<13) | (0x2<<27) | (0x1<<31));  //enter Self Refresh
    //delayus(10); 
    do
    {
        delayus(1);
    }while(pDDR_Reg->DCR & (EXE));
    pDDR_Reg->CCR |= ITMRST;   //ITM reset
    pSCU_Reg->CRU_SOFTRST_CON[0] |= (0x1F<<19);  //reset DLL
    delayus(1);
    pSCU_Reg->CRU_CLKGATE_CON[0] |= (0x1<<18);  //close DDR PHY clock
    delayus(10);
        pDDR_Reg->DLLCR09[0] =0x80000000;
        pDDR_Reg->DLLCR09[9] =0x80000000;
        pDDR_Reg->DLLCR09[1] =0x80000000;
        pDDR_Reg->DLLCR09[2] =0x80000000;
        pDDR_Reg->DLLCR09[3] =0x80000000;
    dsb();
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
    pSCU_Reg->CRU_CLKGATE_CON[0] &= ~(0x1<<18);  //open DDR PHY clock
    delayus(10); 
    pSCU_Reg->CRU_SOFTRST_CON[0] &= ~(0x1F<<19); //de-reset DLL
    delayus(1000); 
    pDDR_Reg->CCR &= ~ITMRST;   //ITM reset
    delayus(500); 
    pDDR_Reg->DCR = (pDDR_Reg->DCR & (~((0x1<<24) | (0x1<<13) | (0xF<<27) | (0x1<<31)))) | ((0x1<<13) | (0x7<<27) | (0x1<<31)); //exit    
    delayus(1000);
    ddr_update_mr();
    delayus(1);

refresh:
    pDDR_Reg->CSR = 0x0;
    pDDR_Reg->DRR |= RD;
    delayus(1);
    pDDR_Reg->CCR |= DTT;
    //delayus(15);
    dsb();
    do
    {
        delayus(1);
    }while(pGRF_Reg->GRF_MEM_STATUS[2] & 0x1);  //wait init ok
    
    if(pDDR_Reg->CSR & 0x100000)
    {
        pDDR_Reg->CSR &= ~0x100000;
        goto refresh;
    }
    pDDR_Reg->DRR = TRFC(tRFC) | TRFPRD(tRFPRD) | RFBURST(8);
    delayus(10);
    pDDR_Reg->DRR = TRFC(tRFC) | TRFPRD(tRFPRD) | RFBURST(1);
    delayus(1);
    pDDR_Reg->ALPMR |= AUTOPD;
    pDDR_Reg->CCR |= FLUSH;    //flush
    delayus(1);
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
    local_flush_tlb_all();
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
    #if DDR_BYPASS_EN
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
    #else
    pDDR_Reg->DLLCR09[0] &= ~(0x1<<31);
    pDDR_Reg->DLLCR09[1] &= ~(0x1<<31);
    pDDR_Reg->DLLCR09[2] &= ~(0x1<<31);
    pDDR_Reg->DLLCR09[3] &= ~(0x1<<31);
    pDDR_Reg->DLLCR09[9] &= ~(0x1<<31);
    pDDR_Reg->DLLCR &= ~(0x1<<23);
    #endif

    /** 4. update timing&parameter  */
    ddr_update_timing();

    /** 5. Issues a Mode Exit command   */
    ddr_selfrefresh_exit();
	dsb(); 
    DDR_RESTORE_SP(save_sp);
    local_irq_restore(flags);
    clk_set_rate(clk_get(NULL, "ddr_pll"), 0);
    return ret;
}
EXPORT_SYMBOL(ddr_change_freq);

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
    n= pGRF_Reg->GRF_MEM_STATUS[2];
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
    
    #if DDR_BYPASS_EN
    pDDR_Reg->DLLCR09[0] |= (0x1<<31);
    pDDR_Reg->DLLCR09[1] |= (0x1<<31);
    pDDR_Reg->DLLCR09[2] |= (0x1<<31);
    pDDR_Reg->DLLCR09[3] |= (0x1<<31);
    pDDR_Reg->DLLCR09[9] |= (0x1<<31);
    pDDR_Reg->DLLCR &= ~(0x1<<23);
    #endif
    
}
EXPORT_SYMBOL(ddr_suspend);

void __sramfunc ddr_resume(void)
{
    uint32_t value;
 
    #if DDR_BYPASS_EN
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
    #endif
    
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
EXPORT_SYMBOL(ddr_resume);

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
    if(mem_type == Mobile_DDR)
        pGRF_Reg->GRF_MEM_CON = (pGRF_Reg->GRF_MEM_CON & ~0x3FF) | ((2<<0)|(2<<2)|(0<<4)|(2<<6)|(2<<8));
    else
        pGRF_Reg->GRF_MEM_CON = (pGRF_Reg->GRF_MEM_CON & ~0x3FF) | ((2<<0)|(1<<2)|(0<<4)|(1<<6)|(2<<8));
}

typedef struct _dtt_cnt_t
{
    uint32_t  value;
    uint32_t  time;
    uint32_t  cnt;
}dtt_cnt_t;

//static int __init ddr_probe(void)
int ddr_init(uint32_t dram_type, uint32_t freq)
{
    volatile uint32_t value = 0;
	uint32_t          addr;
    uint32_t          bw;
    uint32_t          col = 0;
    uint32_t          row = 0;
    uint32_t          bank = 0;
    uint32_t          n;

    ddr_print("version 2.01 20110504 \n");

    mem_type = (pDDR_Reg->DCR & 0x3);
    ddr_type = dram_type;//DDR3_TYPE;//
    ddr_stop = 1;

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
    pDDR_Reg->CCR  &= ~(DFTCMP);
    //pDDR_Reg->CCR |= DQSCFG;// passive windowing mode

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

    
    value = ddr_change_freq(freq);//DDR_FREQ
    ddr_print("init success!!! freq=%dMHz\n", value);
    ddr_print("CSR:0x%x, RSLR0:0x%x, RSLR1:0x%x, RDGR0:0x%x, RDGR1:0x%x\n", 
                        pDDR_Reg->CSR, 
                        pDDR_Reg->RSLR[0], pDDR_Reg->RSLR[1], 
                        pDDR_Reg->RDGR[0], pDDR_Reg->RDGR[1]);
    return 0;
}
EXPORT_SYMBOL(ddr_init);

//core_initcall_sync(ddr_probe);

#ifdef CONFIG_DDR_RECONFIG
#include "ddr_reconfig.c"
#endif
