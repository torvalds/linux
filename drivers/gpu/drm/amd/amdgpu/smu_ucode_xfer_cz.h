// CZ Ucode Loading Definitions
#ifndef SMU_UCODE_XFER_CZ_H
#define SMU_UCODE_XFER_CZ_H

#define NUM_JOBLIST_ENTRIES      32

#define TASK_TYPE_NO_ACTION      0
#define TASK_TYPE_UCODE_LOAD     1
#define TASK_TYPE_UCODE_SAVE     2
#define TASK_TYPE_REG_LOAD       3
#define TASK_TYPE_REG_SAVE       4
#define TASK_TYPE_INITIALIZE     5

#define TASK_ARG_REG_SMCIND      0
#define TASK_ARG_REG_MMIO        1
#define TASK_ARG_REG_FCH         2
#define TASK_ARG_REG_UNB         3

#define TASK_ARG_INIT_MM_PWR_LOG 0
#define TASK_ARG_INIT_CLK_TABLE  1

#define JOB_GFX_SAVE             0
#define JOB_GFX_RESTORE          1
#define JOB_FCH_SAVE             2
#define JOB_FCH_RESTORE          3
#define JOB_UNB_SAVE             4
#define JOB_UNB_RESTORE          5
#define JOB_GMC_SAVE             6
#define JOB_GMC_RESTORE          7
#define JOB_GNB_SAVE             8
#define JOB_GNB_RESTORE          9

#define IGNORE_JOB               0xff
#define END_OF_TASK_LIST     (uint16_t)0xffff

// Size of DRAM regions (in bytes) requested by SMU:
#define SMU_DRAM_REQ_MM_PWR_LOG 48 

#define UCODE_ID_SDMA0           0
#define UCODE_ID_SDMA1           1
#define UCODE_ID_CP_CE           2
#define UCODE_ID_CP_PFP          3
#define UCODE_ID_CP_ME           4
#define UCODE_ID_CP_MEC_JT1      5
#define UCODE_ID_CP_MEC_JT2      6
#define UCODE_ID_GMCON_RENG      7
#define UCODE_ID_RLC_G           8
#define UCODE_ID_RLC_SCRATCH     9
#define UCODE_ID_RLC_SRM_ARAM    10
#define UCODE_ID_RLC_SRM_DRAM    11
#define UCODE_ID_DMCU_ERAM       12
#define UCODE_ID_DMCU_IRAM       13

#define UCODE_ID_SDMA0_MASK           0x00000001       
#define UCODE_ID_SDMA1_MASK           0x00000002        
#define UCODE_ID_CP_CE_MASK           0x00000004      
#define UCODE_ID_CP_PFP_MASK          0x00000008         
#define UCODE_ID_CP_ME_MASK           0x00000010          
#define UCODE_ID_CP_MEC_JT1_MASK      0x00000020             
#define UCODE_ID_CP_MEC_JT2_MASK      0x00000040          
#define UCODE_ID_GMCON_RENG_MASK      0x00000080            
#define UCODE_ID_RLC_G_MASK           0x00000100           
#define UCODE_ID_RLC_SCRATCH_MASK     0x00000200         
#define UCODE_ID_RLC_SRM_ARAM_MASK    0x00000400                
#define UCODE_ID_RLC_SRM_DRAM_MASK    0x00000800                 
#define UCODE_ID_DMCU_ERAM_MASK       0x00001000             
#define UCODE_ID_DMCU_IRAM_MASK       0x00002000              

#define UCODE_ID_SDMA0_SIZE_BYTE           10368        
#define UCODE_ID_SDMA1_SIZE_BYTE           10368          
#define UCODE_ID_CP_CE_SIZE_BYTE           8576        
#define UCODE_ID_CP_PFP_SIZE_BYTE          16768           
#define UCODE_ID_CP_ME_SIZE_BYTE           16768            
#define UCODE_ID_CP_MEC_JT1_SIZE_BYTE      384               
#define UCODE_ID_CP_MEC_JT2_SIZE_BYTE      384            
#define UCODE_ID_GMCON_RENG_SIZE_BYTE      4096              
#define UCODE_ID_RLC_G_SIZE_BYTE           2048             
#define UCODE_ID_RLC_SCRATCH_SIZE_BYTE     132           
#define UCODE_ID_RLC_SRM_ARAM_SIZE_BYTE    8192                  
#define UCODE_ID_RLC_SRM_DRAM_SIZE_BYTE    4096                   
#define UCODE_ID_DMCU_ERAM_SIZE_BYTE       24576               
#define UCODE_ID_DMCU_IRAM_SIZE_BYTE       1024                 

#define NUM_UCODES               14

typedef struct {
	uint32_t high;
	uint32_t low;
} data_64_t;

struct SMU_Task {
    uint8_t type;
    uint8_t arg;
    uint16_t next;
    data_64_t addr;
    uint32_t size_bytes;
};
typedef struct SMU_Task SMU_Task;

struct TOC {
    uint8_t JobList[NUM_JOBLIST_ENTRIES];
    SMU_Task tasks[1];
};

// META DATA COMMAND Definitions
#define METADATA_CMD_MODE0         0x00000103 
#define METADATA_CMD_MODE1         0x00000113 
#define METADATA_CMD_MODE2         0x00000123 
#define METADATA_CMD_MODE3         0x00000133
#define METADATA_CMD_DELAY         0x00000203
#define METADATA_CMD_CHNG_REGSPACE 0x00000303
#define METADATA_PERFORM_ON_SAVE   0x00001000
#define METADATA_PERFORM_ON_LOAD   0x00002000
#define METADATA_CMD_ARG_MASK      0xFFFF0000
#define METADATA_CMD_ARG_SHIFT     16

// Simple register addr/data fields
struct SMU_MetaData_Mode0 {
    uint32_t register_address;
    uint32_t register_data;
};
typedef struct SMU_MetaData_Mode0 SMU_MetaData_Mode0;

// Register addr/data with mask
struct SMU_MetaData_Mode1 {
    uint32_t register_address;
    uint32_t register_mask;
    uint32_t register_data;
};
typedef struct SMU_MetaData_Mode1 SMU_MetaData_Mode1;

struct SMU_MetaData_Mode2 {
    uint32_t register_address;
    uint32_t register_mask;
    uint32_t target_value;
};
typedef struct SMU_MetaData_Mode2 SMU_MetaData_Mode2;

// Always write data (even on a save operation)
struct SMU_MetaData_Mode3 {
    uint32_t register_address;
    uint32_t register_mask;
    uint32_t register_data;
};
typedef struct SMU_MetaData_Mode3 SMU_MetaData_Mode3;

#endif
