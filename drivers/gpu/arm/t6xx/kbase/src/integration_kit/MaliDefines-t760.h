
 //chenli add 
#include <linux/kernel.h>
#define exit(no)
int Mali_RdReg(int unit,int core, int regnum);
void Mali_WrReg(int unit,int core,int regnum,int value);
int Mali_AnyInterruptCheck(int type, int i_mask, int i_value);
void Mali_clear_irqs_and_set_all_masks (void);
void Mali_clear_and_set_masks_for_gpu_irq (void);
void Mali_clear_and_set_masks_for_mmu_irq (void);
void Mali_clear_and_set_masks_for_job_irq (void);
void Mali_SetBase(int* base);
void Mali_Reset(void) ;
void *Mali_LdMem(void *srcptr,int size,int ttb_base);
void Mali_InitPerfCountersFn(int core, int cnt_num, int cnt_id);
void Mali_InitPerfCounters();
void Mali_InitPerfCountersFn(int core, int cnt_num, int cnt_id);
void Mali_ReadPerfCounters();
int Mali_InterruptCheck(int i_mask, int i_value);
void Mali_CheckReg(int unit,int core, int regnum, int value);


//end

/**********************************************
 GPU ID 
 **********************************************/


#ifdef GPU_ID_VALUE
#else
  #define GPU_ID_VALUE 0x69560002
#endif

/**********************************************
 GPU CONFIG 
 **********************************************/
#ifdef GPU_CONFIG_N_CONTROL_BASE
#else
  #define GPU_CONFIG_N_CONTROL_BASE 0x0000
#endif

#ifdef JOB_CONTROL_BASE
#else
  #define JOB_CONTROL_BASE 0x1000
#endif

#ifdef MEM_MANAGEMENT_BASE
#else
  #define MEM_MANAGEMENT_BASE 0x2000
#endif




#ifdef GPU_IRQ_RAWSTAT
#else
  #define GPU_IRQ_RAWSTAT     0x0020
#endif

#ifdef GPU_IRQ_CLEAR
#else
  #define GPU_IRQ_CLEAR       0x0024
#endif

#ifdef GPU_IRQ_MASK 
#else
  #define GPU_IRQ_MASK        0x0028
#endif

#ifdef GPU_IRQ_STATUS
#else
  #define GPU_IRQ_STATUS      0x002C
#endif

#ifdef GPU_COMMAND
#else
  #define GPU_COMMAND           0x0030
#endif

#ifdef GPU_STATUS
#else
  #define GPU_STATUS            0x0034
#endif

#ifdef GPU_FAULTSTATUS
#else
  #define GPU_FAULTSTATUS       0x003C
#endif



#ifdef CYCLE_COUNT_LO
#else
  #define CYCLE_COUNT_LO        0x0090
#endif

#ifdef CYCLE_COUNT_HI
#else
  #define CYCLE_COUNT_HI        0x0094
#endif

#ifdef TIMESTAMP_LO
#else
  #define TIMESTAMP_LO          0x0098
#endif

#ifdef TIMESTAMP_HI
#else
  #define TIMESTAMP_HI          0x009c
#endif

#ifdef L2_MMU_CONFIG
#else
  #define L2_MMU_CONFIG      0x0f0c
#endif


#ifdef JOB_IRQ_RAWSTAT
#else
  #define JOB_IRQ_RAWSTAT         0x1000
#endif

#ifdef JOB_IRQ_CLEAR
#else
  #define JOB_IRQ_CLEAR           0x1004
#endif

#ifdef JOB_IRQ_MASK
#else
  #define JOB_IRQ_MASK            0x1008
#endif

#ifdef JOB_IRQ_STATUS
#else
  #define JOB_IRQ_STATUS          0x100c
#endif

#ifdef JOB_IRQ_THROTTLE
#else
  #define JOB_IRQ_THROTTLE        0x1014
#endif





#ifdef SHADER_PWRON_LO
#else
  #define SHADER_PWRON_LO         0x0180
#endif
#ifdef SHADER_PWROFF_LO
#else
  #define SHADER_PWROFF_LO        0x01c0
#endif
#ifdef SHADER_READY_LO
#else
  #define SHADER_READY_LO         0x0140
#endif
#ifdef SHADER_PWRTRANS_LO
#else
  #define SHADER_PWRTRANS_LO         0x0200
#endif
#ifdef SHADER_PWRACTIVE_LO
#else
  #define SHADER_PWRACTIVE_LO         0x0240
#endif

#ifdef SHADER_PWRON_HI
#else
  #define SHADER_PWRON_HI         0x0184
#endif


#ifdef TILER_PWRON_LO
#else
  #define TILER_PWRON_LO          0x0190
#endif
#ifdef TILER_PWROFF_LO
#else
  #define TILER_PWROFF_LO         0x01d0
#endif
#ifdef TILER_READY_LO
#else
  #define TILER_READY_LO          0x0150
#endif
#ifdef TILER_PWRTRANS_LO
#else
  #define TILER_PWRTRANS_LO         0x0210
#endif
#ifdef TILER_PWRACTIVE_LO
#else
  #define TILER_PWRACTIVE_LO         0x0250
#endif

#ifdef TILER_PWRON_HI
#else
  #define TILER_PWRON_HI          0x0194
#endif

#ifdef L2_PWRON_LO
#else
  #define L2_PWRON_LO             0x01A0
#endif

#ifdef L2_PWRON_HI
#else
  #define L2_PWRON_HI             0x01A4
#endif

#ifdef L2_PWROFF_LO
#else
  #define L2_PWROFF_LO         0x01e0
#endif

#ifdef L2_READY_LO
#else
  #define L2_READY_LO          0x0160
#endif

