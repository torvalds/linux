/****************************************************************
*    	CopyRight(C) 2010 by Rock-Chip Fuzhou
*     All Rights Reserved
*	文件名:ddr.c
*	描述: ddr driver implement
*	作者:hcy
*	创建日期:2011-01-04
*	更改记录:
*	$Log: ddr.c,v $
*	当前版本:1.00   20110104 hcy 提交初始版本
****************************************************************/

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

#include <mach/rk29_iomap.h>
#include <mach/memory.h>
#include <mach/sram.h>
#include <linux/clk.h>

#include <asm/delay.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>


#include <asm/io.h>

//#include <linux/module.h>
//#include <linux/device.h>
//#include <linux/err.h>



unsigned long save_sp;
#define DDR_SAVE_SP		do { save_sp = ddr_save_sp((SRAM_DATA_END&(~7))); } while (0)
#define DDR_RESTORE_SP		do { ddr_save_sp(save_sp); } while (0)
//unsigned long ddr_save_sp( unsigned long new_sp );


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

typedef enum tagDDRDLLMode
{
	DLL_BYPASS=0,
	DLL_NORMAL
}eDDRDLLMode_t;


#define pDDR_Reg 	((pDDR_REG_T)RK29_DDRC_BASE)

#define pGRF_Reg       ((pREG_FILE)RK29_GRF_BASE)
#define pSCU_Reg       ((pCRU_REG)RK29_CRU_BASE)

//#define  read32(address)           (*((volatile unsigned int volatile*)(address)))
//#define  write32(address, value)   (*((volatile unsigned int volatile*)(address)) = value)

//u32 GetDDRCL(u32 newMHz);
//void EnterDDRSelfRefresh(void);
//void ExitDDRSelfRefresh(void);
//void ChangeDDRFreqInSram(u32 oldMHz, u32 newMHz);


static __sramdata u32 bFreqRaise;
static __sramdata u32 capability;  //单个CS的容量

//DDR2
static __sramdata u32 tRFC;
static __sramdata u32 tRFPRD;
static __sramdata u32 tRTP;
static __sramdata u32 tWTR;
static __sramdata u32 tRAS;
static __sramdata u32 tRRD;
static __sramdata u32 tRC;
static __sramdata u32 tFAW;
//Mobile-DDR
static __sramdata u32 tXS;
static __sramdata u32 tXP;

#if 0
asm(	
"	.section \".sram.text\",\"ax\"\n"	
"	.align\n"
"	.type	ddr_save_sp, #function\n"
"               .global ddr_save_sp\n"
"ddr_save_sp:\n"
"	mov r1,sp\n"	
"	mov sp,r0\n"	
"	mov r0,r1\n"	
"	mov pc,lr\n"
"	.previous"
);
#endif

/****************************************************************************
内部sram 的us 延时函数
假定cpu 最高频率1.2 GHz
1 cycle = 1/1.2 ns
1 us = 1000 ns = 1000 * 1.2 cycles = 1200 cycles
*****************************************************************************/
void __sramfunc delayus(u32 us)
{	
     u32 count;
     count = us * 533;
     while(count--)  // 3 cycles
	 	barrier();

}

void __sramfunc DDRUpdateRef(void)
{
    volatile u32 value = 0;

    value = pDDR_Reg->DRR;
    value &= ~(0xFFFF00);
    pDDR_Reg->DRR = value | TRFPRD(tRFPRD);
}


void __sramfunc DDRUpdateTiming(void)
{
    u32 value;
    u32 memType = (pDDR_Reg->DCR & 0x3);

    value = pDDR_Reg->DRR;
    value &= ~(0xFF);
    pDDR_Reg->DRR = TRFC(tRFC) | value;
    if(memType == DDRII)
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
        value = pDDR_Reg->TPR[0];
        value &= ~((0x1F<<16)|(0xF<<21)|(0x3F<<25));
        pDDR_Reg->TPR[0] = value | TRAS(tRAS) | TRRD(tRRD) | TRC(tRC);
        value = pDDR_Reg->TPR[2];
        value &= ~(0x7FFF);
        pDDR_Reg->TPR[2] = value | TXS(tXS) | TXP(tXP);
    }
}


