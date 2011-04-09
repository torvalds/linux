/* arch/arm/mach-rk29/include/mach/ddr.h
 *
 * Copyright (C) 2011 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef __ARCH_ARM_MACH_RK29_DDR_H
#define __ARCH_ARM_MACH_RK29_DDR_H

#include <linux/types.h>
#include <mach/sram.h>

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

#define DDR3_800D   (0)     // 5-5-5
#define DDR3_800E   (1)     // 6-6-6
#define DDR3_1066E  (2)     // 6-6-6
#define DDR3_1066F  (3)     // 7-7-7
#define DDR3_1066G  (4)     // 8-8-8
#define DDR3_1333F  (5)     // 7-7-7
#define DDR3_1333G  (6)     // 8-8-8
#define DDR3_1333H  (7)     // 9-9-9
#define DDR3_1333J  (8)     // 10-10-10
#define DDR3_1600G  (9)     // 8-8-8
#define DDR3_1600H  (10)    // 9-9-9
#define DDR3_1600J  (11)    // 10-10-10
#define DDR3_1600K  (12)    // 11-11-11
#define DDR3_1866J  (13)    // 10-10-10
#define DDR3_1866K  (14)    // 11-11-11
#define DDR3_1866L  (15)    // 12-12-12
#define DDR3_1866M  (16)    // 13-13-13
#define DDR3_2133K  (17)    // 11-11-11
#define DDR3_2133L  (18)    // 12-12-12
#define DDR3_2133M  (19)    // 13-13-13
#define DDR3_2133N  (20)    // 14-14-14
#define DDR3_DEFAULT (21)

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

void __sramfunc ddr_suspend(void);
void __sramfunc ddr_resume(void);
void __sramlocalfunc delayus(uint32_t us);
uint32_t __sramfunc ddr_change_freq(uint32_t nMHz);

#endif