#ifdef L2_PWRTRANS_LO
#else
  #define L2_PWRTRANS_LO         0x0220
#endif

#ifdef L2_PWRACTIVE_LO
#else
  #define L2_PWRACTIVE_LO         0x0260
#endif



/**********************************************
 JSn
 **********************************************/
#ifdef JSn_BASE
#else
  #define JSn_BASE                0x800
#endif

#ifdef JSn_CONFIG
#else
  #define JSn_CONFIG              0x18
#endif

#ifdef JSn_HEAD_NEXT_LO
#else
  #define JSn_HEAD_NEXT_LO        0x40
#endif

#ifdef JSn_HEAD_NEXT_HI
#else
  #define JSn_HEAD_NEXT_HI        0x44
#endif

#ifdef JSn_AFFINITY_NEXT_LO
#else
  #define JSn_AFFINITY_NEXT_LO    0x50
#endif

#ifdef JSn_AFFINITY_NEXT_HI
#else
  #define JSn_AFFINITY_NEXT_HI    0x54
#endif

#ifdef JSn_CONFIG_NEXT
#else
  #define JSn_CONFIG_NEXT         0x58
#endif

#ifdef JSn_COMMAND_NEXT
#else
  #define JSn_COMMAND_NEXT        0x60
#endif

#ifdef JSn_STATUS
#else
  #define JSn_STATUS              0x24
#endif

/**********************************************
 JS0 
 **********************************************/
 
#ifdef JS0_HEAD_NEXT_LO
#else
  #define JS0_HEAD_NEXT_LO        0x1840
#endif

#ifdef JS0_HEAD_NEXT_HI
#else
  #define JS0_HEAD_NEXT_HI        0x1844
#endif

#ifdef JS0_AFFINITY_NEXT_LO
#else
  #define JS0_AFFINITY_NEXT_LO    0x1850
#endif

#ifdef JS0_AFFINITY_NEXT_HI
#else
  #define JS0_AFFINITY_NEXT_HI    0x1850
#endif

#ifdef JS0_CONFIG_NEXT
#else
  #define JS0_CONFIG_NEXT         0x1858
#endif

#ifdef JS0_COMMAND_NEXT
#else
  #define JS0_COMMAND_NEXT        0x1860
#endif

#ifdef JS0_STATUS
#else
  #define JS0_STATUS              0x1824
#endif
 
/**********************************************
 JS1 
 **********************************************/
#ifdef JS1_HEAD_NEXT_LO
#else
  #define JS1_HEAD_NEXT_LO        0x18c0
#endif

#ifdef JS1_HEAD_NEXT_HI
#else
  #define JS1_HEAD_NEXT_HI        0x18c4
#endif

#ifdef JS1_AFFINITY_NEXT_LO
#else
  #define JS1_AFFINITY_NEXT_LO    0x18d0
#endif

#ifdef JS1_AFFINITY_NEXT_HI
#else
  #define JS1_AFFINITY_NEXT_HI    0x18d4
#endif

#ifdef JS1_CONFIG_NEXT
#else
  #define JS1_CONFIG_NEXT         0x18d8
#endif

#ifdef JS1_COMMAND_NEXT
#else
  #define JS1_COMMAND_NEXT        0x18e0
#endif

/**********************************************
 JS2 
 **********************************************/
#ifdef JS2_HEAD_NEXT_LO
#else
  #define JS2_HEAD_NEXT_LO        0x1940
#endif

#ifdef JS2_HEAD_NEXT_HI
#else
  #define JS2_HEAD_NEXT_HI        0x1944
#endif

#ifdef JS2_AFFINITY_NEXT_LO
#else
  #define JS2_AFFINITY_NEXT_LO    0x1950
#endif

#ifdef JS2_AFFINITY_NEXT_HI
#else
  #define JS2_AFFINITY_NEXT_HI    0x1954
#endif

#ifdef JS2_CONFIG_NEXT
#else
  #define JS2_CONFIG_NEXT         0x1958
#endif

#ifdef JS2_COMMAND_NEXT
#else
  #define JS2_COMMAND_NEXT        0x1960
#endif

/**********************************************
 MMU 
 **********************************************/
#ifdef MMU_IRQ_RAWSTAT
#else
  #define MMU_IRQ_RAWSTAT            0x2000
#endif

#ifdef MMU_IRQ_CLEAR
#else
  #define MMU_IRQ_CLEAR              0x2004
#endif

#ifdef MMU_IRQ_MASK
#else
  #define MMU_IRQ_MASK               0x2008
#endif

#ifdef MMU_IRQ_STATUS
#else
  #define MMU_IRQ_STATUS             0x200c
#endif

#ifdef MMU_IRQ_STATUS
#else
  #define MMU_IRQ_STATUS             0x200c
#endif

/**********************************************
 Memory management regs 
 **********************************************/
#ifdef ASn_BASE
#else
  #define ASn_BASE                    0x400
#endif

#ifdef ASn_TRANSTAB_LO
#else
  #define ASn_TRANSTAB_LO             0x00
#endif

#ifdef ASn_TRANSTAB_HI
#else
  #define ASn_TRANSTAB_HI             0x04
#endif

#ifdef ASn_MEMATTR_LO
#else
  #define ASn_MEMATTR_LO              0x08
#endif

#ifdef ASn_MEMATTR_HI
#else
  #define ASn_MEMATTR_HI              0x0c
#endif

#ifdef ASn_LOCKADDR_LO
#else
  #define ASn_LOCKADDR_LO             0x10
#endif

#ifdef ASn_LOCKADDR_HI
#else
  #define ASn_LOCKADDR_HI             0x14
#endif

#ifdef ASn_COMMAND
#else
  #define ASn_COMMAND                 0x18