u32 __sramfunc GetDDRCL(u32 newMHz)
{
    u32          memType = (pDDR_Reg->DCR & 0x3);

    if(memType == DDRII)
    {
        if(newMHz <= 266)
        {
            return 4;
        }
        else if((newMHz > 266) && (newMHz <= 333))
        {
            return 5;
        }
        else if((newMHz > 333) && (newMHz <= 400))
        {
            return 6;
        }
        else // > 400MHz
        {
            return 7;
        }
    }
    else
    {
        return 3;
    }
}

/****************************************************************/
//函数名:SDRAM_EnterSelfRefresh
//描述:SDRAM进入自刷新模式
//参数说明:
//返回值:
//相关全局变量:
//注意:(1)系统完全idle后才能进入自刷新模式，进入自刷新后不能再访问SDRAM
//     (2)要进入自刷新模式，必须保证运行时这个函数所调用到的所有代码不在SDRAM上
/****************************************************************/
void __sramfunc EnterDDRSelfRefresh(void)
{    
    pDDR_Reg->CCR &= ~HOSTEN;  //disable host port
    pDDR_Reg->CCR |= FLUSH;    //flush
    delayus(10);
    pDDR_Reg->DCR = (pDDR_Reg->DCR & (~((0x1<<13) | (0xF<<27) | (0x1<<31)))) | ((0x1<<13) | (0x2<<27) | (0x1<<31));  //enter Self Refresh
    delayus(10);
}

/****************************************************************/
//函数名:SDRAM_ExitSelfRefresh
//描述:SDRAM退出自刷新模式
//参数说明:
//返回值:
//相关全局变量:
//注意:(1)SDRAM在自刷新模式后不能被访问，必须先退出自刷新模式
//     (2)必须保证运行时这个函数的代码不在SDRAM上
/****************************************************************/
void __sramfunc ExitDDRSelfRefresh(void)
{
    volatile u32 n;

    pDDR_Reg->DCR = (pDDR_Reg->DCR & (~((0x1<<13) | (0xF<<27) | (0x1<<31)))) | ((0x1<<13) | (0x7<<27) | (0x1<<31)); //exit
    delayus(10); //wait for exit self refresh dll lock
    pSCU_Reg->CRU_SOFTRST_CON[0] |= (0x1F<<19);  //reset DLL
    delayus(10);
    pSCU_Reg->CRU_SOFTRST_CON[0] &= ~(0x1F<<19);
    delayus(100); 
    //pDDR_Reg->CCR |= DTT;
    n = pDDR_Reg->CCR;
    delayus(100);
    pDDR_Reg->CCR |= HOSTEN;  //enable host port
}


/*-------------------------------------------------------------------
Name    : PLLGetAHBFreq
Desc    : 获取DDR的频率
Params  :
Return  : DDR频率,  KHz 为单位
Notes   :
-------------------------------------------------------------------*/
static u32 PLLGetDDRFreq(void)
{
    u32 nr;
    u32 nf;
    u32 no;
    u32 div;
    
    nr = (((pSCU_Reg->CRU_DPLL_CON) >> 10) & 0x1F) + 1;
    nf = (((pSCU_Reg->CRU_DPLL_CON) >> 3) & 0x7F) + 1;
    no = (0x1 << (((pSCU_Reg->CRU_DPLL_CON) >> 1) & 0x3));
    div = ((pSCU_Reg->CRU_CLKSEL_CON[7] >> 26) & 0x7);
    div = 0x1 << div;
    
    return ((24000*nf)/(nr*no*div));
}

static void DDRPreUpdateRef(u32 MHz)
{
    u32 tmp;
    
    tRFPRD = ((59*MHz) >> 3) & 0x3FFF;  // 62/8 = 7.75us
}

