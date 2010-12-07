/********************************************************************************
*********************************************************************************
			COPYRIGHT (c)   2004 BY ROCK-CHIP FUZHOU
				--  ALL RIGHTS RESERVED  --

File Name:	    epphal.h
Author:		    XUESHAN LIN
Created:        1st Dec 2008
Modified:
Revision:		1.00
********************************************************************************
********************************************************************************/
#ifndef _EPPHAL_H
#define _EPPHAL_H
    #define     read_XDATA32(address)           (*((uint32 volatile*)(address)))
    #define     write_XDATA32(address, value)   (*((uint32 volatile*)(address)) = value)

    #define     USB_OTG_INT_CH                  (1<<8)

//1寄存器结构定义
#ifndef DRIVERS_INTC
    //INTC Registers
    typedef volatile struct tagINTC_STRUCT 
    {
        uint32 IRQ_INTEN_L; 
        uint32 IRQ_INTEN_H;
        uint32 IRQ_INTMASK_L;
        uint32 IRQ_INTMASK_H; 
        uint32 IRQ_INTFORCE_L; 
        uint32 IRQ_INTFORCE_H; 
        uint32 IRQ_RAWSTATUS_L; 
        uint32 IRQ_RAWSTATUS_H; 
        uint32 IRQ_STATUS_L; 
        uint32 IRQ_STATUS_H; 
        uint32 IRQ_MASKSTATUS_L; 
        uint32 IRQ_MASKSTATUS_H; 
        uint32 IRQ_FINALSTATUS_L; 
        uint32 IRQ_FINALSTATUS_H; 
        uint32 RESERVED1[(0xc0-0x38)/4];
        uint32 FIQ_INTEN; 
        uint32 FIQ_INTMASK; 
        uint32 FIQ_INTFORCE;
        uint32 FIQ_RAWSTATUS; 
        uint32 FIQ_STATUS;
        uint32 FIQ_FINALSTATUS;
        uint32 IRQ_PLEVEL;
        uint32 RESERVED2[(0xe8-0xdc)/4];
        uint32 IRQ_PN_OFFSET[40];
        uint32 RESERVED3[(0x3f8-0x188)/4]; 
        uint32 AHB_ICTL_COMP_VERSION;
        uint32 ICTL_COMP_TYPE;
    } INTC_REG, *pINTC_REG;

    //SCU Registers
    typedef volatile struct tagSCU_STRUCT 
    {
        uint32 SCU_APLL_CON; 
        uint32 SCU_DPLL_CON;
        uint32 SCU_CPLL_CON;
        uint32 SCU_MODE_CON; 
        uint32 SCU_PMU_CON; 
        uint32 SCU_CLKSEL0_CON; 
        uint32 SCU_CLESEL1_CON; 
        uint32 SCU_CLKGATE0_CON; 
        uint32 SCU_CLKGATE1_CON; 
        uint32 SCU_CLKGATE2_CON; 
        uint32 SCU_SOFTRST_CON; 
        uint32 SCU_CHIPCFG_CON; 
        uint32 SCU_CPUPD; 
    } SCU_REG, *pSCU_REG;

    //REG FILE registers
    typedef volatile struct tagREG_FILE
    {
        uint32 CPU_APB_REG0; 
        uint32 CPU_APB_REG1;
        uint32 CPU_APB_REG2;
        uint32 CPU_APB_REG3; 
        uint32 CPU_APB_REG4; 
        uint32 CPU_APB_REG5; 
        uint32 CPU_APB_REG6; 
        uint32 CPU_APB_REG7; 
        uint32 IOMUX_A_CON; 
        uint32 IOMUX_B_CON; 
        uint32 GPIO0_AB_PU_CON; 
        uint32 GPIO0_CD_PU_CON; 
        uint32 GPIO1_AB_PU_CON; 
        uint32 GPIO1_CD_PU_CON; 
        uint32 OTGPHY_CON0; 
        uint32 OTGPHY_CON1; 
    } REG_FILE, *pREG_FILE;

    /********************************************************************
    **                          结构定义                                *
    ********************************************************************/
    //GRF Registers
    typedef volatile struct tagGRF_REG
    {
        uint32  CPU_APB_REG0;
        uint32  CPU_APB_REG1;
        uint32  CPU_APB_REG2;
        uint32  CPU_APB_REG3;
        uint32  CPU_APB_REG4;
        uint32  CPU_APB_REG5;
        uint32  CPU_APB_REG6;
        uint32  CPU_APB_REG7;
        uint32  IOMUX_A_CON;
        uint32  IOMUX_B_CON;
        uint32  GPIO0_AB_PU_CON;
        uint32  GPIO0_CD_PU_CON;
        uint32  GPIO1_AB_PU_CON;
        uint32  GPIO1_CD_PU_CON;
        uint32  OTGPHY_CON0;
        uint32  OTGPHY_CON1;
    }GRF_REG, *pGRF_REG,*pAPB_REG;

    //GPIO Registers
    typedef volatile struct tagGPIO_STRUCT
    {
        uint32 GPIO_SWPORTA_DR;
        uint32 GPIO_SWPORTA_DDR;
        uint32 RESERVED1;
        uint32 GPIO_SWPORTB_DR;
        uint32 GPIO_SWPORTB_DDR;
        uint32 RESERVED2;
        uint32 GPIO_SWPORTC_DR;
        uint32 GPIO_SWPORTC_DDR;
        uint32 RESERVED3;
        uint32 GPIO_SWPORTD_DR;
        uint32 GPIO_SWPORTD_DDR;
        uint32 RESERVED4;
        uint32 GPIO_INTEN;
        uint32 GPIO_INTMASK;
        uint32 GPIO_INTTYPE_LEVEL;
        uint32 GPIO_INT_POLARITY;
        uint32 GPIO_INT_STATUS;
        uint32 GPIO_INT_RAWSTATUS;
        uint32 GPIO_DEBOUNCE;
        uint32 GPIO_PORTS_EOI;
        uint32 GPIO_EXT_PORTA;
        uint32 GPIO_EXT_PORTB;
        uint32 GPIO_EXT_PORTC;
        uint32 GPIO_EXT_PORTD;
        uint32 GPIO_LS_SYNC;
    }GPIO_REG,*pGPIO_REG;
    
#endif

//1全局变量
#undef	EXT
#ifdef	IN_EPPHAL
		#define	EXT
#else
		#define	EXT		extern
#endif		

//1函数原型声明

    extern	void    EnableIRQ(void);
    extern	void    DisableIRQ(void);
    extern	void    EnableOtgIntr(void);
    extern	void    DisableOtgIntr(void);
    extern	void    InterruptInit(void);
#endif
	