#endif

#ifdef ASn_FAULTSTATUS
#else
  #define ASn_FAULTSTATUS             0x1c
#endif

#ifdef ASn_FAULTADDR_LO
#else
  #define ASn_FAULTADDR_LO            0x20
#endif

#ifdef ASn_FAULTADDR_HI
#else
  #define ASn_FAULTADDR_HI            0x24
#endif

#ifdef ASn_STATUS
#else
  #define ASn_STATUS                  0x28
#endif


/**********************************************
 PRFCNT regs 
 **********************************************/
#ifdef PRFCNT_CONFIG
#else
  #define PRFCNT_CONFIG              0x068
#endif

#ifdef PRFCNT_JM_EN
#else
  #define PRFCNT_JM_EN               0x06C
#endif

#ifdef PRFCNT_SHADER_EN
#else
  #define PRFCNT_SHADER_EN           0x070
#endif

#ifdef PRFCNT_TILER_EN
#else 
  #define PRFCNT_TILER_EN            0x074
#endif

#ifdef PRFCNT_L3_CACHE_EN
#else
  #define PRFCNT_L3_CACHE_EN         0x078
#endif

#ifdef PRFCNT_MMU_L2_EN
#else
  #define PRFCNT_MMU_L2_EN           0x07C
#endif


/**********************************************
 GPU COMMANDS 
 **********************************************/

#ifdef GPU_COMMAND__CYCLE_COUNT_START
#else
  #define GPU_COMMAND__CYCLE_COUNT_START 5
#endif


#ifdef GPU_COMMAND__NOP
#else
  #define GPU_COMMAND__NOP 0
#endif


#ifdef GPU_COMMAND__SOFT_RESET
#else
  #define GPU_COMMAND__SOFT_RESET 1
#endif


#ifdef GPU_COMMAND__HARD_RESET
#else
  #define GPU_COMMAND__HARD_RESET 2
#endif


#ifdef GPU_COMMAND__PRFCNT_CLEAR
#else
  #define GPU_COMMAND__PRFCNT_CLEAR 3
#endif


#ifdef GPU_COMMAND__PRFCNT_SAMPLE
#else
  #define GPU_COMMAND__PRFCNT_SAMPLE 4
#endif

#ifdef GPU_COMMAND__CYCLE_COUNT_START
#else
  #define GPU_COMMAND__CYCLE_COUNT_START 5
#endif

#ifdef GPU_COMMAND__CYCLE_COUNT_STOP
#else
  #define GPU_COMMAND__CYCLE_COUNT_STOP 6
#endif

#ifdef GPU_COMMAND__CLEAN_CACHES
#else
  #define GPU_COMMAND__CLEAN_CACHES 7
#endif

#ifdef GPU_COMMAND__CLEAN_INV_CACHES
#else
  #define GPU_COMMAND__CLEAN_INV_CACHES 8
#endif


/**********************************************
 JSn_STATUS  DEFINES
 **********************************************/
#ifdef JSn_STATUS__ACTIVE
#else
  #define JSn_STATUS__ACTIVE 0x8
#endif

/**********************************************
 Clock gating overrides
 **********************************************/
#ifdef JM_CLOCK_GATING_OVERRIDE
#else
  #define JM_CLOCK_GATING_OVERRIDE      0xf00
#endif

#ifdef SC_CLOCK_GATING_OVERRIDE
#else
  #define SC_CLOCK_GATING_OVERRIDE      0xf04
#endif

#ifdef TILER_CLOCK_GATING_OVERRIDE
#else
  #define TILER_CLOCK_GATING_OVERRIDE   0xf08
#endif

#ifdef L2_CLOCK_GATING_OVERRIDE
#else
  #define L2_CLOCK_GATING_OVERRIDE      0xf0C
#endif


/**********************************************
 Hardcoded regs (autogenerated from the toplevel testbench file
 common/apb/mali_t760_apb/mali_t760_enums.sv)
 **********************************************/

#ifdef GPU_ID
#else
	#define GPU_ID 0x0
#endif

#ifdef L2_FEATURES
#else
	#define L2_FEATURES 0x4
#endif

#ifdef L3_FEATURES
#else
	#define L3_FEATURES 0x8
#endif

#ifdef TILER_FEATURES
#else
	#define TILER_FEATURES 0xC
#endif

#ifdef MEM_FEATURES
#else
	#define MEM_FEATURES 0x10
#endif

#ifdef MMU_FEATURES
#else
	#define MMU_FEATURES 0x14
#endif

#ifdef AS_PRESENT
#else
	#define AS_PRESENT 0x18
#endif

#ifdef JS_PRESENT
#else
	#define JS_PRESENT 0x1C
#endif

#ifdef GPU_IRQ_RAWSTAT
#else
	#define GPU_IRQ_RAWSTAT 0x20
#endif

#ifdef GPU_IRQ_CLEAR
#else
	#define GPU_IRQ_CLEAR 0x24
#endif

#ifdef GPU_IRQ_MASK
#else
	#define GPU_IRQ_MASK 0x28
#endif

#ifdef GPU_IRQ_STATUS
#else
	#define GPU_IRQ_STATUS 0x2C
#endif

#ifdef GPU_COMMAND
#else
	#define GPU_COMMAND 0x30
#endif

#ifdef GPU_STATUS
#else
	#define GPU_STATUS 0x34
#endif

#ifdef GPU_FAULTSTATUS
#else
	#define GPU_FAULTSTATUS 0x3C
#endif

#ifdef GPU_FAULTADDRESS_LO
#else
	#define GPU_FAULTADDRESS_LO 0x40
#endif

#ifdef GPU_FAULTADDRESS_HI
#else
	#define GPU_FAULTADDRESS_HI 0x44
