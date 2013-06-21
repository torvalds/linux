/*the following code reside in dram when enter super mem
 * actually, these code will be load into dram when start & load the system.
 * the addr is : 0xc0f0,0000; size is 256Kbyte;
 */
	
/* these code will be located in fixed addr
 * after receive wake up signal, boot will jump to the fixed addr
 * and excecute these code, and jump to sram for continuing resume.
 */

 #include "./../super_i.h"

extern char *resume1_bin_start;
extern char *resume1_bin_end;
static int (*resume1)(void);

#ifdef RETURN_FROM_RESUME0_WITH_MMU
#define MMU_OPENED
#define SWITCH_STACK
#define FLUSH_TLB
#define FLUSH_ICACHE
#define INVALIDATE_DCACHE
#endif

#ifdef RETURN_FROM_RESUME0_WITH_NOMMU
#undef MMU_OPENED
#define SWITCH_STACK
#define SET_COPRO_REG
#define FLUSH_TLB
#define FLUSH_ICACHE
#define INVALIDATE_DCACHE
#endif 

#if defined(ENTER_SUPER_STANDBY) || defined(ENTER_SUPER_STANDBY_WITH_NOMMU) || defined(WATCH_DOG_RESET)
#undef MMU_OPENED
#undef SWITCH_STACK
#define SET_COPRO_REG
#define FLUSH_TLB
#define FLUSH_ICACHE
//#define INVALIDATE_DCACHE
#endif

// #define no_save __attribute__ ((section(".no_save")))
int resume0_c_part(void)
{
#ifdef SWITCH_STACK
#ifdef MMU_OPENED
	save_sp();
#else
	save_sp_nommu();
#endif
#endif
#if 1
	//busy_waiting();
	serial_init_nommu();
	serial_puts_nommu("start of resume0. \n");
#endif

#ifndef GET_CYCLE_CNT
	init_perfcounters(1, 0);
#endif

	//busy_waiting();
#ifdef SET_COPRO_REG
	set_copro_default();
	//busy_waiting();
	fake_busy_waiting();
#endif
	//busy_waiting();
	//save_mem_status(RUSUME0_START + 0x20);
	//why? flush tlb, then enable mmu abort.because 0x0000,0000 use ttbr0

#ifdef FLUSH_TLB
	mem_flush_tlb();
#endif

#ifdef FLUSH_ICACHE
	//clean i cache
	flush_icache();
	enable_cache();
#endif

#ifdef INVALIDATE_DCACHE
	/*notice: will corrupt r0 - r11*/
	invalidate_dcache();
#endif

	/* preload tlb for mem */
	//mem_preload_tlb();
	//busy_waiting();

#ifdef MMU_OPENED
	/*restore dram training area*/
	mem_memcpy((void *)DRAM_BASE_ADDR, (void *)DRAM_BACKUP_BASE_ADDR2, DRAM_TRANING_SIZE);
	//busy_waiting();
	resume1 = (int (*)(void))SRAM_FUNC_START;	
	//move resume1 code from dram to sram
	mem_memcpy((void *)SRAM_FUNC_START, (void *)&resume1_bin_start, (int)&resume1_bin_end - (int)&resume1_bin_start);
	//sync

	//jump to sram
	resume1();
	/*never get here.*/

#else

	mem_preload_tlb_nommu();
	/*restore dram training area*/
	mem_memcpy((void *)DRAM_BASE_ADDR_PA, (void *)DRAM_BACKUP_BASE_ADDR2_PA, DRAM_TRANING_SIZE);

	serial_puts_nommu("before jump to resume1. \n");
	//busy_waiting();
	resume1 = (int (*)(void))SRAM_FUNC_START_PA;	
	//move resume1 code from dram to sram
	mem_memcpy((void *)SRAM_FUNC_START_PA, (void *)&resume1_bin_start, (int)&resume1_bin_end - (int)&resume1_bin_start);
	//sync	

	//jump to sram
	resume1();
	/*never get here.*/

#endif


	while(1);
}

/*******************************************************************************
*函数名称: set_pll
*函数原型：void set_pll( void )
*函数功能: resume中用C语言编写的 调整CPU频率
*入口参数: void
*返 回 值: void
*备    注:
*******************************************************************************/
void set_pll( void )
{
	//cpus in charge this

	return ;
}


