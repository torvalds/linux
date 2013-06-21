/* these code will be removed to sram.
 * function: open the mmu, and jump to dram, for continuing resume*/
#include "./../super_i.h"

static struct aw_mem_para mem_para_info;

extern char *__bss_start;
extern char *__bss_end;
static __u32 sp_backup;
static char    *tmpPtr = (char *)&__bss_start;
static __u32 status = 0; 

#ifdef RETURN_FROM_RESUME0_WITH_MMU
#define MMU_OPENED
#undef POWER_OFF
#define FLUSH_TLB
#define FLUSH_ICACHE
#define INVALIDATE_DCACHE
#endif

#ifdef RETURN_FROM_RESUME0_WITH_NOMMU
#undef MMU_OPENED
#undef POWER_OFF
#define FLUSH_TLB
#define FLUSH_ICACHE
#define INVALIDATE_DCACHE
#endif

#if defined(ENTER_SUPER_STANDBY) || defined(ENTER_SUPER_STANDBY_WITH_NOMMU) || defined(WATCH_DOG_RESET)
#undef MMU_OPENED
#define POWER_OFF
#define FLUSH_TLB
#define FLUSH_ICACHE
#define INVALIDATE_DCACHE
#endif

static void restore_ccmu(void);

int main(void)
{
    /* clear bss segment */
    do{*tmpPtr ++ = 0;}while(tmpPtr <= (char *)&__bss_end);
    
    
#ifdef MMU_OPENED
    //move other storage to sram: saved_resume_pointer(virtual addr), saved_mmu_state
    mem_memcpy((void *)&mem_para_info, (void *)(DRAM_BACKUP_BASE_ADDR1), sizeof(mem_para_info));
#else
    mem_preload_tlb_nommu();
    /*switch stack*/
    //save_mem_status_nommu(RESUME1_START |0x02);
    //move other storage to sram: saved_resume_pointer(virtual addr), saved_mmu_state
    mem_memcpy((void *)&mem_para_info, (void *)(DRAM_BACKUP_BASE_ADDR1_PA), sizeof(mem_para_info));
    /*restore mmu configuration*/
    restore_mmu_state(&(mem_para_info.saved_mmu_state));
    //disable_dcache();

#endif
    //serial_init();
    if(unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_RESUME)){
        serial_puts("after restore mmu. \n");
    }
    if (unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_CHECK_CRC))
    {
        standby_dram_crc(1);
    }


//after open mmu mapping
#ifdef FLUSH_TLB
    //busy_waiting();
    mem_flush_tlb();
    mem_preload_tlb();
#endif

#ifdef FLUSH_ICACHE
    //clean i cache
    flush_icache();
#endif
    
    //twi freq?
    setup_twi_env();
    mem_twi_init(AXP_IICBUS);

#ifdef POWER_OFF
    restore_ccmu();
#endif

    /*restore pmu config*/
#ifdef POWER_OFF
    if (likely(mem_para_info.axp_enable))
    {
        mem_power_exit(mem_para_info.axp_event);
    }

    /* disable watch-dog: coresponding with boot0 */
    mem_tmr_disable_watchdog();
#endif

//before jump to late_resume    
#ifdef FLUSH_TLB
    mem_flush_tlb();
#endif

#ifdef FLUSH_ICACHE
    //clean i cache
    flush_icache();
#endif

    if (unlikely((mem_para_info.debug_mask)&PM_STANDBY_PRINT_CHECK_CRC))
    {
        serial_puts("before jump_to_resume. \n");
    }
    //before jump, invalidate data
    jump_to_resume((void *)mem_para_info.resume_pointer, mem_para_info.saved_runtime_context_svc);
    
    return;
}

void restore_ccmu(void)
{
    /* gating off dram clock */
    //mem_clk_dramgating(0);
    int i=0;
#if 0   
    for(i=0; i<6; i++){
        dram_hostport_on_off(i, 0);
    }
    
    for(i=16; i<31; i++){
        dram_hostport_on_off(i, 0);
    }
#endif


#if 1
    if (likely(mem_para_info.axp_enable))
    {
        while( 0 != mem_set_voltage(POWER_VOL_DCDC2, mem_para_info.suspend_dcdc2)){
                ;
        }
        while(0 != mem_set_voltage(POWER_VOL_DCDC3, mem_para_info.suspend_dcdc3)){
                ;
        }
    }
#endif

    change_runtime_env(1);
    delay_ms(10);

    mem_clk_setdiv(&mem_para_info.clk_div);
    mem_clk_set_pll_factor(&mem_para_info.pll_factor);
    change_runtime_env(1);
    delay_ms(mem_para_info.suspend_delay_ms);

#if 0
    /* gating on dram clock */
    //mem_clk_dramgating(1);
    for(i=0; i<6; i++){
        dram_hostport_on_off(i, 1);
    }
    
    for(i=16; i<31; i++){
        dram_hostport_on_off(i, 1);
    }
#endif

    return;
}



