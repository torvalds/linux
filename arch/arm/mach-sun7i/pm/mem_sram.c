#include "pm_types.h"
#include "pm_i.h"

/*
*********************************************************************************************************
*                                       MEM SRAM SAVE
*
* Description: mem sram save.
*
* Arguments  : none.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 mem_sram_save(struct sram_state *psram_state)
{
	int i=0;

	/*save all the sram reg*/
	for(i=0; i<(SRAM_REG_LENGTH); i++){
		psram_state->sram_reg_back[i] = *(volatile __u32 *)(SW_VA_SRAM_IO_BASE + i*0x04); 
	}
	return 0;
}

/*
*********************************************************************************************************
*                                       MEM sram restore
*
* Description: mem sram restore.
*
* Arguments  : none.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 mem_sram_restore(struct sram_state *psram_state)
{
	int i=0;
	
	/*restore all the sram reg*/
	for(i=0; i<(SRAM_REG_LENGTH); i++){
		 *(volatile __u32 *)(SW_VA_SRAM_IO_BASE + i*0x04) = psram_state->sram_reg_back[i];
	}

	return 0;
}
