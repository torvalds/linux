/*----------------------------------------------------------------------------
*
*The confidential and proprietary information contained in this file may
*only be used by a person authorised under and to the extent permitted
*by a subsisting licensing agreement from ARM Limited.
*
*        (C) COPYRIGHT 2008-2009,2011-2013 ARM Limited.
*             ALL RIGHTS RESERVED
*             
*This entire notice must be reproduced on all copies of this file
*and copies of this file may only be made by a person if such person is
*permitted to do so under the terms of a subsisting license agreement
*from ARM Limited.
*Modified  : $Date: 2013-08-01 18:15:13 +0100 (Thu, 01 Aug 2013) $
*Revision  : $Revision: 66689 $
*Release   : $State: $
*-----------------------------------------------------------------------------
* 
*-----------------------------------------------------------------------------
* Abstract :Implementaion of all functions that are used to access MALI
*-----------------------------------------------------------------------------
* Overview
*----------
* 
**************************************************************/

#include "MaliDefines-t760.h"


static int gpu_errmask = 0;
static int mmu_errmask = 0;
static int job_errmask = 0;
static int* MALI_BASE;

void Mali_Reset(void) 
{
};

void Mali_SetBase(int* base)
{
	MALI_BASE = base;
}

#define printf printk
void Mali_SetInterruptErrMask(int type, int e_mask)
{
  switch (type) {
	  case 1 : gpu_errmask = e_mask; break;
	  case 2 : mmu_errmask = e_mask; break;
	  case 3 : job_errmask = e_mask; break;
	  default : 
		  printf("Programming Error\n"); 	
		  exit(1);
  }
}

void Mali_Identify(void)
{
	int gpuid = Mali_RdReg(0x20,0,0);

	int l2features, l3features, tilerfeatures,memfeatures;
	int mmufeatures, aspresent, jspresent;


	printf("GPU ID=0x%08x ProductID=0x%04x, Version=r%1dp%1d Version_status=%1d\n",
  (gpuid) & 0XFFFFFFFF,
  (gpuid>>16) & 0xFFFF,
  (gpuid>>12) & 0xF,
  (gpuid>>4) & 0xFF,
  (gpuid) & 0xF);

	l2features = Mali_RdReg(0x20,0,0x4);
	printf("L2. LineSize=%2d Associativity=%2d CacheSize=%dkB BusWidth=%3d\n",
		   1 << ((l2features)       & 0xFF),
		   1 << ((l2features >> 8)  & 0xFF),
		  (1 << ((l2features >> 16) & 0xFF)) >> 10,
		   1 << ((l2features >> 24) & 0xFF)
		);

	l3features = Mali_RdReg(0x20,0,0x8);
	if (l3features != 0) {
		printf("L3. LineSize=%3d Associativity=%3d CacheSize=%dkB BusWidth=%3d\n",
			   1 << ((l3features)       & 0xFF),
			   1 << ((l3features >> 8)  & 0xFF),
        (1 << ((l3features >> 16) & 0xFF)) >> 10,
			   1 << ((l3features >> 24) & 0xFF));
	};

	tilerfeatures =  Mali_RdReg(0x20,0,0xC);
	printf("Tiler. BinSize=%4d MaxActiveLevels=%4d\n",
		   1 << ((tilerfeatures)        & 0x3F),
		   1 << ((tilerfeatures >> 8)   & 0xF));
		   
	memfeatures =  Mali_RdReg(0x20,0,0x10);
	printf("MemFeatures. CoherentCoreGroup=%d CoherentSuperGroup=%d L2 Slices=%d\n",
		   memfeatures & 0x1,
		   (memfeatures > 1) & 0x1,
       (((memfeatures>>8) & 0xf)+1));

	mmufeatures =  Mali_RdReg(0x20,0,0x14);
	printf("MMUFeatures. VA_BITS=%d PA_BITS=%d\n",
		   mmufeatures & 0xFF,
		   (mmufeatures >> 8) & 0xFF);

	aspresent =  Mali_RdReg(0x20,0,0x18);
	jspresent =  Mali_RdReg(0x20,0,0x1C);
	printf("ASPresent=%08x JSPresent=%08x\n",
		   aspresent,
		   jspresent);

}


void *Mali_LdMem(void *srcptr,int size,int ttb_base) {
  /* Setup MMU to point to data structures for this image */
  Mali_WrReg(0x20,0,0x240c,0);
  Mali_WrReg(0x20,0,0x2408,0x88888888);
  Mali_WrReg(0x20,0,0x2404,0);
  Mali_WrReg(0x20,0,0x2400,ttb_base|0x3);
  printf (" - - - - - - - - > ttb_base = %x\n", ttb_base);
  Mali_WrReg(0x20,0,0x2418,1); // Broadcast updates
  /* Clear interrupts and enable them */
  Mali_WrReg(0x20,0,0x2004,0xFFFFFFFF);
  Mali_WrReg(0x20,0,0x2008,0xFFFFFFFF);

  return srcptr;
};

