#ifndef __MACH_ROCKCHIP_PM_H
#define __MACH_ROCKCHIP_PM_H



/******************************sleep data in bootram********************************/

#define RKPM_ALIGN(size,n_byte) (((size+n_byte-1)/n_byte)*n_byte)

// index data off in SLP_DATA_SAVE_BASE
// it may be include in *.s ,so con't use enum type define



#define RKPM_BOOTDATA_CPUSP            (0)  //cpu sp addr
#define RKPM_BOOTDATA_CPUCODE       (RKPM_BOOTDATA_CPUSP+1)  //cpu resume fun,phy base
#define RKPM_BOOTDATA_DDR_F            (RKPM_BOOTDATA_CPUCODE+1)  //ddr flag , 1 need to resume ddr ctr
#define RKPM_BOOTDATA_DPLL_F          (RKPM_BOOTDATA_DDR_F+1) //ddr pll flag, 1  need to resume dpll
#define RKPM_BOOTDATA_DDRCODE       (RKPM_BOOTDATA_DPLL_F+1) //ddr resume code ,phy base
#define RKPM_BOOTDATA_DDRDATA       (RKPM_BOOTDATA_DDRCODE+1) //ddr resume data ,phy base
// for l2 resume
#define RKPM_BOOTDATA_L2LTY_F             (RKPM_BOOTDATA_DDRDATA+1)  //save l2 latency val
#define RKPM_BOOTDATA_L2LTY             (RKPM_BOOTDATA_L2LTY_F+1)
// for arm errata 81835 resume
#define RKPM_BOOTDATA_ARM_ERRATA_818325_F            (RKPM_BOOTDATA_L2LTY+1)  //save l2 latency val
#define RKPM_BOOTDATA_ARM_ERRATA_818325             (RKPM_BOOTDATA_ARM_ERRATA_818325_F+1)

#define RKPM_BOOTDATA_ARR_SIZE  (RKPM_BOOTDATA_ARM_ERRATA_818325+1)



#define RKPM_BOOT_CODE_OFFSET (0x0)
#define RKPM_BOOT_CODE_SIZE	(0x100)

#define RKPM_BOOT_DATA_OFFSET (RKPM_BOOT_CODE_OFFSET+RKPM_BOOT_CODE_SIZE)
#define RKPM_BOOT_DATA_SIZE	(RKPM_BOOTDATA_ARR_SIZE*4)

#define RKPM_BOOT_DDRCODE_OFFSET (RKPM_BOOT_DATA_OFFSET+RKPM_BOOT_DATA_SIZE)





#define RKPM_CTRBITS_SOC_DLPMD RKPM_OR_5BITS(ARMDP_LPMD ,ARMOFF_LPMD ,ARMLOGDP_LPMD ,ARMOFF_LOGDP_LPMD,ARMLOGOFF_DLPMD)



#ifndef _RKPM_SEELP_S_INCLUDE_
#include <dt-bindings/suspend/rockchip-pm.h>

#define RKPM_LOOPS_PER_USEC	24
#define RKPM_LOOP(loops)	do { unsigned int i = (loops); if (i < 7) i = 7; barrier(); asm volatile(".align 4; 1: subs %0, %0, #1; bne 1b;" : "+r" (i)); } while (0)
/* delay on slow mode */
#define rkpm_udelay(usecs)	RKPM_LOOP((usecs)*RKPM_LOOPS_PER_USEC)
/* delay on deep slow mode */
#define rkpm_32k_udelay(usecs)	RKPM_LOOP(((usecs)*RKPM_LOOPS_PER_USEC)/(24000000/32768))



typedef void (*rkpm_ops_void_callback)(void);
typedef void (*rkpm_ops_printch_callback)(char byte);
typedef void (*rkpm_ops_paramter_u32_cb)(u32 val);
typedef void (*rkpm_sram_suspend_arg_cb)(void *arg);

// ops in ddr callback
struct rkpm_ops {

	rkpm_ops_void_callback prepare;
	rkpm_ops_void_callback finish;
	rkpm_ops_void_callback regs_pread;

	rkpm_ops_void_callback pwr_dmns;
	rkpm_ops_void_callback re_pwr_dmns;

	rkpm_ops_void_callback gtclks;
	rkpm_ops_void_callback re_gtclks;


	rkpm_ops_void_callback plls;
	rkpm_ops_void_callback re_plls;
	
	rkpm_ops_void_callback  gpios;
	rkpm_ops_void_callback  re_gpios;
    
        rkpm_ops_paramter_u32_cb save_setting;  //idle \ sleep mode save and setting
	rkpm_ops_void_callback re_save_setting;

       
        rkpm_ops_void_callback  slp_setting;
        rkpm_ops_void_callback  slp_re_first;
        //rkpm_ops_void_callback  slp_re_last;
    
        rkpm_ops_printch_callback printch;

};


// ops in intmen callback
struct rkpm_sram_ops{

	rkpm_ops_void_callback volts;
	rkpm_ops_void_callback re_volts;

	rkpm_ops_void_callback gtclks;
	rkpm_ops_void_callback re_gtclks;

	rkpm_ops_paramter_u32_cb sysclk;
	rkpm_ops_paramter_u32_cb re_sysclk;

	rkpm_ops_void_callback pmic;
	rkpm_ops_void_callback re_pmic;
	
	rkpm_ops_void_callback ddr;
	rkpm_ops_void_callback re_ddr;
    