#endif

#ifdef PWR_KEY
#else
	#define PWR_KEY 0x50
#endif

#ifdef PWR_OVERRIDE0
#else
	#define PWR_OVERRIDE0 0x54
#endif

#ifdef PWR_OVERRIDE1
#else
	#define PWR_OVERRIDE1 0x58
#endif

#ifdef PRFCNT_BASE_LO
#else
	#define PRFCNT_BASE_LO 0x60
#endif

#ifdef PRFCNT_BASE_HI
#else
	#define PRFCNT_BASE_HI 0x64
#endif

#ifdef PRFCNT_CONFIG
#else
	#define PRFCNT_CONFIG 0x68
#endif

#ifdef PRFCNT_JM_EN
#else
	#define PRFCNT_JM_EN 0x6C
#endif

#ifdef PRFCNT_SHADER_EN
#else
	#define PRFCNT_SHADER_EN 0x70
#endif

#ifdef PRFCNT_TILER_EN
#else
	#define PRFCNT_TILER_EN 0x74
#endif

#ifdef PRFCNT_L3_CACHE_EN
#else
	#define PRFCNT_L3_CACHE_EN 0x78
#endif

#ifdef PRFCNT_MMU_L2_EN
#else
	#define PRFCNT_MMU_L2_EN 0x7C
#endif

#ifdef CYCLE_COUNT_LO
#else
	#define CYCLE_COUNT_LO 0x90
#endif

#ifdef CYCLE_COUNT_HI
#else
	#define CYCLE_COUNT_HI 0x94
#endif

#ifdef TIMESTAMP_LO
#else
	#define TIMESTAMP_LO 0x98
#endif

#ifdef TIMESTAMP_HI
#else
	#define TIMESTAMP_HI 0x9C
#endif

#ifdef TEX_FEATURES_0
#else
	#define TEX_FEATURES_0 0xB0
#endif

#ifdef TEX_FEATURES_1
#else
	#define TEX_FEATURES_1 0xB4
#endif

#ifdef TEX_FEATURES_2
#else
	#define TEX_FEATURES_2 0xB8
#endif

#ifdef JS0_FEATURES
#else
	#define JS0_FEATURES 0xC0
#endif

#ifdef JS1_FEATURES
#else
	#define JS1_FEATURES 0xC4
#endif

#ifdef JS2_FEATURES
#else
	#define JS2_FEATURES 0xC8
#endif

#ifdef JS3_FEATURES
#else
	#define JS3_FEATURES 0xCC
#endif

#ifdef JS4_FEATURES
#else
	#define JS4_FEATURES 0xD0
#endif

#ifdef JS5_FEATURES
#else
	#define JS5_FEATURES 0xD4
#endif

#ifdef JS6_FEATURES
#else
	#define JS6_FEATURES 0xD8
#endif

#ifdef JS7_FEATURES
#else
	#define JS7_FEATURES 0xDC
#endif

#ifdef JS8_FEATURES
#else
	#define JS8_FEATURES 0xE0
#endif

#ifdef JS9_FEATURES
#else
	#define JS9_FEATURES 0xE4
#endif

#ifdef JS10_FEATURES
#else
	#define JS10_FEATURES 0xE8
#endif

#ifdef JS11_FEATURES
#else
	#define JS11_FEATURES 0xEC
#endif

#ifdef JS12_FEATURES
#else
	#define JS12_FEATURES 0xF0
#endif

#ifdef JS13_FEATURES
#else
	#define JS13_FEATURES 0xF4
#endif

#ifdef JS16_FEATURES
#else
	#define JS16_FEATURES 0xF8
#endif

#ifdef JS15_FEATURES
#else
	#define JS15_FEATURES 0xFC
#endif

#ifdef SHADER_PRESENT_LO
#else
	#define SHADER_PRESENT_LO 0x100
#endif

#ifdef SHADER_PRESENT_HI
#else
	#define SHADER_PRESENT_HI 0x104
#endif

#ifdef TILER_PRESENT_LO
#else
	#define TILER_PRESENT_LO 0x110
#endif

#ifdef TILER_PRESENT_HI
#else
	#define TILER_PRESENT_HI 0x114
#endif

#ifdef L2_PRESENT_LO
#else
	#define L2_PRESENT_LO 0x120
#endif

#ifdef L2_PRESENT_HI
#else
	#define L2_PRESENT_HI 0x124
#endif

#ifdef L3_PRESENT_LO
#else
	#define L3_PRESENT_LO 0x130
#endif

#ifdef L3_PRESENT_HI
#else
	#define L3_PRESENT_HI 0x134
#endif

#ifdef SHADER_READY_LO
#else
	#define SHADER_READY_LO 0x140
#endif

#ifdef SHADER_READY_HI
#else
	#define SHADER_READY_HI 0x144
#endif

#ifdef TILER_READY_LO
#else
	#define TILER_READY_LO 0x150
#endif

#ifdef TILER_READY_HI
#else
	#define TILER_READY_HI 0x154
#endif

#ifdef L2_READY_LO
#else
	#define L2_READY_LO 0x160
#endif

#ifdef L2_READY_HI
#else
	#define L2_READY_HI 0x164
#endif

#ifdef L3_READY_LO
#else
	#define L3_READY_LO 0x170
#endif

#ifdef L3_READY_HI
#else
	#define L3_READY_HI 0x174
#endif

#ifdef SHADER_PWRON_LO
#else
	#define SHADER_PWRON_LO 0x180
#endif

#ifdef SHADER_PWRON_HI
#else
	#define SHADER_PWRON_HI 0x184
#endif

#ifdef TILER_PWRON_LO
#else
	#define TILER_PWRON_LO 0x190