static void Mali_GPUError(int stat,int mask,int faultstat,int faultvalo,int faultvahi)
{
	printf("Unexpected GPU Fault:\n");
	printf("Int Status=%08x, Error Mask=%08x\n",stat, mask);
	printf("FaultStats=%08x,VA=%08x%08x\n",faultstat, faultvahi,faultvalo);
	printf ("TEST FAILED\n\n");
	exit(1);
}
static void Mali_MMUError(int stat,int mask,int faultstat,int faultvalo,int faultvahi)
{
	printf("Unexpected MMU Fault:\n");
	printf("Int Status=%08x, Error Mask=%08x\n",stat, mask);
	printf("FaultStats=%08x, VA=%08x%08x\n",faultstat, faultvahi,faultvalo);
	printf ("TEST FAILED\n\n");
	exit(1);
}
static void Mali_JobError(int stat,int mask)
{
	printf("Unexpected Job Interrupt:\n");
	printf("Int Status=%08x, Error Mask=%08x\n",stat, mask);
	printf ("TEST FAILED\n\n");
	exit(1);
}

/* This function is backwards compatible with Mali400 API
 * For T760 it checks for job interrupts if mask and value given
 * We wait for any interrupt if mask and value is all set
 */
int Mali_InterruptCheck(int i_mask, int i_value)
{
	if ((i_mask == i_value) && i_mask == 0xFFFFFFFF) {
		return Mali_AnyInterruptCheck(0,i_mask,i_value);
	} else {
		return Mali_AnyInterruptCheck(3,i_mask,i_value);
	}
}

/* This function checks if we have the required interrupts.
 * It polls the Mali interrupt registers for this.
 * If we do not have the required interrupts, then it remembers which ones we 
 * have seen, and then masks them.
 */
int Mali_AnyInterruptCheck(int type, int i_mask, int i_value)
{
  int done;

  /* Read interrupt status */
  int gpu_stat = Mali_RdReg(0x20,0,0x2c);
  int job_stat = Mali_RdReg(0x20,0,0x100c);
  int mmu_stat = Mali_RdReg(0x20,0,0x200c);

  int gpu_rawstat = Mali_RdReg(0x20,0,0x20);
  int job_rawstat = Mali_RdReg(0x20,0,0x1000);
  int mmu_rawstat = Mali_RdReg(0x20,0,0x2000);

  int v;

  /* Compare against failure conditions */
  if ((gpu_stat & gpu_errmask) != 0) {
	  Mali_GPUError(gpu_stat,gpu_errmask,
					Mali_RdReg(0x20,0,0x3C),
					Mali_RdReg(0x20,0,0x40),
					Mali_RdReg(0x20,0,0x44));
  }

  if ((mmu_stat & mmu_errmask) != 0) {
	  Mali_MMUError(mmu_stat,mmu_errmask,
					Mali_RdReg(0x20,0,0x201c),
					Mali_RdReg(0x20,0,0x2020),
					Mali_RdReg(0x20,0,0x2024));
  }

  if ((job_stat & job_errmask) != 0) {
	  Mali_JobError(job_stat,job_errmask);
  }

  /* If no failure then test against masks as required */
  done = 0;
  switch (type) {
	  // This is wait for any interrupt, so check against real interrupt outputs
	  case 0 : done = ((gpu_stat | job_stat | mmu_stat) != 0); break;

	  // These are masked checks, so check if required interrupts are triggered.
      // We do against rawstat, since if not all required interrupts have occured
      // then we need to mask the first ones to happen, whilst waiting for the others.
	  case 1 : done = ((gpu_rawstat & i_mask) == i_value); break;
	  case 2 : done = ((job_rawstat & i_mask) == i_value); break;
	  case 3 : done = ((mmu_rawstat & i_mask) == i_value); break;
	  default : 
		  printf("Programming Error\n"); 	
		  exit(1);
  }





  /* If we do not satisfy final conditions then
   * mask all seen interrupts
   */
  if (!done) {
	  // If not done then mark the interrupts we have seen
	  Mali_WrReg(0x20,0,0x28,  Mali_RdReg(0x20,0,0x28)   &  ~gpu_stat);
	  Mali_WrReg(0x20,0,0x1008,Mali_RdReg(0x20,0,0x1008) &  ~job_stat);
	  Mali_WrReg(0x20,0,0x2008,Mali_RdReg(0x20,0,0x2008) &  ~mmu_stat);
	  printf ("Interrupt - conditions not met - WFI\n");
  }

  if (done) {
	  // When done - re-enable all the interrupts we were looking for
	  // This is only an issue for specific interrupt testing.
	  switch (type) {
		  case 0 :    break;
		  case 1 :	  Mali_WrReg(0x20,0,0x28,  Mali_RdReg(0x20,0,0x28)   | i_value); break;
		  case 2 :	  Mali_WrReg(0x20,0,0x1008,Mali_RdReg(0x20,0,0x1008) | i_value); break;
		  case 3 :    Mali_WrReg(0x20,0,0x2008,Mali_RdReg(0x20,0,0x2008) | i_value); break;
		  default :
			  printf("Programming error\n");
			  exit(1);
	  }

	  printf ("Interrupt - conditions met - continue test\n");
  }

  printf("gpu_raw_stat = %x\n",gpu_rawstat);
  printf("job_raw_stat = %x\n",job_rawstat);
  printf("mmu_raw_stat = %x\n",mmu_rawstat);
  v =  Mali_RdReg(0x20,0,0x1824);
  printf("JS0Status = %x\n",v);
  v =  Mali_RdReg(0x20,0,0x18a4);
  printf("JS1Status = %x\n",v);
  v =  Mali_RdReg(0x20,0,0x1924);
  printf("JS2Status = %x\n",v);

  return done;
};


