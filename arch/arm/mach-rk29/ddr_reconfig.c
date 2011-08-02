static __sramdata uint32_t ddrreg[0x40];
extern void local_flush_tlb_all(void);
#if 1
unsigned int __sramlocalfunc ddr_datatraining(int nMHz)
{
    pDDR_Reg->CSR =0x0;
    pDDR_Reg->DRR |= RD;
    delayus(1);
    pDDR_Reg->CCR |= DTT;
    dsb();
    do{
       delayus(1);     
    }while(pGRF_Reg->GRF_MEM_STATUS[2] &0x1);
    
    if(pDDR_Reg->CSR & 0x100000)
        while(1);
    pDDR_Reg->DRR &= ~RD;
    return 0;

}

void __sramlocalfunc ddrReg_Save(void)
{
    int i=0;
//    pDDR_REG_T pDDR_Reg=((pDDR_REG_T)RK29_DDRC_BASE);
    for(i =0; i<0x30; i++)
        ddrreg[i] =*(unsigned long volatile *)(RK29_DDRC_BASE +i*4);
    ddrreg[3] =0;
    ddrreg[i++] =*(unsigned long volatile *)(RK29_DDRC_BASE +0x7c*4); //pDDR_Reg->MR;
    ddrreg[i++] =*(unsigned long volatile *)(RK29_DDRC_BASE +0x7d*4);
    ddrreg[i++] =*(unsigned long volatile *)(RK29_DDRC_BASE +0x7e*4);
    ddrreg[i++] =*(unsigned long volatile *)(RK29_DDRC_BASE +0x7f*4);
    //rest reg did not saved yet
}
void __sramlocalfunc ddrReg_Restore(void)
{
    int i=0;
//    pDDR_REG_T pDDR_Reg=((pDDR_REG_T)RK29_DDRC_BASE);
    for(i =0; i<0x30; i++)
        *(unsigned long volatile *)(RK29_DDRC_BASE +i*4) =ddrreg[i];
     *(unsigned long volatile *)(RK29_DDRC_BASE +0x7c*4) =ddrreg[i++];
     *(unsigned long volatile *)(RK29_DDRC_BASE +0x7d*4) =ddrreg[i++];
     *(unsigned long volatile *)(RK29_DDRC_BASE +0x7e*4) =ddrreg[i++];
     *(unsigned long volatile *)(RK29_DDRC_BASE +0x7f*4) =ddrreg[i++];
    //rest reg did not saved yet
}
void __sramlocalfunc Delay10cyc(int count)
{
    volatile int i;

    while(count--)
    {
        for (i=0; i<8; i++);    //12*8+8=104cyc
    }
}
#define     RECONFIG_DEBUG 0
#if    RECONFIG_DEBUG
unsigned int __sramdata mem[42];
unsigned int __sramdata maxtimeout =0;
#endif
unsigned int __sramdata gpu_suspended;
unsigned int __sramdata gpu_power;
unsigned int __sramdata gpu_clock;
unsigned int __sramdata gpuctl;
unsigned int __sramdata gpususpendcmd =0x7;
unsigned int __sramdata currcmdbufadr;
unsigned int __sramdata clksel17;
unsigned int __sramdata cru_gatecon[4];
unsigned int __sramdata i2sxfer;
void __sramlocalfunc __ddr_reconfig(int mode)
{
#if 1
    int i, n, bakdatr;
    volatile unsigned int * temp=(volatile unsigned int *)SRAM_CODE_OFFSET;

//        __cpuc_flush_kern_all();
//        __cpuc_flush_user_all();
        local_flush_tlb_all();
        n=temp[0];
        barrier();
        n=temp[1024];
        barrier();
        n=temp[1024*2];
        barrier();
        n=temp[1024*3];
        barrier();
        n= pSCU_Reg->CRU_SOFTRST_CON[0];
        
        dsb();
        pDDR_Reg->DLLCR09[0] &=~0x3c000;
        pDDR_Reg->DLLCR09[1] &=~0x3c000;
        pDDR_Reg->DLLCR09[2] &=~0x3c000;
        pDDR_Reg->DLLCR09[3] &=~0x3c000;
        pDDR_Reg->DLLCR09[0] |=0x4000; //set 90-18
        pDDR_Reg->DLLCR09[1] |=0x4000;
        pDDR_Reg->DLLCR09[2] |=0x4000;
        pDDR_Reg->DLLCR09[3] |=0x4000;

        n=pGRF_Reg->GRF_OS_REG[2]; // *(unsigned long volatile *)(0xf50080d8);
        
        pDDR_Reg->CCR &= ~HOSTEN;               //ddr3 400m 4us 4*6*rank+1;

        pDDR_Reg->DCR = (pDDR_Reg->DCR & (~((0x1<<24) | (0x1<<13) | (0xF<<27) | (0x1<<31)))) | ((0x1<<13) | (0x2<<27) | (0x1<<31));  //enter Self Refresh
        while(pDDR_Reg->DCR &(0x1<<31)); //may done soon
        ddrReg_Save();
        
#if 1
#if 1
        //DO_INT Must be cleared before ddrReg_Save

        pSCU_Reg->CRU_SOFTRST_CON[2] |= ((0x3<<15) | (0x3<<11) |(0x3<<8));
        pSCU_Reg->CRU_SOFTRST_CON[0] |= (0x7f<<18);
        dsb();
        Delay10cyc(100);
        pSCU_Reg->CRU_SOFTRST_CON[2] &= ~((0x3<<15) | (0x3<<11) | (0x3<<8));
//        if((mode >>12)&0xfff)
//            *(unsigned long volatile *)(0xf50080ac) =mode &0xfff;
            
        pDDR_Reg->PQCR[0] =0x0e03f000;
        pDDR_Reg->PQCR[1] =(mode ==0) ?0x0e000000 : 0x0e00f000;
//        pDDR_Reg->PQCR[2] =0x0e00f000;
        ddrReg_Restore();
        pDDR_Reg->MMGCR =((mode&0xf) ==0) ?0 : 2;
            
        pSCU_Reg->CRU_SOFTRST_CON[0]  &=~(0x7F<<18);
        dsb();
        Delay10cyc(200); //need 1024 cycles, worst case assume ddr @200MHZ, cpu at @1GHZ, need 5120 cycles delay
//        if((pDDR_Reg->DRR) &0x0f000000)
//            while(1);
#else
        cru_gatecon[0] =pSCU_Reg->CRU_CLKGATE_CON[0];
        cru_gatecon[1] =pSCU_Reg->CRU_CLKGATE_CON[1];
        cru_gatecon[3] =pSCU_Reg->CRU_CLKGATE_CON[3];
        
        pSCU_Reg->CRU_CLKGATE_CON[0] |=/*(2<<19)*/(3<<9);
        pSCU_Reg->CRU_CLKGATE_CON[1] |=(1<<6);
        pSCU_Reg->CRU_CLKGATE_CON[3] |=((1<<1) |(0xf<<10) |(0xf<<14));
        pSCU_Reg->CRU_SOFTRST_CON[2] |=(1<<9);// ((0x1<<15) | (0x3<<11) | (0x3<<8));
        dsb();
        Delay10cyc(100);
        pSCU_Reg->CRU_SOFTRST_CON[0] |= (0x7f<<18);
        Delay10cyc(100);
        pSCU_Reg->CRU_SOFTRST_CON[2] &= ~((0x1<<15) | (0x3<<11) | (0x3<<8));
        if((mode >>12)&0xfff)
            *(unsigned long volatile *)(0xf50080ac) =mode &0xfff;
            
        pDDR_Reg->PQCR[0] =0x0e03f000;
        pDDR_Reg->PQCR[1] =0x0e01f000;
        pDDR_Reg->PQCR[2] =0x0e00f000;
        ddrReg_Restore();
        pDDR_Reg->MMGCR =(mode ==0) ?0:2;
        dsb();
            
        pSCU_Reg->CRU_SOFTRST_CON[0]  &=~(0x7F<<18);
        dsb();
        Delay10cyc(100);
        pSCU_Reg->CRU_CLKGATE_CON[0]=cru_gatecon[0];
        pSCU_Reg->CRU_CLKGATE_CON[1]=cru_gatecon[1];
        pSCU_Reg->CRU_CLKGATE_CON[3]=cru_gatecon[3];
        Delay10cyc(200); //need 1024 cycles, worst case assume ddr @200MHZ, cpu at @1GHZ, need 5120 cycles delay
#endif
        pDDR_Reg->DCR |= DO_INIT; 
        while(pGRF_Reg->GRF_MEM_STATUS[2] & 0x1)  //wait init ok
            Delay10cyc(1);
        pDDR_Reg->DRR |=(1<<31);
        Delay10cyc(10);
        pDDR_Reg->CCR |= DTT;               //ddr3 400m 4us 4*6*rank+1;
        Delay10cyc(100);
        while(pGRF_Reg->GRF_MEM_STATUS[2] & 0x1)  //wait dtt ok
            Delay10cyc(1);
        if(pGRF_Reg->GRF_MEM_STATUS[2] & 0x2)
            while(1);
        pDDR_Reg->DRR &=~(1<<31);
        pDDR_Reg->DLLCR09[0] &=~0x3c000;
        pDDR_Reg->DLLCR09[1] &=~0x3c000;
        pDDR_Reg->DLLCR09[2] &=~0x3c000;
        pDDR_Reg->DLLCR09[3] &=~0x3c000;
        pDDR_Reg->DLLCR09[0] |=0x10000; //set 90+18
        pDDR_Reg->DLLCR09[1] |=0x10000;
        pDDR_Reg->DLLCR09[2] |=0x10000;
        pDDR_Reg->DLLCR09[3] |=0x10000;
        pDDR_Reg->DCR &=~DO_INIT;
        pDDR_Reg->CCR |= HOSTEN;  //enable host port
        dsb();
#endif
#endif
}