static void DDRPreUpdateTiming(u32 MHz)
{
    u32 tmp;
    u32 cl;
    u32 memType = (pDDR_Reg->DCR & 0x3);

    cl = GetDDRCL(MHz);
    //时序
    if(memType == DDRII)
    {
        if(capability <= 0x2000000)  // 256Mb
        {
            tmp = (75*MHz/1000) + ((((75*MHz)%1000) > 0) ? 1:0);
        }
        else if(capability <= 0x4000000) // 512Mb
        {
            tmp = (105*MHz/1000) + ((((105*MHz)%1000) > 0) ? 1:0);
        }
        else if(capability <= 0x8000000)  // 1Gb
        {
            tmp = (128*MHz/1000) + ((((128*MHz)%1000) > 0) ? 1:0);
        }
        else  // 4Gb
        {
            tmp = (328*MHz/1000) + ((((328*MHz)%1000) > 0) ? 1:0);
        }
        //tRFC = 75ns(256Mb)/105ns(512Mb)/127.5ns(1Gb)/327.5ns(4Gb)
        tmp = (tmp > 0xFF) ? 0xFF : tmp;
        tRFC = tmp;
        // tRTP = 7.5ns
        tmp = (8*MHz/1000) + ((((8*MHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 6) ? 6 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRTP = tmp;
        //tWTR = 10ns(DDR2-400), 7.5ns (DDR2-533/667/800)
        if(MHz <= 200)
        {
            tmp = (10*MHz/1000) + ((((10*MHz)%1000) > 0) ? 1:0);
        }
        else
        {
            tmp = (8*MHz/1000) + ((((8*MHz)%1000) > 0) ? 1:0);
        }
        tmp = (tmp > 6) ? 6 : tmp;
        tmp = (tmp < 1) ? 1 : tmp;
        tWTR = tmp;
        //tRAS_min = 45ns
        tmp = (45*MHz/1000) + ((((45*MHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 31) ? 31 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRAS = tmp;
        // tRRD = 10ns
        tmp = (10*MHz/1000) + ((((10*MHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 8) ? 8 : tmp;
        tmp = (tmp < 1) ? 1 : tmp;
        tRRD = tmp;
        if(MHz <= 200)  //tRC = 65ns
        {
            tmp = (65*MHz/1000) + ((((65*MHz)%1000) > 0) ? 1:0);
        }
        else //tRC = 60ns
        {
            tmp = (60*MHz/1000) + ((((60*MHz)%1000) > 0) ? 1:0);
        }
        tmp = (tmp > 42) ? 42 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRC = tmp;
        //tFAW = 50ns
        tmp = (50*MHz/1000) + ((((50*MHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 31) ? 31 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tFAW = tmp;
        if(MHz <= 333)
        {
            tXP = 7;
        }
        else
        {
            tXP = 8;
        }
    }
    else
    {
        if(capability <= 0x2000000)  // 128Mb,256Mb
        {
            // 128Mb,256Mb  tRFC=80ns
            tmp = (80*MHz/1000) + ((((80*MHz)%1000) > 0) ? 1:0);
        }
        else if(capability <= 0x4000000) // 512Mb
        {
            // 512Mb  tRFC=110ns
            tmp = (110*MHz/1000) + ((((110*MHz)%1000) > 0) ? 1:0);
        }
        else if(capability <= 0x8000000)  // 1Gb
        {
            // 1Gb tRFC=140ns
            tmp = (140*MHz/1000) + ((((140*MHz)%1000) > 0) ? 1:0);
        }
        else  // 4Gb
        {
            // 大于1Gb没找到，按DDR2的 tRFC=328ns
            tmp = (328*MHz/1000) + ((((328*MHz)%1000) > 0) ? 1:0);
        }
        //tRFC = 80ns(128Mb,256Mb)/110ns(512Mb)/140ns(1Gb)
        tmp = (tmp > 0xFF) ? 0xFF : tmp;
        tRFC = tmp;
        if(MHz <= 100)  //tRAS_min = 50ns
        {
            tmp = (50*MHz/1000) + ((((50*MHz)%1000) > 0) ? 1:0);
        }
        else //tRAS_min = 45ns
        {
            tmp = (45*MHz/1000) + ((((45*MHz)%1000) > 0) ? 1:0);
        }
        tmp = (tmp > 31) ? 31 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRAS = tmp;
        // tRRD = 15ns
        tmp = (15*MHz/1000) + ((((15*MHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 8) ? 8 : tmp;
        tmp = (tmp < 1) ? 1 : tmp;
        tRRD = tmp;
        if(MHz <= 100)  //tRC = 80ns
        {
            tmp = (80*MHz/1000) + ((((80*MHz)%1000) > 0) ? 1:0);
        }
        else //tRC = 75ns
        {
            tmp = (75*MHz/1000) + ((((75*MHz)%1000) > 0) ? 1:0);
        }
        tmp = (tmp > 42) ? 42 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tRC = tmp;
        //tXSR = 200 ns
        tmp = (200*MHz/1000) + ((((200*MHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 1023) ? 1023 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tXS = tmp;
        //tXP=25ns
        tmp = (25*MHz/1000) + ((((25*MHz)%1000) > 0) ? 1:0);
        tmp = (tmp > 31) ? 31 : tmp;
        tmp = (tmp < 2) ? 2 : tmp;
        tXP = tmp;
    }
}

static void __sramfunc StopDDR(u32 oldMHz, u32 newMHz)
{
    u32 value;
    u32 memType = (pDDR_Reg->DCR & 0x3);
    u32 cl;

    if(memType == DDRII)
    {
        cl = GetDDRCL(newMHz);

        pDDR_Reg->CCR |= FLUSH;    //flush
        delayus(10);
        pDDR_Reg->CCR &= ~HOSTEN;  //disable host port
        
        value = pDDR_Reg->TPR[0];
        value &= ~((0xF<<8)|(0xF<<12));
        pDDR_Reg->TPR[0] = value | TRP(cl) | TRCD(cl);
        value = pDDR_Reg->TPR3;
        value &= ~((0xF<<3)|(0xF<<7)|(0xF<<11));
        pDDR_Reg->TPR3 = value | CL(cl) | CWL(cl-1) | WR(cl);
        //set mode register cl
        pDDR_Reg->DCR = (pDDR_Reg->DCR & (~((0x1<<13) | (0xF<<27) | (0x1<<31)))) | ((0x1<<13) | (0x5<<27) | (0x1<<31));  //precharge-all
        value = pDDR_Reg->MR;
        value &= ~((0x7<<4)|(0x7<<9));
        pDDR_Reg->MR = value | DDR_CL(cl) | DDR2_WR(cl);        
    }
    EnterDDRSelfRefresh();
    if(oldMHz < newMHz)  //升频
    {
        DDRUpdateTiming();
        bFreqRaise = 1;
    }
    else //降频
    {
        DDRUpdateRef();
        bFreqRaise = 0;
    }
}

static void __sramfunc ResumeDDR(void)
{
    if(bFreqRaise)  //升频
    {
        DDRUpdateRef();
    }
    else //降频
    {
        DDRUpdateTiming();
    }
    ExitDDRSelfRefresh();
}

/*-------------------------------------------------------------------
Name    : PLLSetAUXFreq
Desc    : 获取AUX频率
Params  :
Return  : AUX频率,  MHz 为单位
Notes   :
-------------------------------------------------------------------*/
void __sramfunc PLLSetAUXFreq(u32 freq)
{
    // ddr slow
    pSCU_Reg->CRU_MODE_CON &=~(0x3<<6);
    pSCU_Reg->CRU_MODE_CON |= 0x0<<6;

   delayus(10);
    
    pSCU_Reg->CRU_DPLL_CON |= (0x1 << 15); //power down pll
    delayus(1);  //delay at least 500ns
    switch(freq)    //  实际频率要和系统确认 ?????????
    {
        case 136:
            pSCU_Reg->CRU_DPLL_CON = (0x1<<16) | (0x1<<15) | (1<<10) | (44<<3) | (2<<1);  //high band 135
            break;
        case 200:
            pSCU_Reg->CRU_DPLL_CON = (0x1<<16) | (0x1<<15) | (1<<10) | (66<<3) | (2<<1);  //high band 201
            break;
        case 266:
            pSCU_Reg->CRU_DPLL_CON = (0x1<<16) | (0x1<<15) | (1<<10) | (43<<3) | (1<<1);  //high band 264
            break;
        case 333:
            pSCU_Reg->CRU_DPLL_CON = (0x1<<16) | (0x1<<15) | (1<<10) | ((56)<<3) | (1<<1);  //high band
            break;
        case 400:
            pSCU_Reg->CRU_DPLL_CON = (0x1<<16) | (0x1<<15) | (1<<10) | ((66)<<3) | (1<<1);  //high band
            break;
        case 533:
            pSCU_Reg->CRU_DPLL_CON = (0x1<<16) | (0x1<<15) | (1<<10) | (43<<3) | (0<<1);  //high band
            break;
    }
	
    delayus(1);  //delay at least 500ns
    pSCU_Reg->CRU_DPLL_CON &= ~(0x1<<15);
    delayus(2000); // 7.2us*140=1.008ms

    // ddr pll normal
    pSCU_Reg->CRU_MODE_CON &=~(0x3<<6);
    pSCU_Reg->CRU_MODE_CON |= 0x1<<6;

    // ddr_pll_clk: clk_ddr=1:1 	
    pSCU_Reg->CRU_CLKSEL_CON[7] &=~(0x1F<<24);
    pSCU_Reg->CRU_CLKSEL_CON[7] |= (0x0 <<26)| (0x0<<24);

}


static void __sramfunc DDR_ChangePrior(void)
{
	// 2_Display(0) > 1_PERI(1) & 3_GPU(1) > 4_VCODEC(2) & 0_CPU(2)
       pGRF_Reg->GRF_MEM_CON = (pGRF_Reg->GRF_MEM_CON & ~0x3FF) | 0x246;
}

//这个函数的前提条件是:
// 1, 不能有DDR访问
// 2, ChangeDDRFreq函数及函数中所调用到的所有函数都不放在DDR中
// 3, 堆栈不放在DDR中
// 4, 中断关闭，否则可能中断会引起访问DDR

void __sramfunc ChangeDDRFreqInSram(u32 oldMHz, u32 newMHz)
{
    StopDDR(oldMHz, newMHz);
    PLLSetAUXFreq(newMHz);
    ResumeDDR();
}

void __sramfunc DDRDLLSetMode(eDDRDLLMode_t DLLmode, u32 freq)
{
   if( DLLmode == DLL_BYPASS )
   	{
		pDDR_Reg->DLLCR09[0] |= 1<<28;
		pDDR_Reg->DLLCR09[1] |= 1<<28;
		pDDR_Reg->DLLCR09[2] |= 1<<28;
		pDDR_Reg->DLLCR09[3] |= 1<<28;
		pDDR_Reg->DLLCR09[4] |= 1<<28;
	if(freq <= 100)
	      pDDR_Reg->DLLCR &= ~(1<<23); 	
	else if(freq <=200)
	     pDDR_Reg->DLLCR |= 1<<23;
   	}
   else
   	{
	       pDDR_Reg->DLLCR09[0] &= ~(1<<28);
		pDDR_Reg->DLLCR09[1] &= ~(1<<28);
		pDDR_Reg->DLLCR09[2] &= ~(1<<28);
		pDDR_Reg->DLLCR09[3] &= ~(1<<28);
		pDDR_Reg->DLLCR09[4] &= ~(1<<28);   
   	}   	
}
typedef struct DDR_CONFIG_Tag
{
    u32 row;
    u32 bank;
    u32 col;
    u32 config;
}DDR_CONFIG_T;

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
        {16, 8, 11, (DIO_16 | DSIZE_8Gb)},
        {16, 8, 10, (DIO_16 | DSIZE_4Gb)},
        {15, 8, 10, (DIO_16 | DSIZE_2Gb)},
        {14, 8, 10, (DIO_16 | DSIZE_1Gb)},
        {13, 8, 10, (DIO_16 | DSIZE_512Mb)},
    },
    {
        // x32
        {13, 4, 10, (DIO_32 | DSIZE_1Gb)},
        {13, 4, 9,  (DIO_32 | DSIZE_512Mb)},
        {12, 4, 9,  (DIO_32 | DSIZE_256Mb)},
        // x16
        {14, 4, 10, (DIO_16 | DSIZE_1Gb)},
        {13, 4, 10, (DIO_16 | DSIZE_512Mb)},
        {13, 4, 9,  (DIO_16 | DSIZE_256Mb)},
        {12, 4, 9,  (DIO_16 | DSIZE_128Mb)},
        {0,  0, 0,  0},
        {0,  0, 0,  0},
        {0,  0, 0,  0},
    }
};


/****************************************************************/
//函数名:SDRAM_Init
//描述:DDR 初始化
//参数说明:
//返回值:
//相关全局变量:
//注意:
/****************************************************************/
u32 ddrDataTraining[16];
void DDR_Init(void)
{
    volatile u32 value = 0;
	u32          addr;
    u32          MHz;
    unsigned long Hz;
    u32          memType = (pDDR_Reg->DCR & 0x3);  
    u32          bw;
    u32          col;
    u32          row;
    u32          bank;
    u32          n;

    //算物理对齐地址
    n = (unsigned long)ddrDataTraining;
	printk("\n#################### VA = 0x%x\n", n);
    addr =  __pa((unsigned long)ddrDataTraining);
	printk("#################### PA = 0x%x", addr);
    if(addr&0x1F)
    {
        addr += (32-(addr&0x1F));
    }
    addr -= 0x60000000;
	printk("#################### PA aligned = 0x%x\n", addr);
    //算数据线宽
    bw = ((((pDDR_Reg->DCR >> 7) & 0x7)+1)>>1);
    //查出col，row，bank
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
	printk("#################### bw = 0x%x, col = 0x%x, row = 0x%x, bank = 0x%x\n", bw, col, row, bank);
    //根据不同的地址映射方式，算出DTAR寄存器的配置
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
        case AMAP_FIX:    //不支持这种固定的方式
        default:
            break;
    }
	printk("#################### value = 0x%x\n", value);
    pDDR_Reg->DTAR = value;

    if(memType == DDRII)
    {
        pDDR_Reg->ALPMR = LPPERIOD_POWER_DOWN(0xFF); /* | AUTOPD;*/
    }
    else
    {
        pDDR_Reg->ALPMR = LPPERIOD_CLK_STOP(0xFF) | LPPERIOD_POWER_DOWN(0xFF) | AUTOCS | AUTOPD;
    }
    DDR_ChangePrior();
    value = (((pDDR_Reg->DCR >> 7) & 0x7)+1) >> ((pDDR_Reg->DCR >> 2) & 0x3);
    capability = 0x800000 << (((pDDR_Reg->DCR >> 4) & 0x7) + value + ((pDDR_Reg->DCR >> 11) & 0x3));

    Hz = clk_get_rate(clk_get(NULL,"ddr"));
    MHz = Hz/1000000;   //PLLGetDDRFreq()/1000;
    printk("DDR_Init: freq=%dMHz\n", MHz);
    DDRPreUpdateRef(MHz);
    DDRPreUpdateTiming(MHz);
    DDRUpdateRef();
    DDRUpdateTiming();
}

void DDR_ChangeFreq(u32 DDRoldMHz, u32 DDRnewMHz)
{

    DDRPreUpdateRef(DDRnewMHz);
    DDRPreUpdateTiming(DDRnewMHz);	
    DDR_SAVE_SP;
    flush_cache_all();      // 20100615,HSL@RK.
    __cpuc_flush_user_all();
    ChangeDDRFreqInSram(DDRoldMHz, DDRnewMHz);
    DDR_RESTORE_SP;
}

////////////////////////////////////////////////////////////////////////////////////

/****************************************************************/
//函数名:SDRAM_EnterSelfRefresh
//描述:SDRAM进入自刷新模式
//参数说明:
//返回值:
//相关全局变量:
//注意:(1)系统完全idle后才能进入自刷新模式，进入自刷新后不能再访问SDRAM
//     (2)要进入自刷新模式，必须保证运行时这个函数所调用到的所有代码不在SDRAM上
/****************************************************************/
void __sramfunc DDR_EnterSelfRefresh(void)
{
    EnterDDRSelfRefresh();
#if 1
    pSCU_Reg->CRU_CLKGATE_CON[0] |= (0x1<<18);  //close DDR PHY clock    
    delayus(10);
    // ddr slow
    pSCU_Reg->CRU_MODE_CON &=~(0x3<<6);
    pSCU_Reg->CRU_MODE_CON |= 0x0<<6;
    delayus(10);	
    pSCU_Reg->CRU_DPLL_CON |= (0x1 << 15);  //power down DPLL
    delayus(10);  //delay at least 500ns
#endif
}

/****************************************************************/
//函数名:SDRAM_ExitSelfRefresh
//描述:SDRAM退出自刷新模式
//参数说明:
//返回值:
//相关全局变量:
//注意:(1)SDRAM在自刷新模式后不能被访问，必须先退出自刷新模式
//     (2)必须保证运行时这个函数的代码不在SDRAM上
/****************************************************************/
void __sramfunc DDR_ExitSelfRefresh(void)
{
#if 1
     pSCU_Reg->CRU_DPLL_CON &= ~(0x1 << 15);  //power on DPLL   
    //   while(!(pGRF_Reg->GRF_SOC_CON[0] & (1<<28)));
     delayus(200); // 7.2us*140=1.008ms // 锁定pll
    // ddr pll normal
    pSCU_Reg->CRU_MODE_CON &=~(0x3<<6);
    pSCU_Reg->CRU_MODE_CON |= 0x1<<6; 
    delayus(10);	
    pSCU_Reg->CRU_CLKGATE_CON[0] &= ~(0x1<<18);  //enable DDR PHY clock    
    delayus(10);    
#endif
    ExitDDRSelfRefresh();

}
 void preload_sram_addr(unsigned int vaddr)
{
	
	__asm__(
		"mcr p15,0,%0,c8,c5,1 \n\t"
		"mcr p15,0,%1,c10,c0,1 \n\t"
		"mcr p15,0,%0,c10,c1,1 \n\t"
		"mcr p15,0,%2,c10,c0,1\n\t"
		::"r"(vaddr) , "r"(0x00000001), "r"(0x08400000));
}
// 考虑cpu  预取、mmu cache
static void __sramfunc do_selfrefreshtest(void)
{
	volatile u32 n;	
	//local_flush_tlb_all();
	//preload_sram_addr(0xff400000);
	flush_cache_all();      // 20100615,HSL@RK.
	__cpuc_flush_kern_all();
       __cpuc_flush_user_all();

	
    //  pSCU_Reg->CRU_CLKGATE_CON[1] |= 1<<6; // disable DDR Periph clock
   //   pSCU_Reg->CRU_CLKGATE_CON[3] |= 1<<1 |1<<11|1<<13|1<<16; // disable DDR LCDC / DDR VPU / DDR GPU clock
	  	
#if 1
volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;
n=temp[0];
barrier();
n=temp[1024];
barrier();
n=temp[1024*2];
barrier();
n=temp[1024*3];
barrier();


#if 0
	n = * (volatile u32 *)SRAM_CODE_OFFSET;
	n = * (volatile u32 *)(SRAM_CODE_OFFSET + 4096);
	n = * (volatile u32 *)(SRAM_CODE_OFFSET + 8192);
	n = * (volatile u32 *)(SRAM_CODE_OFFSET + 12288);
#endif

#endif
	n= pDDR_Reg->CCR;
	n= pSCU_Reg->CRU_SOFTRST_CON[0];
      //	flush_cache_all();      // 20100615,HSL@RK.
	//__cpuc_flush_kern_all();
       //__cpuc_flush_user_all();
       //barrier();
       dsb();//dmb();
	  
     //  printk("do_selfrefreshtest tlb \n");
   	DDR_EnterSelfRefresh();
	//delayus(100000000);
	//delayus(1000*1000*100);
   	DDR_ExitSelfRefresh();
	dsb(); //dmb();
#if 1
	delayus(1);
	delayus(1);
	delayus(1);
	delayus(1);

#endif
}

static void selfrefreshtest(void)
{
	DDR_SAVE_SP;
	do_selfrefreshtest();
   	DDR_RESTORE_SP;
}

static void changefreqtest(u32 DDRnewMHz)
{
    u32 MHz;
    u32 value =0;
    unsigned long Hz;

    Hz = clk_get_rate(clk_get(NULL,"ddr"));
    MHz =  Hz /1000000; // PLLGetDDRFreq()/1000;
    DDRPreUpdateRef(DDRnewMHz);
    DDRPreUpdateTiming(DDRnewMHz);	
    DDR_SAVE_SP;
    flush_cache_all();      // 20100615,HSL@RK.
    __cpuc_flush_user_all();
    ChangeDDRFreqInSram(MHz, DDRnewMHz);
    DDRDLLSetMode(DLL_BYPASS,DDRnewMHz);
    DDR_RESTORE_SP;
}

#ifdef CONFIG_HAS_EARLYSUSPEND


static __sramdata u32 gfreq = 0;
static void suspend(struct early_suspend *h)
{
	u32 MHz;
       unsigned long Hz;
       unsigned long flags;
	  
      local_irq_save(flags);   
      Hz = clk_get_rate(clk_get(NULL,"ddr"));
      MHz =  Hz /1000000; 
      gfreq = MHz;

      udelay(1000);
	  
      DDR_ChangeFreq(MHz,200);

      local_irq_restore(flags);

     //printk("enter ddr early suspend\n");
}

static void resume(struct early_suspend *h)
{

      unsigned long flags;
	  
      local_irq_save(flags);

      udelay(1000);
	  
      DDR_ChangeFreq(200,333);
	  
      local_irq_restore(flags);
	  
      //printk("enter ddr early suspend resume \n");  
}

struct early_suspend early_suspend_info = {
	.suspend = suspend,
	.resume = resume,
	.level = EARLY_SUSPEND_LEVEL_DISABLE_FB +1,
};
#endif


void __sramfunc ddr_suspend(void)
{
	volatile u32 n;	
	flush_cache_all();      // 20100615,HSL@RK.
	__cpuc_flush_kern_all();
       __cpuc_flush_user_all();

	
    //  pSCU_Reg->CRU_CLKGATE_CON[1] |= 1<<6; // disable DDR Periph clock
   //   pSCU_Reg->CRU_CLKGATE_CON[3] |= 1<<1 |1<<11|1<<13|1<<16; // disable DDR LCDC / DDR VPU / DDR GPU clock
	  	
#if 1
	volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;
	n=temp[0];
	barrier();
	n=temp[1024];
	barrier();
	n=temp[1024*2];
	barrier();
	n=temp[1024*3];
	barrier();

#endif
	n= pDDR_Reg->CCR;
	n= pSCU_Reg->CRU_SOFTRST_CON[0];
      //	flush_cache_all();      // 20100615,HSL@RK.
	//__cpuc_flush_kern_all();
       //__cpuc_flush_user_all();
       //barrier();
       dsb();//dmb();	  
   	DDR_EnterSelfRefresh();
}

void __sramfunc ddr_resume(void)
{
#if 0
	unsigned long flags;
	  
      local_irq_save(flags);
      udelay(1000);	  
      DDR_ChangeFreq(200,333);	  
      local_irq_restore(flags);
#else
   	DDR_ExitSelfRefresh();
	dsb(); 
#endif
}

static int __init ddr_update_freq(void)
{

  // DDR_Init();
   
#if 0 //#ifdef CONFIG_HAS_EARLYSUSPEND
	register_early_suspend(&early_suspend_info);
#endif


#if 0
   unsigned long flags , i;
   printk("DDR enter self-refresh!\n");
   
   local_irq_save(flags);
 
   DDR_Init();
   //DDR_ChangeFreq(333); 
  
   for (i=0;i<1000000;i++)
   {
       printk("%d ", i);
	if(!(i%50))
		printk("\n");

	selfrefreshtest();
	//changefreqtest(200);	
       //delayus(10000000);
       //changefreqtest(333);
   }
   
   local_irq_restore(flags);
   
   printk("DDR exit self-refresh!\n");
   
#endif
   

   return 0;
}

core_initcall_sync(ddr_update_freq);
//late_initcall(ddr_update_freq);