#endif

#ifdef TILER_PWRON_HI
#else
	#define TILER_PWRON_HI 0x194
#endif

#ifdef L2_PWRON_LO
#else
	#define L2_PWRON_LO 0x1A0
#endif

#ifdef L2_PWRON_HI
#else
	#define L2_PWRON_HI 0x1A4
#endif

#ifdef L3_PWRON_LO
#else
	#define L3_PWRON_LO 0x1B0
#endif

#ifdef L3_PWRON_HI
#else
	#define L3_PWRON_HI 0x1B4
#endif

#ifdef SHADER_PWROFF_LO
#else
	#define SHADER_PWROFF_LO 0x1C0
#endif

#ifdef SHADER_PWROFF_HI
#else
	#define SHADER_PWROFF_HI 0x1C4
#endif

#ifdef TILER_PWROFF_LO
#else
	#define TILER_PWROFF_LO 0x1D0
#endif

#ifdef TILER_PWROFF_HI
#else
	#define TILER_PWROFF_HI 0x1D4
#endif

#ifdef L2_PWROFF_LO
#else
	#define L2_PWROFF_LO 0x1E0
#endif

#ifdef L2_PWROFF_HI
#else
	#define L2_PWROFF_HI 0x1E4
#endif

#ifdef L3_PWROFF_LO
#else
	#define L3_PWROFF_LO 0x1F0
#endif

#ifdef L3_PWROFF_HI
#else
	#define L3_PWROFF_HI 0x1F4
#endif

#ifdef SHADER_PWRTRANS_LO
#else
	#define SHADER_PWRTRANS_LO 0x200
#endif

#ifdef SHADER_PWRTRANS_HI
#else
	#define SHADER_PWRTRANS_HI 0x204
#endif

#ifdef TILER_PWRTRANS_LO
#else
	#define TILER_PWRTRANS_LO 0x210
#endif

#ifdef TILER_PWRTRANS_HI
#else
	#define TILER_PWRTRANS_HI 0x214
#endif

#ifdef L2_PWRTRANS_LO
#else
	#define L2_PWRTRANS_LO 0x220
#endif

#ifdef L2_PWRTRANS_HI
#else
	#define L2_PWRTRANS_HI 0x224
#endif

#ifdef L3_PWRTRANS_LO
#else
	#define L3_PWRTRANS_LO 0x230
#endif

#ifdef L3_PWRTRANS_HI
#else
	#define L3_PWRTRANS_HI 0x234
#endif

#ifdef SHADER_PWRACTIVE_LO
#else
	#define SHADER_PWRACTIVE_LO 0x240
#endif

#ifdef SHADER_PWRACTIVE_HI
#else
	#define SHADER_PWRACTIVE_HI 0x244
#endif

#ifdef TILER_PWRACTIVE_LO
#else
	#define TILER_PWRACTIVE_LO 0x250
#endif

#ifdef TILER_PWRACTIVE_HI
#else
	#define TILER_PWRACTIVE_HI 0x254
#endif

#ifdef L2_PWRACTIVE_LO
#else
	#define L2_PWRACTIVE_LO 0x260
#endif

#ifdef L2_PWRACTIVE_HI
#else
	#define L2_PWRACTIVE_HI 0x264
#endif

#ifdef L3_PWRACTIVE_LO
#else
	#define L3_PWRACTIVE_LO 0x270
#endif

#ifdef L3_PWRACTIVE_HI
#else
	#define L3_PWRACTIVE_HI 0x274
#endif

#ifdef USER_IN_00
#else
	#define USER_IN_00 0x400
#endif

#ifdef USER_IN_01
#else
	#define USER_IN_01 0x404
#endif

#ifdef USER_IN_02
#else
	#define USER_IN_02 0x408
#endif

#ifdef USER_IN_03
#else
	#define USER_IN_03 0x40C
#endif

#ifdef USER_IN_04
#else
	#define USER_IN_04 0x410
#endif

#ifdef USER_IN_05
#else
	#define USER_IN_05 0x414
#endif

#ifdef USER_IN_06
#else
	#define USER_IN_06 0x418
#endif

#ifdef USER_IN_07
#else
	#define USER_IN_07 0x41C
#endif

#ifdef USER_IN_08
#else
	#define USER_IN_08 0x420
#endif

#ifdef USER_IN_09
#else
	#define USER_IN_09 0x424
#endif

#ifdef USER_IN_10
#else
	#define USER_IN_10 0x428
#endif

#ifdef USER_IN_11
#else
	#define USER_IN_11 0x42C
#endif

#ifdef USER_IN_12
#else
	#define USER_IN_12 0x430
#endif

#ifdef USER_IN_13
#else
	#define USER_IN_13 0x434
#endif

#ifdef USER_IN_14
#else
	#define USER_IN_14 0x438
#endif

#ifdef USER_IN_15
#else
	#define USER_IN_15 0x43C
#endif

#ifdef USER_IN_16
#else
	#define USER_IN_16 0x440
#endif

#ifdef USER_IN_17
#else
	#define USER_IN_17 0x444
#endif

#ifdef USER_IN_18
#else
	#define USER_IN_18 0x448
#endif

#ifdef USER_IN_19
#else
	#define USER_IN_19 0x44C
#endif

#ifdef USER_IN_20
#else
	#define USER_IN_20 0x450
#endif

#ifdef USER_IN_21
#else
	#define USER_IN_21 0x454
#endif

#ifdef USER_IN_22
#else
	#define USER_IN_22 0x458
#endif

#ifdef USER_IN_23
#else
	#define USER_IN_23 0x45C
#endif

#ifdef USER_IN_24
#else
	#define USER_IN_24 0x460
