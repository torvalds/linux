#include <linux/delay.h>
//#define __uxx_sxx_name
#include "pm_i.h"


static __mem_twic_reg_t*   TWI_REG_BASE[3] = {
    (__mem_twic_reg_t*)SW_VA_TWI0_IO_BASE,
    (__mem_twic_reg_t*)SW_VA_TWI1_IO_BASE,
    (__mem_twic_reg_t*)SW_VA_TWI2_IO_BASE
};


/*
*********************************************************************************************************
*                                   mem_twi_save
*
*Description: save twi status before enter super standby.
*
*Arguments  :
*
*Return     :
*
*********************************************************************************************************
*/
__s32 mem_twi_save(struct twi_state *ptwi_state)
{
	__mem_twic_reg_t *twi_reg;
	ptwi_state->twi_reg = twi_reg = TWI_REG_BASE[AXP_IICBUS];

	ptwi_state->twi_reg_backup[0] = twi_reg->reg_saddr;
	ptwi_state->twi_reg_backup[1] = twi_reg->reg_xsaddr;
	ptwi_state->twi_reg_backup[2] = twi_reg->reg_data;
	ptwi_state->twi_reg_backup[3] = twi_reg->reg_ctl;
	ptwi_state->twi_reg_backup[4] = twi_reg->reg_clkr;
	ptwi_state->twi_reg_backup[5] = twi_reg->reg_efr;
	ptwi_state->twi_reg_backup[6] = twi_reg->reg_lctl;
	
	return 0;
}

/*
*********************************************************************************************************
*                                   mem_twi_restore
*
*Description: restore twi status after resume.
*
*Arguments  :
*
*Return     :
*
*********************************************************************************************************
*/
__s32 mem_twi_restore(struct twi_state *ptwi_state)
{
	__mem_twic_reg_t *twi_reg = ptwi_state->twi_reg;
#if 0
/*double check.*/
	
	/* softreset twi module  */
	twi_reg->reg_reset |= 0x1;
	/* delay */
	//mdelay(100); not recommended.
	//while(**); to check bit flag.
	
	/* restore */
	twi_reg->reg_saddr		=	ptwi_state->twi_reg_backup[0];
	twi_reg->reg_xsaddr		=	ptwi_state->twi_reg_backup[1];
	twi_reg->reg_data		=	ptwi_state->twi_reg_backup[2];
	twi_reg->reg_ctl		=	ptwi_state->twi_reg_backup[3];
	twi_reg->reg_clkr		=	ptwi_state->twi_reg_backup[4];
	twi_reg->reg_efr		=	ptwi_state->twi_reg_backup[5];
	twi_reg->reg_lctl		=	ptwi_state->twi_reg_backup[6];
#endif
	twi_reg->reg_ctl		=	ptwi_state->twi_reg_backup[3];
	twi_reg->reg_clkr		=	ptwi_state->twi_reg_backup[4];
	
	return 0;
}
