#include "pm_types.h"
#include "pm_i.h"

/*
*********************************************************************************************************
*                                       MEM gpio INITIALISE
*
* Description: mem gpio initialise.
*
* Arguments  : none.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 mem_gpio_save(struct gpio_state *pgpio_state)
{
	int i=0;
	
	/*save all the gpio reg*/
	for(i=0; i<(GPIO_REG_LENGTH); i++){
		pgpio_state->gpio_reg_back[i] = *(volatile __u32 *)(IO_ADDRESS(SW_PA_PORTC_IO_BASE) + i*0x04); 
	}
	return 0;
}

/*
*********************************************************************************************************
*                                       MEM gpio INITIALISE
*
* Description: mem gpio initialise.
*
* Arguments  : none.
*
* Returns    : 0/-1;
*********************************************************************************************************
*/
__s32 mem_gpio_restore(struct gpio_state *pgpio_state)
{
	int i=0;
	
	/*restore all the gpio reg*/
	for(i=0; i<(GPIO_REG_LENGTH); i++){
		 *(volatile __u32 *)(IO_ADDRESS(SW_PA_PORTC_IO_BASE) + i*0x04) = pgpio_state->gpio_reg_back[i];
	}
	return 0;
}