#endif

#ifdef USER_IN_25
#else
	#define USER_IN_25 0x464
#endif

#ifdef USER_IN_26
#else
	#define USER_IN_26 0x468
#endif

#ifdef USER_IN_27
#else
	#define USER_IN_27 0x46C
#endif

#ifdef USER_IN_28
#else
	#define USER_IN_28 0x470
#endif

#ifdef USER_IN_29
#else
	#define USER_IN_29 0x474
#endif

#ifdef USER_IN_30
#else
	#define USER_IN_30 0x478
#endif

#ifdef USER_IN_31
#else
	#define USER_IN_31 0x47C
#endif

#ifdef USER_OUT_00
#else
	#define USER_OUT_00 0x500
#endif

#ifdef USER_OUT_01
#else
	#define USER_OUT_01 0x504
#endif

#ifdef USER_OUT_02
#else
	#define USER_OUT_02 0x508
#endif

#ifdef USER_OUT_03
#else
	#define USER_OUT_03 0x50C
#endif

#ifdef USER_OUT_04
#else
	#define USER_OUT_04 0x510
#endif

#ifdef USER_OUT_05
#else
	#define USER_OUT_05 0x514
#endif

#ifdef USER_OUT_06
#else
	#define USER_OUT_06 0x518
#endif

#ifdef USER_OUT_07
#else
	#define USER_OUT_07 0x51C
#endif

#ifdef USER_OUT_08
#else
	#define USER_OUT_08 0x520
#endif

#ifdef USER_OUT_09
#else
	#define USER_OUT_09 0x524
#endif

#ifdef USER_OUT_10
#else
	#define USER_OUT_10 0x528
#endif

#ifdef USER_OUT_11
#else
	#define USER_OUT_11 0x52C
#endif

#ifdef USER_OUT_12
#else
	#define USER_OUT_12 0x530
#endif

#ifdef USER_OUT_13
#else
	#define USER_OUT_13 0x534
#endif

#ifdef USER_OUT_14
#else
	#define USER_OUT_14 0x538
#endif

#ifdef USER_OUT_15
#else
	#define USER_OUT_15 0x53C
#endif

#ifdef USER_OUT_16
#else
	#define USER_OUT_16 0x540
#endif

#ifdef USER_OUT_17
#else
	#define USER_OUT_17 0x544
#endif

#ifdef USER_OUT_18
#else
	#define USER_OUT_18 0x548
#endif

#ifdef USER_OUT_19
#else
	#define USER_OUT_19 0x54C
#endif

#ifdef USER_OUT_20
#else
	#define USER_OUT_20 0x550
#endif

#ifdef USER_OUT_21
#else
	#define USER_OUT_21 0x554
#endif

#ifdef USER_OUT_22
#else
	#define USER_OUT_22 0x558
#endif

#ifdef USER_OUT_23
#else
	#define USER_OUT_23 0x55C
#endif

#ifdef USER_OUT_24
#else
	#define USER_OUT_24 0x560
#endif

#ifdef USER_OUT_25
#else
	#define USER_OUT_25 0x564
#endif

#ifdef USER_OUT_26
#else
	#define USER_OUT_26 0x568
#endif

#ifdef USER_OUT_27
#else
	#define USER_OUT_27 0x56C
#endif

#ifdef USER_OUT_28
#else
	#define USER_OUT_28 0x570
#endif

#ifdef USER_OUT_29
#else
	#define USER_OUT_29 0x574
#endif

#ifdef USER_OUT_30
#else
	#define USER_OUT_30 0x578
#endif

#ifdef USER_OUT_31
#else
	#define USER_OUT_31 0x57C
#endif

#ifdef JM_CONFIG
#else
	#define JM_CONFIG 0xF00
#endif

#ifdef SHADER_CONFIG
#else
	#define SHADER_CONFIG 0xF04
#endif

#ifdef TILER_CONFIG
#else
	#define TILER_CONFIG 0xF08
#endif

#ifdef L2_MMU_CONFIG
#else
	#define L2_MMU_CONFIG 0xF0C
#endif

#ifdef GPU_DEBUG_LO
#else
	#define GPU_DEBUG_LO 0xFE8
#endif

#ifdef GPU_DEBUG_HI
#else
	#define GPU_DEBUG_HI 0xFEC
#endif

#ifdef GPU_CHICKEN_BITS_0
#else
	#define GPU_CHICKEN_BITS_0 0xFF0
#endif

#ifdef GPU_CHICKEN_BITS_1
#else
	#define GPU_CHICKEN_BITS_1 0xFF4
#endif

#ifdef GPU_CHICKEN_BITS_2
#else
	#define GPU_CHICKEN_BITS_2 0xFF8
#endif

#ifdef GPU_CHICKEN_BITS_3
#else
	#define GPU_CHICKEN_BITS_3 0xFFC
#endif

#ifdef JOB_IRQ_RAWSTAT
#else
	#define JOB_IRQ_RAWSTAT 0x1000
#endif

#ifdef JOB_IRQ_CLEAR
#else
	#define JOB_IRQ_CLEAR 0x1004
#endif

#ifdef JOB_IRQ_MASK
#else
	#define JOB_IRQ_MASK 0x1008
#endif

#ifdef JOB_IRQ_STATUS
#else
	#define JOB_IRQ_STATUS 0x100C
#endif

#ifdef JOB_IRQ_JS_STATE
#else
	#define JOB_IRQ_JS_STATE 0x1010
#endif

#ifdef JOB_IRQ_THROTTLE
#else
	#define JOB_IRQ_THROTTLE 0x1014
#endif

#ifdef JS0_HEAD_LO
#else
	#define JS0_HEAD_LO 0x1800
#endif