        rkpm_ops_printch_callback printch;
};

#if 0
struct rkpm_gpios_info_st{
    u32 pins;
    u32 in_out;
    u32 level_pull;
};
#endif

/******************ctr bits setting  ************************/

//#define rkpm_chk_val_ctrbit(val,bit) ((val)&(bit))//check bit
#define rkpm_chk_val_ctrbits(val,bits) ((val)&(bits))  //check bits

/************************************reg ops*****************************************************/
#define cru_readl(offset)	readl_relaxed(RK_CRU_VIRT + offset)
#define cru_writel(v, offset)	do { writel_relaxed(v, RK_CRU_VIRT + offset); dsb(); } while (0)

#define pmu_readl(offset)	readl_relaxed(RK_PMU_VIRT + offset)
#define pmu_writel(v,offset)	do { writel_relaxed(v, RK_PMU_VIRT + offset); dsb(); } while (0)

#define grf_readl(offset)	readl_relaxed(RK_GRF_VIRT + offset)
#define grf_writel(v, offset)	do { writel_relaxed(v, RK_GRF_VIRT + offset); dsb(); } while (0)

#define reg_readl(base) readl_relaxed(base)
#define reg_writel(v, base)	do { writel_relaxed(v, base); dsb(); } while (0)

#if 0
#define PM_ERR(fmt, args...) printk(KERN_ERR fmt, ##args)
#define PM_LOG(fmt, args...) printk(KERN_ERR fmt, ##args)
#define PM_WARNING(fmt, args...) printk(KERN_WARNING fmt, ##args)
#else

#define PM_ERR(fmt, args...) printk(fmt, ##args)
#define PM_LOG(fmt, args...) printk(fmt, ##args)
#define PM_WARNING(fmt, args...) printk(fmt, ##args)
#endif

/*********************************pm control******************************************/

extern void rkpm_slp_cpu_resume(void);
extern void rkpm_slp_cpu_while_tst(void);

//extern int  rkpm_slp_cpu_save(int state,int offset);
extern void rkpm_ddr_reg_offset_dump(void __iomem * base_addr,u32 _offset);
extern void  rkpm_ddr_regs_dump(void __iomem * base_addr,u32 start_offset,u32 end_offset);

extern void rkpm_set_pie_info(struct rkpm_sram_ops *pm_sram_ops,rkpm_sram_suspend_arg_cb pie_cb);
extern void rkpm_set_ops_prepare_finish(rkpm_ops_void_callback prepare,rkpm_ops_void_callback finish);
extern void rkpm_set_ops_pwr_dmns(rkpm_ops_void_callback pwr_dmns,rkpm_ops_void_callback re_pwr_dmns);
extern void rkpm_set_ops_gtclks(rkpm_ops_void_callback gtclks,rkpm_ops_void_callback re_gtclks);
extern void rkpm_set_ops_plls(rkpm_ops_void_callback plls,rkpm_ops_void_callback re_plls);
extern void rkpm_set_ops_gpios(rkpm_ops_void_callback gpios,rkpm_ops_void_callback re_gpios);
extern void rkpm_set_ops_printch(rkpm_ops_printch_callback printch);
extern void rkpm_set_ops_regs_pread(rkpm_ops_void_callback regs_pread);
extern void rkpm_set_ops_save_setting(rkpm_ops_paramter_u32_cb save_setting,rkpm_ops_void_callback re_save_setting);
extern void rkpm_set_ops_regs_sleep(rkpm_ops_void_callback slp_setting,rkpm_ops_void_callback re_last);

extern void rkpm_set_sram_ops_volt(rkpm_ops_void_callback volts,rkpm_ops_void_callback re_volts);
extern void rkpm_set_sram_ops_gtclks(rkpm_ops_void_callback gtclks,rkpm_ops_void_callback re_gtclks);
extern void rkpm_set_sram_ops_sysclk(rkpm_ops_paramter_u32_cb sysclk,rkpm_ops_paramter_u32_cb re_sysclk);
extern void rkpm_set_sram_ops_pmic(rkpm_ops_void_callback pmic,rkpm_ops_void_callback re_pmic);
extern void rkpm_set_sram_ops_ddr(rkpm_ops_void_callback ddr,rkpm_ops_void_callback re_ddr);
extern void rkpm_set_sram_ops_printch(rkpm_ops_printch_callback printch);

extern void rkpm_set_ctrbits(u32 bits);
extern u32  rkpm_get_ctrbits(void);
extern void rkpm_clr_ctrbits(u32 bits);//clear

extern void rkpm_ddr_printch(char byte);
extern void rkpm_ddr_printascii(const char *s);
extern void  rkpm_ddr_printhex(unsigned int hex);

extern u32 clk_suspend_clkgt_info_get(u32 *clk_ungt_msk,u32 *clk_gt_last_set,u32 buf_cnt);


/********************************sram print**********************************/
#include "sram.h"

#if 1//def CPU
extern void PIE_FUNC(rkpm_sram_printch_pie)(char byte);
extern void  PIE_FUNC(rkpm_sram_printhex_pie)(unsigned int hex);

#define rkpm_sram_printch(byte)  FUNC(rkpm_sram_printch_pie)(byte)
//void __sramfunc rkpm_sram_printascii(const char *s);
#define rkpm_sram_printhex(hex) FUNC(rkpm_sram_printhex_pie)(hex)
#endif

#endif
#endif

