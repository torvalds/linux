#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/suspend.h>
#include "pm.h"
//#include "sram.h"

/*************************dump reg********************************************/

#if 0 // not used

static void __sramfunc rkpm_sram_regs_dump(u32 base_addr,u32 start_offset,u32 end_offset)
{
	u32 i;
       /* 
        rkpm_sram_printch('\n');
        rkpm_sram_printhex(base_addr);
        rkpm_sram_printch(':');
        rkpm_sram_printch('\n');     
     */
	for(i=start_offset;i<=end_offset;)
	{
            rkpm_sram_printhex(i);	 
            rkpm_sram_printch('-');
            rkpm_sram_printhex(readl_relaxed((void *)(base_addr + i)));	 
            if(!(i%5)&&i!=0)
            rkpm_sram_printch('\n');
            i+=4;
	}
    
    rkpm_sram_printch('\n');

}
#endif

struct rkpm_sram_ops DEFINE_PIE_DATA(pm_sram_ops);
//for sram
static __sramdata u32 rkpm_sram_ctrbits;

/****************** for pm.c and in sram***************************************/
//only usr in sram function
#define rkpm_chk_sram_ctrbit(bit) (rkpm_sram_ctrbits&(bit))
#define rkpm_chk_sram_ctrbits(bits) (rkpm_sram_ctrbits&(bits))

#define  RKPM_SRAM_FUN(fun) \
        if(DATA(pm_sram_ops).fun)\
            (DATA(pm_sram_ops).fun)()
		
#define  RKPM_BITCTR_SRAM_FUN(ctr,fun) \
	if(rkpm_chk_sram_ctrbit(RKPM_CTR_##ctr)&&DATA(pm_sram_ops).fun)\
		(DATA(pm_sram_ops).fun)()   
		
// fun with paramater
#define  RKPM_BITSCTR_SRAM_PFUN(bits,fun,fun_p) \
    if(rkpm_chk_sram_ctrbits(bits)&&DATA(pm_sram_ops).fun_p) \
        {DATA(pm_sram_ops).fun;} while(0)
/********************************sram print**********************************/

void PIE_FUNC(rkpm_sram_printch_pie)(char byte)
{
    if(DATA(pm_sram_ops).printch)
           DATA(pm_sram_ops).printch(byte); 
    
   // if (byte == '\n')
        //FUNC(rkpm_sram_printch_pie)('\r');
}
EXPORT_PIE_SYMBOL(FUNC(rkpm_sram_printch_pie));


void  PIE_FUNC(rkpm_sram_printhex_pie)(unsigned int hex)
{
    int i = 8;
     FUNC(rkpm_sram_printch_pie)('0');
     FUNC(rkpm_sram_printch_pie)('x');
    while (i--) {
    	unsigned char c = (hex & 0xF0000000) >> 28;
    	 FUNC(rkpm_sram_printch_pie)(c < 0xa ? c + '0' : c - 0xa + 'a');
    	hex <<= 4;
    }
}
EXPORT_PIE_SYMBOL(FUNC(rkpm_sram_printhex_pie));


/******************************************pm main function******************************************/
#define RKPM_CTR_SYSCLK RKPM_OR_3BITS(SYSCLK_DIV,SYSCLK_32K,SYSCLK_OSC_DIS)

static void __sramfunc rkpm_sram_suspend(u32 ctrbits)
{
	rkpm_sram_printch('7');
	RKPM_BITCTR_SRAM_FUN(VOLTS, volts);
	rkpm_sram_printch('8');
	RKPM_BITCTR_SRAM_FUN(BUS_IDLE, bus_idle_request);
	RKPM_BITCTR_SRAM_FUN(DDR, ddr);

	/*rkpm_sram_printch('8');*/
	/*RKPM_BITCTR_SRAM_FUN(VOLTS,volts);*/
	/*rkpm_sram_printch('9');*/
	/* RKPM_BITCTR_SRAM_FUN(GTCLKS,gtclks);*/

	RKPM_BITSCTR_SRAM_PFUN(RKPM_CTR_SYSCLK,sysclk(rkpm_sram_ctrbits),sysclk);
	
	RKPM_BITCTR_SRAM_FUN(PMIC,pmic);
         
        //  if(rkpm_chk_sram_ctrbit(RKPM_CTR_SRAM_NO_WFI))
        {
            dsb();
            wfi();
        }

	RKPM_BITCTR_SRAM_FUN(PMIC,re_pmic);
     
	RKPM_BITSCTR_SRAM_PFUN(RKPM_CTR_SYSCLK,re_sysclk(rkpm_sram_ctrbits),re_sysclk);
    
	/*RKPM_BITCTR_SRAM_FUN(GTCLKS,re_gtclks);*/
    
	rkpm_sram_printch('9');
	RKPM_BITCTR_SRAM_FUN(VOLTS,re_volts);
    
	rkpm_sram_printch('8');	
	RKPM_BITCTR_SRAM_FUN(DDR,re_ddr);
    
	rkpm_sram_printch('7');	
}

void PIE_FUNC(rkpm_sram_suspend_arg)(void *arg)
{
    rkpm_sram_ctrbits=*((u32 *)arg);
    
   // rkpm_sram_printhex(rkpm_sram_ctrbits); 
    //rkpm_sram_printhex(*((u32 *)arg));
    rkpm_sram_suspend(rkpm_sram_ctrbits);    
}
EXPORT_PIE_SYMBOL(FUNC(rkpm_sram_suspend_arg));
static void rkpm_pie_init(void)
{
    if(rockchip_pie_chunk)
    {
        rkpm_set_pie_info(kern_to_pie(rockchip_pie_chunk, &DATA(pm_sram_ops))
                        ,fn_to_pie(rockchip_pie_chunk, &FUNC(rkpm_sram_suspend_arg)));
    }
}