#ifdef JS0_HEAD_HI
#else
	#define JS0_HEAD_HI 0x1804
#endif

#ifdef JS0_TAIL_LO
#else
	#define JS0_TAIL_LO 0x1808
#endif

#ifdef JS0_TAIL_HI
#else
	#define JS0_TAIL_HI 0x180C
#endif

#ifdef JS0_AFFINITY_LO
#else
	#define JS0_AFFINITY_LO 0x1810
#endif

#ifdef JS0_AFFINITY_HI
#else
	#define JS0_AFFINITY_HI 0x1814
#endif

#ifdef JS0_CONFIG
#else
	#define JS0_CONFIG 0x1818
#endif

#ifdef JS0_COMMAND
#else
	#define JS0_COMMAND 0x1820
#endif

#ifdef JS0_STATUS
#else
	#define JS0_STATUS 0x1824
#endif

#ifdef JS0_HEAD_NEXT_LO
#else
	#define JS0_HEAD_NEXT_LO 0x1840
#endif

#ifdef JS0_HEAD_NEXT_HI
#else
	#define JS0_HEAD_NEXT_HI 0x1844
#endif

#ifdef JS0_AFFINITY_NEXT_LO
#else
	#define JS0_AFFINITY_NEXT_LO 0x1850
#endif

#ifdef JS0_AFFINITY_NEXT_HI
#else
	#define JS0_AFFINITY_NEXT_HI 0x1854
#endif

#ifdef JS0_CONFIG_NEXT
#else
	#define JS0_CONFIG_NEXT 0x1858
#endif

#ifdef JS0_COMMAND_NEXT
#else
	#define JS0_COMMAND_NEXT 0x1860
#endif

#ifdef JS1_HEAD_LO
#else
	#define JS1_HEAD_LO 0x1880
#endif

#ifdef JS1_HEAD_HI
#else
	#define JS1_HEAD_HI 0x1884
#endif

#ifdef JS1_TAIL_LO
#else
	#define JS1_TAIL_LO 0x1888
#endif

#ifdef JS1_TAIL_HI
#else
	#define JS1_TAIL_HI 0x188C
#endif

#ifdef JS1_AFFINITY_LO
#else
	#define JS1_AFFINITY_LO 0x1890
#endif

#ifdef JS1_AFFINITY_HI
#else
	#define JS1_AFFINITY_HI 0x1894
#endif

#ifdef JS1_CONFIG
#else
	#define JS1_CONFIG 0x1898
#endif

#ifdef JS1_COMMAND
#else
	#define JS1_COMMAND 0x18a0
#endif

#ifdef JS1_STATUS
#else
	#define JS1_STATUS 0x18a4
#endif

#ifdef JS1_HEAD_NEXT_LO
#else
	#define JS1_HEAD_NEXT_LO 0x18c0
#endif

#ifdef JS1_HEAD_NEXT_HI
#else
	#define JS1_HEAD_NEXT_HI 0x18c4
#endif

#ifdef JS1_AFFINITY_NEXT_LO
#else
	#define JS1_AFFINITY_NEXT_LO 0x18d0
#endif

#ifdef JS1_AFFINITY_NEXT_HI
#else
	#define JS1_AFFINITY_NEXT_HI 0x18d4
#endif

#ifdef JS1_CONFIG_NEXT
#else
	#define JS1_CONFIG_NEXT 0x18d8
#endif

#ifdef JS1_COMMAND_NEXT
#else
	#define JS1_COMMAND_NEXT 0x18e0
#endif

#ifdef JS2_HEAD_LO
#else
	#define JS2_HEAD_LO 0x1900
#endif

#ifdef JS2_HEAD_HI
#else
	#define JS2_HEAD_HI 0x1904
#endif

#ifdef JS2_TAIL_LO
#else
	#define JS2_TAIL_LO 0x1908
#endif

#ifdef JS2_TAIL_HI
#else
	#define JS2_TAIL_HI 0x190C
#endif

#ifdef JS2_AFFINITY_LO
#else
	#define JS2_AFFINITY_LO 0x1910
#endif

#ifdef JS2_AFFINITY_HI
#else
	#define JS2_AFFINITY_HI 0x1914
#endif

#ifdef JS2_CONFIG
#else
	#define JS2_CONFIG 0x1918
#endif

#ifdef JS2_COMMAND
#else
	#define JS2_COMMAND 0x1920
#endif

#ifdef JS2_STATUS
#else
	#define JS2_STATUS 0x1924
#endif

#ifdef JS2_HEAD_NEXT_LO
#else
	#define JS2_HEAD_NEXT_LO 0x1940
#endif

#ifdef JS2_HEAD_NEXT_HI
#else
	#define JS2_HEAD_NEXT_HI 0x1944
#endif

#ifdef JS2_AFFINITY_NEXT_LO
#else
	#define JS2_AFFINITY_NEXT_LO 0x1950
#endif

#ifdef JS2_AFFINITY_NEXT_HI
#else
	#define JS2_AFFINITY_NEXT_HI 0x1954
#endif

#ifdef JS2_CONFIG_NEXT
#else
	#define JS2_CONFIG_NEXT 0x1958
#endif

#ifdef JS2_COMMAND_NEXT
#else
	#define JS2_COMMAND_NEXT 0x1960
#endif

#ifdef MMU_IRQ_RAWSTAT
#else
	#define MMU_IRQ_RAWSTAT 0x2000
#endif

#ifdef MMU_IRQ_CLEAR
#else
	#define MMU_IRQ_CLEAR 0x2004
#endif

#ifdef MMU_IRQ_MASK
#else
	#define MMU_IRQ_MASK 0x2008
#endif

