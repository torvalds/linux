/*
 * Contact: gqyang <gqyang <at> newbietech.com>                               
 *                                                                                   
 * License terms: GNU General Public License (GPL) version 2                         
 */       
/*
 * the following code need be exceute in sram
 * before dram enter self-refresh,cpu can not access dram.
 */
 
#include "./../super_i.h"
#define RETRY_TIMES (5)

static __s32 suspend_with_nommu(void);
static __s32 suspend_with_mmu(void);

extern char *__bss_start;
extern char *__bss_end;
static int retry = RETRY_TIMES;
static struct aw_mem_para mem_para_info;

#ifdef RETURN_FROM_RESUME0_WITH_MMU
#define SWITCH_STACK
//#define DIRECT_RETRUN
//#define DRAM_ENTER_SELFRESH
//#define INIT_DRAM
//#define MEM_POWER_OFF
#define WITH_MMU
#define FLUSH_TLB
#define FLUSH_ICACHE
#define INVALIDATE_DCACHE
#endif

#ifdef RETURN_FROM_RESUME0_WITH_NOMMU
#define SWITCH_STACK
//#define DIRECT_RETRUN
#define DRAM_ENTER_SELFRESH
//#define INIT_DRAM
#define PRE_DISABLE_MMU 
 			/*it is not possible to disable mmu, because 
                                 u need to keep va == pa, before disable mmu
                                 so, the va must be in 0x0000 region.so u need to 
			 creat this mapping before jump to suspend.
                                 so u need ttbr0 
                                 to keep this mapping.  */
#define SET_COPRO_DEFAULT 
#define FLUSH_TLB
#define FLUSH_ICACHE
#define INVALIDATE_DCACHE
#define DISABLE_MMU
#define JUMP_WITH_NOMMU
#endif

#ifdef DIRECT_RETURN_FROM_SUSPEND
//#define SWITCH_STACK
#define DIRECT_RETRUN
#endif

#if defined(ENTER_SUPER_STANDBY) 
#define SWITCH_STACK
#undef PRE_DISABLE_MMU
//#define DIRECT_RETRUN
#define DRAM_ENTER_SELFRESH
//#define DISABLE_INVALIDATE_CACHE
//#define INIT_DRAM
//#define DISABLE_MMU
#define MEM_POWER_OFF
#define FLUSH_TLB
#define FLUSH_ICACHE
//#define FLUSH_DCACHE  //can not flush data, if u do this, the data that u dont want save will be saved.
#define INVALIDATE_DCACHE
#endif

#if defined(ENTER_SUPER_STANDBY_WITH_NOMMU) 
#define SWITCH_STACK
#define PRE_DISABLE_MMU
#define DRAM_ENTER_SELFRESH
#define DISABLE_MMU
#define MEM_POWER_OFF
#define FLUSH_TLB
#define FLUSH_ICACHE
#define INVALIDATE_DCACHE
//#define SET_COPRO_DEFAULT 
#endif

#ifdef WATCH_DOG_RESET
#define SWITCH_STACK
#define PRE_DISABLE_MMU
#define DRAM_ENTER_SELFRESH
#define DISABLE_MMU
#define START_WATCH_DOG
#define FLUSH_TLB
#define FLUSH_ICACHE
//#define FLUSH_DCACHE 
                                                        /*can not flush data, if u do this, the data that u dont want save willed. 
                                       * especillay, after u change the mapping table.
                                       */
#define INVALIDATE_DCACHE
#define SET_COPRO_DEFAULT 
#endif

/*
*********************************************************************************************************
*                                   SUPER STANDBY EXECUTE IN SRAM
*
* Description: super mem ,suspend to ram entry in sram.
*
* Arguments  : arg  pointer to the parameter that 
*
* Returns    : none
*
* Note       :
*********************************************************************************************************
*/