void Mali_MaskAllInterrupts (void) {
	Mali_WrReg(0x20,0,0x24,0xFFFFFFFF);
	Mali_WrReg(0x20,0,0x1004,0xFFFFFFFF);
	Mali_WrReg(0x20,0,0x2004,0xFFFFFFFF);
};


int Mali_RdReg(int unit,int core, int regnum)
{
  int *reg_ptr = (int *)MALI_BASE + (regnum/4);
  return *reg_ptr;
};


void Mali_WrReg(int unit,int core,int regnum,int value)
{
  
  int *reg_ptr = (int *)MALI_BASE + (regnum/4);
  *reg_ptr = value;
};

unsigned int Mali_PollReg (unsigned int unit, unsigned int core, unsigned int regnum, unsigned int bit_location, unsigned int val, char * name_str)
{
  int regnum_value =  Mali_RdReg(0x20,0,regnum);


  printf ("Polling (for %d) of register %s 0x%x bit %d started\n", val, name_str, regnum, bit_location);
  
  if (val)
  {
    while ((regnum_value & (1<<bit_location)) != (1<<bit_location))
    {
      regnum_value = Mali_RdReg(0x20,0,regnum);
    }  
  }
  else
  {
    while ((regnum_value & (1<<bit_location)) == (1<<bit_location))
    {
      regnum_value = Mali_RdReg(0x20,0,regnum);
    }  
  }
  printf ("Polling (for %d) of register %s 0x%x bit %d ended   current value=0x%x \n", val, name_str, regnum, bit_location, regnum_value);
  
  return regnum_value;
};


void Mali_clear_irqs_and_set_all_masks (void) {
  Mali_clear_and_set_masks_for_gpu_irq ();
  Mali_clear_and_set_masks_for_mmu_irq ();
  Mali_clear_and_set_masks_for_job_irq ();
}

void Mali_clear_and_set_masks_for_gpu_irq (void) {
  printf ("Clear the GPU IRQ flags\n");
  Mali_WrReg(0x20,0x0, GPU_IRQ_CLEAR       , 0xFFFFFFFF);
  printf ("Set the GPU IRQ Mask to all 1s\n");
  Mali_WrReg(0x20,0x0, GPU_IRQ_MASK        , 0xFFFFFFFF);
}
void Mali_clear_and_set_masks_for_mmu_irq (void) {
  printf ("Clear the MMU IRQ flags\n");
  Mali_WrReg(0x20,0x0, MMU_IRQ_CLEAR       , 0xFFFFFFFF);
  printf ("Set the MMU IRQ Mask to all 1s\n");
  Mali_WrReg(0x20,0x0, MMU_IRQ_MASK        , 0xFFFFFFFF);
}
void Mali_clear_and_set_masks_for_job_irq (void) {
  printf ("Clear the JOB IRQ flags\n");
  Mali_WrReg(0x20,0x0, JOB_IRQ_CLEAR       , 0xFFFFFFFF);
  printf ("Set the JOB IRQ Mask to all 1s\n");
  Mali_WrReg(0x20,0x0, JOB_IRQ_MASK        , 0xFFFFFFFF);
}

void Mali_write_ASn_register (int ASn_reg, int n, int value_to_write) {
  int ASn_reg_final = MEM_MANAGEMENT_BASE | ASn_BASE | ASn_reg | (n * 64);
#ifdef DEBUG_ON
  printf ("Write: ASn_reg_final=0x%x n=%d value=0x%x\n", ASn_reg_final, n, value_to_write);
#endif
  Mali_WrReg (0x20, 0, ASn_reg_final, value_to_write);
}

void Mali_write_JSn_register (int JSn_reg, int n, int value_to_write) {
  int JSn_reg_final = JOB_CONTROL_BASE | JSn_BASE | JSn_reg | (n * 128);
#ifdef DEBUG_ON
  printf ("Write: JSn_reg_final=0x%x n=%d value=0x%x\n", JSn_reg_final, n, value_to_write);
#endif
  Mali_WrReg (0x20, 0, JSn_reg_final, value_to_write);
}

int Mali_read_JSn_register (int JSn_reg, int n) {
  int JSn_reg_final = JOB_CONTROL_BASE | JSn_BASE | JSn_reg | (n * 128);
  int value_read = Mali_RdReg (0x20, 0, JSn_reg_final);
#ifdef DEBUG_ON
  printf ("Read: JSn_reg_final=0x%x n=%d value=0x%x\n", JSn_reg_final, n, value_read);
#endif
  return value_read;
}

#undef printf