#ifdef MMU_IRQ_STATUS
#else
	#define MMU_IRQ_STATUS 0x200C
#endif

#ifdef AS0_TRANSTAB_LO
#else
	#define AS0_TRANSTAB_LO 0x2400
#endif

#ifdef AS0_TRANSTAB_HI
#else
	#define AS0_TRANSTAB_HI 0x2404
#endif

#ifdef AS0_MEMATTR_LO
#else
	#define AS0_MEMATTR_LO 0x2408
#endif

#ifdef AS0_MEMATTR_HI
#else
	#define AS0_MEMATTR_HI 0x240C
#endif

#ifdef AS0_LOCKADDR_LO
#else
	#define AS0_LOCKADDR_LO 0x2410
#endif

#ifdef AS0_LOCKADDR_HI
#else
	#define AS0_LOCKADDR_HI 0x2414
#endif

#ifdef AS0_COMMAND
#else
	#define AS0_COMMAND 0x2418
#endif

#ifdef AS0_FAULTSTATUS
#else
	#define AS0_FAULTSTATUS 0x241C
#endif

#ifdef AS0_FAULTADDRESS_LO
#else
	#define AS0_FAULTADDRESS_LO 0x2420
#endif

#ifdef AS0_FAULTADDRESS_HI
#else
	#define AS0_FAULTADDRESS_HI 0x2424
#endif

#ifdef AS0_STATUS
#else
	#define AS0_STATUS 0x2428
#endif

#ifdef AS1_TRANSTAB_LO
#else
	#define AS1_TRANSTAB_LO 0x2440
#endif

#ifdef AS1_TRANSTAB_HI
#else
	#define AS1_TRANSTAB_HI 0x2444
#endif

#ifdef AS1_MEMATTR_LO
#else
	#define AS1_MEMATTR_LO 0x2448
#endif

#ifdef AS1_MEMATTR_HI
#else
	#define AS1_MEMATTR_HI 0x244C
#endif

#ifdef AS1_LOCKADDR_LO
#else
	#define AS1_LOCKADDR_LO 0x2450
#endif

#ifdef AS1_LOCKADDR_HI
#else
	#define AS1_LOCKADDR_HI 0x2454
#endif

#ifdef AS1_COMMAND
#else
	#define AS1_COMMAND 0x2458
#endif

#ifdef AS1_FAULTSTATUS
#else
	#define AS1_FAULTSTATUS 0x245C
#endif

#ifdef AS1_FAULTADDRESS_LO
#else
	#define AS1_FAULTADDRESS_LO 0x2460
#endif

#ifdef AS1_FAULTADDRESS_HI
#else
	#define AS1_FAULTADDRESS_HI 0x2464
#endif

#ifdef AS1_STATUS
#else
	#define AS1_STATUS 0x2468
#endif

#ifdef AS2_TRANSTAB_LO
#else
	#define AS2_TRANSTAB_LO 0x2480
#endif

#ifdef AS2_TRANSTAB_HI
#else
	#define AS2_TRANSTAB_HI 0x2484
#endif

#ifdef AS2_MEMATTR_LO
#else
	#define AS2_MEMATTR_LO 0x2488
#endif

#ifdef AS2_MEMATTR_HI
#else
	#define AS2_MEMATTR_HI 0x248C
#endif

#ifdef AS2_LOCKADDR_LO
#else
	#define AS2_LOCKADDR_LO 0x2490
#endif

#ifdef AS2_LOCKADDR_HI
#else
	#define AS2_LOCKADDR_HI 0x2494
#endif

#ifdef AS2_COMMAND
#else
	#define AS2_COMMAND 0x2498
#endif

#ifdef AS2_FAULTSTATUS
#else
	#define AS2_FAULTSTATUS 0x249C
#endif

#ifdef AS2_FAULTADDRESS_LO
#else
	#define AS2_FAULTADDRESS_LO 0x24A0
#endif

#ifdef AS2_FAULTADDRESS_HI
#else
	#define AS2_FAULTADDRESS_HI 0x24A4
#endif

#ifdef AS2_STATUS
#else
	#define AS2_STATUS 0x24A8
#endif

#ifdef AS3_TRANSTAB_LO
#else
	#define AS3_TRANSTAB_LO 0x24C0
#endif

#ifdef AS3_TRANSTAB_HI
#else
	#define AS3_TRANSTAB_HI 0x24C4
#endif

#ifdef AS3_MEMATTR_LO
#else
	#define AS3_MEMATTR_LO 0x24C8
#endif

#ifdef AS3_MEMATTR_HI
#else
	#define AS3_MEMATTR_HI 0x24CC
#endif

#ifdef AS3_LOCKADDR_LO
#else
	#define AS3_LOCKADDR_LO 0x24D0
#endif

#ifdef AS3_LOCKADDR_HI
#else
	#define AS3_LOCKADDR_HI 0x24D4
#endif

#ifdef AS3_COMMAND
#else
	#define AS3_COMMAND 0x24D8
#endif

#ifdef AS3_FAULTSTATUS
#else
	#define AS3_FAULTSTATUS 0x24DC
#endif

#ifdef AS3_FAULTADDRESS_LO
#else
	#define AS3_FAULTADDRESS_LO 0x24E0
#endif

#ifdef AS3_FAULTADDRESS_HI
#else
	#define AS3_FAULTADDRESS_HI 0x24E4
#endif

#ifdef AS3_STATUS
#else
	#define AS3_STATUS 0x24E8
#endif

#ifdef ILLEGAL_ADDRESS1
#else
	#define ILLEGAL_ADDRESS1 0x3000
#endif

#ifdef ILLEGAL_ADDRESS2
#else
	#define ILLEGAL_ADDRESS2 0x3ffc
#endif