int main(void)
{
	char    *tmpPtr = (char *)&__bss_start;
	static __u32 sp_backup = 0;
	__s32 dram_size = 0;
#ifdef GET_CYCLE_CNT
	__u32 start = 0;
#endif
	serial_init();

	/* clear bss segment */
	do{*tmpPtr ++ = 0;}while(tmpPtr <= (char *)&__bss_end);

	/* save stack pointer registger, switch stack to sram */
	//mark it, just for test 
#ifdef SWITCH_STACK
#ifdef PRE_DISABLE_MMU
	//busy_waiting();
	sp_backup = save_sp_nommu();
#else
	sp_backup = save_sp();
#endif
#endif	


	/* flush data and instruction tlb, there is 32 items of data tlb and 32 items of instruction tlb,
	The TLB is normally allocated on a rotating basis. The oldest entry is always the next allocated */
#ifdef FLUSH_TLB
	mem_flush_tlb();
#ifdef PRE_DISABLE_MMU
	/* preload tlb for mem */
	//busy_waiting();
	//mem_preload_tlb_nommu(); //0x0000 mapping is not large enough for preload nommu tlb
						//eg: 0x01c2.... is not in the 0x0000,0000 range.
	mem_preload_tlb();
#else
	/* preload tlb for mem */
	mem_preload_tlb();
#endif

#endif	


	/*get input para*/
	mem_memcpy((void *)&mem_para_info, (void *)(DRAM_BACKUP_BASE_ADDR1), sizeof(mem_para_info));

	if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_STANDBY)){
		printk("before init devices. \n");
	}
	/* initialise mem modules */
	mem_int_init();
	mem_twi_init(AXP_IICBUS);

	if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_STANDBY)){
		printk("before power init.\n");
	}

    if (likely(mem_para_info.axp_enable))
    {
        while(mem_power_init(mem_para_info.axp_event)&&--retry){
            printk("mem_power_init failed. \n");
            ;
        }
        if(0 == retry){
            goto mem_power_init_err;
        }else{
            retry = RETRY_TIMES;
        }
    }
	
	/* dram enter self-refresh */
#ifdef DRAM_ENTER_SELFRESH
	if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_CHECK_CRC))
    {
        standby_dram_crc(0);
    }
#endif
	
	//need to mask all the int src
	if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_STANDBY)){
		printk("before power off. \n");
	}
#ifdef DISABLE_MMU
	if(suspend_with_nommu()){
		goto suspend_err;
	}
#else
	if(suspend_with_mmu()){
		goto suspend_err;
	}
#endif	
	//notice: never get here, so need watchdog, not busy_waiting.


suspend_err:
	if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_STANDBY)){
		printk("err: \n");
	}
#ifdef DRAM_ENTER_SELFRESH
	init_DRAM(&mem_para_info.dram_para);	
#endif

suspend_dram_err:
mem_power_init_err:

#if 0
    if (likely(mem_para_info.axp_enable))
    {
        while(mem_power_exit(mem_para_info.axp_event)&&--retry){
            ;
        }
        if(0 == retry){
            return -1;
        }else{
            retry = RETRY_TIMES;
        }
    }
#endif

	//busy_waiting();
	restore_sp(sp_backup);

	return -1;
}

static __s32 suspend_with_nommu(void)
{
		disable_mmu();
		mem_flush_tlb();
		//after disable mmu, it is time to preload nommu, need to access dram?
		mem_preload_tlb_nommu();
		//while(1);
#ifdef SET_COPRO_DEFAULT
			set_copro_default();

#endif

#ifdef DRAM_ENTER_SELFRESH
    if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_STANDBY)){
        printk_nommu("before dram enter selfresh. \n");
    }
	if(dram_power_save_process(1)){
		goto mem_power_off_nommu_err;
	}
	
	mem_clk_dramgating_nommu(0);
#endif
	
#ifdef JUMP_WITH_NOMMU
			//jump_to_resume0_nommu(0x40100000);
			jump_to_resume0(DRAM_BACKUP_BASE_ADDR_PA);
#endif 
	
#ifdef MEM_POWER_OFF
			/*power off*/
			/*NOTICE: not support to power off yet after disable mmu.
			  * because twi use virtual addr. 
			  */
			if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_STANDBY)){
				printk_nommu("notify pmu to power off. \n");
			}
			
            if (likely(mem_para_info.axp_enable))
            {
                int ret;
                while(ret = mem_power_off_nommu()&&--retry){
                    if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_STANDBY)){
                        printk_nommu("notify pmu to power off:%d: failed one time, retry.... \n", ret);
                    }
                    ;
                }
                if(0 == retry){
                    goto mem_power_off_nommu_err;
                    
                }else{
                    retry = RETRY_TIMES;
                }
            }

#endif
			return 0;

mem_power_off_nommu_err:
	enable_mmu();
	mem_flush_tlb();
	mem_preload_tlb();
	
	return -1;

}

static __s32 suspend_with_mmu(void)
{
	
#ifdef SET_COPRO_DEFAULT
	set_copro_default();
#endif
	
	
#ifdef MEM_POWER_OFF
	/*power off*/
    if (likely(mem_para_info.axp_enable))
    {
        while(mem_power_off()&&--retry){
            ;
        }
        if(0 == retry){
            return -1;
        }else{
            retry = RETRY_TIMES;
        }           
    }
#endif
	
#ifdef WITH_MMU
	jump_to_resume0(DRAM_BACKUP_BASE_ADDR);
#endif

	return 0;

}

