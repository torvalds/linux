#include <linux/power/aw_pm.h>
#include "pm_types.h"
#include "./pm.h"



void save_mmu_state(struct mmu_state *saved_mmu_state)
{
	/* CR0 */
	asm volatile ("mrc p15, 2, %0, c0, c0, 0" : "=r"(saved_mmu_state->cssr));
	/* CR1 */
	asm volatile ("mrc p15, 0, %0, c1, c0, 2" : "=r"(saved_mmu_state->cacr));

	/* CR3 */
	asm volatile ("mrc p15, 0, %0, c3, c0, 0" : "=r"(saved_mmu_state->dacr));
	
	/*cr10*/
	asm volatile ("mrc p15, 0, %0, c10, c2, 0" : "=r"(saved_mmu_state->prrr)); /* Primary Region Remap Register */
	asm volatile ("mrc p15, 0, %0, c10, c2, 1" : "=r"(saved_mmu_state->nrrr)); /* Normal Memory Remap Register */
	
	//save ttb
	/* CR2 */
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c2, c0, 0" : "=r"(saved_mmu_state->ttb_0r));
	asm volatile ("mrc p15, 0, %0, c2, c0, 1" : "=r"(saved_mmu_state->ttb_1r));
	asm volatile ("mrc p15, 0, %0, c2, c0, 2" : "=r"(saved_mmu_state->ttbcr));
	/* CR1 */
	//busy_waiting();
	asm volatile ("mrc p15, 0, %0, c1, c0, 0" : "=r"(saved_mmu_state->cr));  //will effect visible addr space

	return;
}

void restore_mmu_state(struct mmu_state *saved_mmu_state)
{

	//busy_waiting();
	/* CR0 */
	asm volatile ("mcr p15, 2, %0, c0, c0, 0" : : "r"(saved_mmu_state->cssr));
	/* CR1 */
	asm volatile ("mcr p15, 0, %0, c1, c0, 2" : : "r"(saved_mmu_state->cacr));
	/* CR3 */
	asm volatile ("mcr p15, 0, %0, c3, c0, 0" : : "r"(saved_mmu_state->dacr));
#if 0
	/*cr10*/
	asm volatile ("mcr p15, 0, %0, c10, c2, 0" : : "r"(saved_mmu_state->prrr)); /* Primary Region Remap Register */
	asm volatile ("mcr p15, 0, %0, c10, c2, 1" : : "r"(saved_mmu_state->nrrr)); /* Normal Memory Remap Register */
#endif

	/* CR2 */
	/*when translate 0x0000,0000, use ttb0, while ttb0 shoudbe the same with ttb1*/
	asm volatile ("mcr p15, 0, %0, c2, c0, 0" : : "r"(saved_mmu_state->ttb_1r));
	asm volatile ("mcr p15, 0, %0, c2, c0, 1" : : "r"(saved_mmu_state->ttb_1r));
	asm volatile ("mcr p15, 0, %0, c2, c0, 2" : : "r"(saved_mmu_state->ttbcr));
	asm("b __turn_mmu_on");
	asm(".align 5");
	asm(".type __turn_mmu_on, %function");
	asm("__turn_mmu_on:");	
	asm("mov r0, r0");
	
	/* CR1 */
	/*cr: will effect visible addr space*/		
	asm volatile ("mcr p15, 0, %0, c1, c0, 0" : : "r"(saved_mmu_state->cr)); 
#if 0
	asm volatile ("mrc p15, 0, r0, c1, c0, 0" : : );
	asm volatile ("ORR	    r0, r0, #0x1 << 0");
	asm volatile ("mcr p15, 0, r0, c1, c0, 0" : : );
#endif
	/*read id reg*/
	asm volatile ("mrc p15, 0, r3, c0, c0, 0" : : ); 	
	asm("mov r3, r3"); 
	asm("mov r3, r3"); 
	asm("isb");

	return;
}

#if 0
void disable_mmu(void)
{
	__u32 c1format = 0;

	asm volatile("mrc p15,0,%0,c1,c0,0" :"=r"(c1format) :);
	c1format &= ~ 0x1007;
	c1format |= 0;
	asm volatile("mcr p15,0,%0,c1,c0,0" : :"r"(c1format));
	
	/*read id reg*/
	asm volatile ("mrc p15, 0, r3, c0, c0, 0" : : ); 	
	asm("mov r3, r3"); 
	asm("mov r3, r3"); 
	asm("isb");
	
	return;
}
#endif

//
//void jump_to_resume(void* pointer)
//{
//	asm("mov lr, r0");
//	asm("mov pc, lr");
//	return;
//}