unsigned int tmodelay1us(unsigned int tmo)
{
    delayus(1);
    return tmo +1;
}
/**********************************
*input mode
*case mode 
*0 normal
*1 cpu priority highest
*2 cpu priority ualtra
        GRF_MEM_CON[1:0]: CPU       (host 0)
                   [3:2]: PERI      (host 1)
                   [5:4]: DISPLAY   (host 2)
                   [7:6]: GPU       (host 3)
                   [9:8]: VCODEC    (host 4)
***********************************/

void/* inline*/ __sramfunc sram_printch(char byte);
int  ddr_reconfig(int mode)
{
    int baklcdctrl;
    int count =0;
    int i;
    unsigned int ret =0;
    unsigned int con3save, flags;
    unsigned int tmo =0;
    mode &=0xf;
    if((pDDR_Reg->MMGCR ==0) &&(mode <2))
    {
        pDDR_Reg->PQCR[0] =(mode ==0) ?0x0e000000 : 0x0e00f000;
        pDDR_Reg->PQCR[1] =(mode ==0) ?0x0e000000 : 0x0e03f000;
        pDDR_Reg->PQCR[2] =(mode ==0) ?0x0e000000 : 0x0e00f000;
        pGRF_Reg->GRF_MEM_CON = (pGRF_Reg->GRF_MEM_CON & ~0x3FF)
                    | ((mode ==0) ?((2<<0)|(1<<2)|(0<<4)|(1<<6)|(2<<8)):((0<<0)|(2<<2)|(1<<4)|(2<<6)|(2<<8)));
        return 1;
    }   
    local_irq_save(flags);
    sram_printch('1');
/*    if(mode ==2)
    {
        tmp =*(unsigned long volatile *)(0xf50080bc);
        pDDR_Reg->PQCR[0] =0x0e03f000;
        pDDR_Reg->PQCR[1] =0x0e01f000;
        pDDR_Reg->PQCR[2] =0x0e00f000;
        pDDR_Reg->MMGCR =(mode ==0) ?0 : 2;
    }
*/
//    asm volatile ("cpsid	if");
    {
        __cpuc_flush_kern_all();
        __cpuc_flush_user_all();
        dsb();
        //some risk: if a common to lcdc is going, then a read form 0xf410c0000 may retrun an old val
        con3save =pSCU_Reg->CRU_CLKGATE_CON[3];
        pSCU_Reg->CRU_CLKGATE_CON[3] =con3save |(1<<3);
        pGRF_Reg->GRF_SOC_CON[0] |=(1<<0);
    {
        gpu_suspended =0;
        gpu_power =0;
        gpu_clock =0;
        
        if((*(unsigned long volatile *)(RK29_PMU_BASE +0x10) &0x40) ==0)
        {
            gpu_power =1;
            if((0xf<<14) !=(pSCU_Reg->CRU_CLKGATE_CON[3] &(0xf<<14)))
            {
                gpu_clock =1;
                if(*(unsigned long volatile *)(RK29_GPU_BASE +0x4) !=0x7fffffff)
                {   //clock enable and not at idle
                    gpu_suspended =1;
#if 1
                    #if 1
                    int chktime =0;
                    for(chktime =0; chktime<32; chktime++ )
                    {
                        if(*(unsigned long volatile *)(RK29_GPU_BASE +0x4) !=0x7ffffffe)
                        {    chktime =0;
                              //
                        if((tmo =tmodelay1us(tmo)) >10)
                        #if 0 //RECONFIG_DEBUG
                            while(1);
                        #else
                            goto ddr_reconfig_cancel;
                        #endif
                        }
                    }
                    #if RECONFIG_DEBUG
                    if(tmo >maxtimeout)
                    {
                        maxtimeout =tmo;
                        printk("maxtimout %d\n", maxtimeout);
                    }
                    #endif
                    {
                        unsigned int i,tmp;
                        currcmdbufadr =*(unsigned long volatile *)(RK29_GPU_BASE +0x664);
                        if((currcmdbufadr&0xfff0) ==0)
                            for(i =0; i<6; i++)
                            {
                                tmp =*(unsigned long volatile *)(RK29_GPU_BASE +0x664);
                                if(((tmp >currcmdbufadr) &&((tmp -currcmdbufadr) >0x10))
                                    ||((tmp <currcmdbufadr) &&((currcmdbufadr -tmp) >0x10)))
                                {
                                    printk("gpu:cmdbuffer base reg read error 0x%x !=0x%x\n", tmp, currcmdbufadr);
                                    i =0;
                                }
                                else
                                    delayus(1);
                                currcmdbufadr =tmp;
                            }
                    }
                    #if 0
                    for(i =0; i<0x1000; i++)
                    {
                        unsigned int tmp;
                        if(currcmdbufadr >(tmp =*(unsigned long volatile *)(0xf4120664)))
                            currcmdbufadr =tmp;
                    }
                    #else
                    if(*(int *)(currcmdbufadr +0x60000000) !=0x380000c8) //0x60000000 assume VA =PA +0x60000000
                    {
                        currcmdbufadr -=8;
                        if(*(int *)(currcmdbufadr +0x60000000) !=0x380000c8)
                        {
                            currcmdbufadr -=8;
                            if(*(int *)(currcmdbufadr +0x60000000) !=0x380000c8)
                            #if RECONFIG_DEBUG
                                while(1);
                            #else
                                goto ddr_reconfig_cancel;
                            #endif
                        }
                    }
                    #endif
                    #if 0 //RECONFIG_DEBUG
                    if((currcmdbufadr &0xffffe000) !=0x736ce000)
                        while(1);
                    {
                        int i;
                        for(i =0; i<16; i++)
                            mem[i] =*(int *)(currcmdbufadr +0x60000000 +(i-4)*4);
                    }
            
                    #endif
                    #endif
                    
                    *(unsigned long volatile *)(RK29_GPU_BASE +0x658) =0x2;
                    dsb();
                    while(*(unsigned long volatile *)(RK29_GPU_BASE +0x4) !=0x7fffffff) delayus(1);      //
#else
                    gpuctl =*(unsigned long volatile *)(RK29_GPU_BASE +0x0);
                    *(unsigned long volatile *)(RK29_GPU_BASE +0x0) =gpususpendcmd;
                    delayus(100);
#endif
                }
            }
        }
    sram_printch('5');
        if(!(gpu_clock &gpu_power))
        {
            unsigned int tmoadd1ms =tmo +3000;
                sram_printch('c');
//            if(tmo==0)
                if(pGRF_Reg->GRF_OS_REG[3] ==0xff)
                    while(1);
            pSCU_Reg->CRU_CLKGATE_CON[3] =(con3save |(1<<3)) &0xfffc3fff;
            clksel17 =pSCU_Reg->CRU_CLKSEL_CON[17];
            pSCU_Reg->CRU_CLKSEL_CON[17]&=~(3<<14);
            dsb();
		    *(unsigned long volatile *)(RK29_PMU_BASE +0x10) &=~0x40;
            dsb();
            while((tmo =tmodelay1us(tmo)) <tmoadd1ms);
            pSCU_Reg->CRU_CLKGATE_CON[3] =(con3save |(1<<3)) &0xfffc3fff;
        }
    }        
    sram_printch('6');
    //status check
        //3 VIP clock con2[22,18](0x20000064) VIPCTL[0](0x10108010) 0==stop 
        while(((0)==(pSCU_Reg->CRU_CLKGATE_CON[2] &((0x1<<18)|(0x1<<22)))) 
            &&((0)!=(*(unsigned long volatile *)(RK29_VIP_BASE +0x10) &(1<<0))) &&((1)!=(*(unsigned long volatile *)(RK29_VIP_BASE +0x2c) &(1<<0))))
            if((tmo =tmodelay1us(tmo)) >20)
            #if RECONFIG_DEBUG
               // goto ddr_reconfig_cancel2;
                while(1);
            #else
                goto ddr_reconfig_cancel2;
            #endif

    sram_printch('7');
        //1 IPP clock_con3[5:4](0x20000068)  INT_ST[6](0x10110010) 1 ==working
        if(((0)==(pSCU_Reg->CRU_CLKGATE_CON[3] &(0x3<<4))) &&
            ((0)!=(*(unsigned long volatile *)(RK29_IPP_BASE +0x10) &(1<<6))))
            if((tmo =tmodelay1us(tmo)) >200000)
            #if RECONFIG_DEBUG
                while(1);
            #else
                goto ddr_reconfig_cancel2;
            #endif
    sram_printch('8');
        //2 SDMA0 clock con0[10](0x2000005c) DSR[3:0](0x20180000) 0 ==stop
        
//        i2sxfer =*(unsigned long volatile *)(RK29_I2S0_BASE +0x28);
//        *(unsigned long volatile *)(RK29_I2S0_BASE +0x28) =0;
        while(((0)==(pSCU_Reg->CRU_CLKGATE_CON[0] &(0x1<<10))) 
            &&(((0)!=(*(unsigned long volatile *)(RK29_SDMAC0_BASE +0x0) &(0xf<<0))) 
                ||(((0)!=(*(unsigned long volatile *)(RK29_SDMAC0_BASE +0x100) &(0xf<<0)))/*&& ((0x27)!=(*(unsigned long volatile *)(RK29_SDMAC0_BASE +0x100) &(0xff<<0)))*/)
                    ||((0)!=(*(unsigned long volatile *)(RK29_SDMAC0_BASE +0x108) &(0xf<<0))) 
                        ||((0)!=(*(unsigned long volatile *)(RK29_SDMAC0_BASE +0x110) &(0xf<<0))) 
                            ||((0)!=(*(unsigned long volatile *)(RK29_SDMAC0_BASE +0x118) &(0xf<<0)))))
            if((tmo =tmodelay1us(tmo)) >200000)
            #if RECONFIG_DEBUG
                while(1);
            #else
                goto ddr_reconfig_cancel2;
            #endif
    sram_printch('9');
        //2 DMA0 clock con0[9](0x2000005c) DSR[3:0](0x201C0000) 0 ==stop
        while(((0)==(pSCU_Reg->CRU_CLKGATE_CON[0] &(0x1<<9))) 
            &&(((0)!=(*(unsigned long volatile *)(RK29_DMAC0_BASE +0x0) &(0xf<<0))) 
                ||((0)!=(*(unsigned long volatile *)(RK29_DMAC0_BASE +0x100) &(0xf<<0))) 
                    ||((0)!=(*(unsigned long volatile *)(RK29_DMAC0_BASE +0x108) &(0xf<<0))) 
                        ||((0)!=(*(unsigned long volatile *)(RK29_DMAC0_BASE +0x110) &(0xf<<0))) 
                            ||((0)!=(*(unsigned long volatile *)(RK29_DMAC0_BASE +0x118) &(0xf<<0)))))
            if((tmo =tmodelay1us(tmo)) >200000)
            #if RECONFIG_DEBUG
                while(1);
            #else
                goto ddr_reconfig_cancel2;
            #endif
    sram_printch('a');
        //2 DMA1 clock con1[5](0x20000060) DSR[3:0](0x20078000) 0 ==stop
        while(((0)==(pSCU_Reg->CRU_CLKGATE_CON[1] &(0x1<<5))) 
            &&(((0)!=(*(unsigned long volatile *)(RK29_DMAC1_BASE +0x0) &(0xf<<0))) 
                ||((0)!=(*(unsigned long volatile *)(RK29_DMAC1_BASE +0x100) &(0xf<<0))) 
                    ||((0)!=(*(unsigned long volatile *)(RK29_DMAC1_BASE +0x108) &(0xf<<0))) 
                        ||((0)!=(*(unsigned long volatile *)(RK29_DMAC1_BASE +0x110) &(0xf<<0))) 
                            ||((0)!=(*(unsigned long volatile *)(RK29_DMAC1_BASE +0x118) &(0xf<<0)))))
            if((tmo =tmodelay1us(tmo)) >200000)
            #if RECONFIG_DEBUG
                while(1);
            #else
                goto ddr_reconfig_cancel2;
            #endif
    sram_printch('b');
/*
        //4 USB
        if(((0)==(*(unsigned long volatile *)(0xf5000068) &(0x3<<4))) &&
            ((0)==(*(unsigned long volatile *)(0xf4110010) &(1<<6))))
            while(1);
*/
        //5 VPU when select VDPU clk VDPU clock con2[19,13:12]else con2[18,11:10] (0x20000068) wreg[1](0x10104204)  0==stop
        //wreg24[0] 0==stop
        {
            int clkgatemask;
            clkgatemask =((0x1<<18)|(0x3<<10))<<((((pGRF_Reg->GRF_SOC_CON[0]))>>23) &1);
            if((0)==(pSCU_Reg->CRU_CLKGATE_CON[3] &clkgatemask))
            while((((0)!=(*(unsigned long volatile *)(RK29_VCODEC_BASE +0x204) &(1<<0)))
                &&((0)==(*(unsigned long volatile *)(RK29_VCODEC_BASE +0x204) &(1<<13)))) //until idle or buff_int
                    ||((0)!=(*(unsigned long volatile *)(RK29_VCODEC_BASE +0x38) &(1<<0))))
                if((tmo =tmodelay1us(tmo)) >200000)
                #if RECONFIG_DEBUG
                    while(1);
                #else
                    goto ddr_reconfig_cancel2;
                #endif
        }
//        while(((0xf<<14)!=(pSCU_Reg->CRU_CLKGATE_CON[3] &(0xf<<14))) &&
//            (*(unsigned long volatile *)(0xf4120004) !=0x7fffffff));
    sram_printch('2');
    
        {
	        static unsigned long save_sp;

	        DDR_SAVE_SP(save_sp);
            {
	            __ddr_reconfig(mode);

            }
	        DDR_RESTORE_SP(save_sp);
        } //    do_ddr_reconfig(mode);
///////////////////////////////////////////////////////////   
    sram_printch('3');
        ret =1;
//        *(unsigned long volatile *)(RK29_I2S0_BASE +0x28) =i2sxfer;
        if(gpu_suspended)
        {
#if 1
            *(unsigned long volatile *)(RK29_GPU_BASE +0x654) =currcmdbufadr;
            *(unsigned long volatile *)(RK29_GPU_BASE +0x658) =0x10002;
            dsb();
            while(*(unsigned long volatile *)(RK29_GPU_BASE +0x4) !=0x7ffffffe);
            #if RECONFIG_DEBUG
            mem[34] =*(unsigned long volatile *)(RK29_GPU_BASE +0x660);
            mem[35] =*(unsigned long volatile *)(RK29_GPU_BASE +0x664);
            mem[36] =*(unsigned long volatile *)(RK29_GPU_BASE +0x668);
            mem[37] =*(unsigned long volatile *)(RK29_GPU_BASE +0x66c);
            {
                int i;
                for(i =0; i<16; i++)
                    mem[i+16] =*(int *)(currcmdbufadr +0x60000000 +(i-4)*4);
            }
            mem[32] =currcmdbufadr;
            mem[33]++;
//            printk("reconfig 0x%x  ,0x%x  ,0x%x  ,0x%x  ,", *(unsigned int volatile *)(0xf4120660),
//                *(unsigned int volatile *)(0xf4120664),*(unsigned int volatile *)(0xf4120668),
//                *(unsigned int volatile *)(0xf412066c));
            #endif
#else
            *(unsigned long volatile *)(RK29_GPU_BASE +0x0) =gpuctl;
#endif
        }
    
        #if RECONFIG_DEBUG
        printk("clkgate =0x%x, 0x%x\n",pSCU_Reg->CRU_CLKGATE_CON[3],tmo);
        #endif
        count++;

ddr_reconfig_cancel2:
        if(!gpu_clock )
            pSCU_Reg->CRU_CLKSEL_CON[17] =clksel17;
        if(!gpu_power)
            *(unsigned long volatile *)(RK29_PMU_BASE +0x10) |=0x40;
        dsb();
        #if RECONFIG_DEBUG
        if((gpu_power ==0) &&( 1 ==gpu_clock))
            while(1);
        #endif
ddr_reconfig_cancel:
        pSCU_Reg->CRU_CLKGATE_CON[3] =con3save;
        pGRF_Reg->GRF_SOC_CON[0]&=~(1<<0);
    }
    local_irq_restore(flags);
    sram_printch('4');
    return ret;
}
#endif


int  rk29fb_irq_notify_ddr(void)
{
    {
        int tmp;
        if(((tmp =*(unsigned long volatile *)(RK29_LCDC_BASE)) &(2<<10)) ==0) //win 0 blanked
        {
            if((tmp &(1<<10)) &&(((pDDR_Reg->MMGCR &(1<<1)) ==2) ||((pGRF_Reg->GRF_MEM_CON &0x3) ==0))) //has OSD and current ddr is supper priority
                ddr_reconfig(0);
        }
        else
        {
            if(((pDDR_Reg->MMGCR &(1<<1)) ==0) &&((pGRF_Reg->GRF_MEM_CON &0x3) ==2))   //current not supper priority
            {
                if((((tmp >>3) &0x7) <2) &&((tmp &(1<<10)) ==0))    //LCD is RGB format, has not OSD
                    ddr_reconfig(1);
            }
        }
    }
}

//#include "ddr_test.c"

