/*----------------------------------------------------------------------------
*
* The confidential and proprietary information contained in this file may
* only be used by a person authorised under and to the extent permitted
* by a subsisting licensing agreement from ARM Limited.
*
*        (C) COPYRIGHT 2008-2009,2011-2013 ARM Limited.
*             ALL RIGHTS RESERVED
*             
* This entire notice must be reproduced on all copies of this file
* and copies of this file may only be made by a person if such person is
* permitted to do so under the terms of a subsisting license agreement
* from ARM Limited.
*
* Modified  : $Date: 2013-08-01 18:15:13 +0100 (Thu, 01 Aug 2013) $
* Revision  : $Revision: 66689 $
* Release   : $State: $
*-----------------------------------------------------------------------------*/

/* 

BRIEF DESCRIPTION, PURPOSE AND STRATEGY
=======================================

This test checks that the interrupts are correctly connected by directly writing to their RAW status registers.


DEBUG HINTS IN CASE OF FAILURE
===========================
a) Check the APB bus connections as this is the bus that is used to communicate with the Job manager.
b) Check that the GPU's reset signal is not asserted.
c) Check that the GPU input clock is toggling as expected.
d) Check that the DFT signals are disabled.
e) Check that MBIST is disabled.
f) Check the GPU interrupt signals are connected correctly i.e. IRQGPU, IRQMMU and IRQJOB.

*/

#include "MaliFns.h"
#include "MaliDefines-t760.h"


int RunMaliTest_sim2_t760_part0 (void);
int RunMaliTest_sim2_t760_part1 (void);
int RunMaliTest_sim2_t760_part2 (void);
int RunMaliTest_sim2_t760_part3 (void);

static int mali_step;

int RunMaliTest_sim2_t760 (int *base) {
  	Mali_SetBase(base);
  	
	RunMaliTest_sim2_t760_part0();
    RunMaliTest_sim2_t760_part1();
    RunMaliTest_sim2_t760_part2();
    RunMaliTest_sim2_t760_part3();

    return 0;
};

#define printf printk
int RunMaliTest_sim2_t760_part0 (void) {
  int res = 0;

  int gpuid, num_cores, l2_size, axi_width, i, as_present, js_present, core_bitmap;

  printf("RUNNING TEST: sim2\n");
  printf("  Purpose: Check interrupt line connectivity\n");
  printf("  Will assert all three interrupt signals sequentially.\n");

  // Get current configuration to allow testing all registers
  gpuid         = Mali_RdReg(0x20, 0, 0x0000);
  core_bitmap   = Mali_RdReg(0x20, 0, 0x0100);
  l2_size       = ((Mali_RdReg(0x20, 0, 0x0004) >> 16) & 0xFF);
  axi_width     = (1 <<((Mali_RdReg(0x20, 0, 0x0004) >> 24) & 0xFF));
  as_present    = Mali_RdReg(0x20, 0, 0x0018);
  js_present    = Mali_RdReg(0x20, 0, 0x001c);

  i = core_bitmap;
  num_cores = 0;
  while(i) {
    num_cores++;
    i >>= 1;
  }
  printf("Selected configuration:\n");
  printf("  Bus width: %d\n", axi_width);
  printf("  Number of shader cores: %d\n", num_cores);
  printf("  L2 cache size: %d kB\n", ((1<<l2_size)/1024));

  // Clear all interrupt registers
  Mali_WrReg(0x20,0x0,0x0024,0xFFFFFFFF); // GPU_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x1004,0xFFFFFFFF); // JOB_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x2004,0xFFFFFFFF); // MMU_IRQ_CLEAR

  // Enable GPU interrupt source, disable all other interrupt sources
  Mali_WrReg(0x20,0x0,0x0028,0x00000001); // GPU_IRQ_MASK
  Mali_WrReg(0x20,0x0,0x1008,0x00000000); // JOB_IRQ_MASK
  Mali_WrReg(0x20,0x0,0x2008,0x00000000); // MMU_IRQ_MASK

  // Trigger GPU interrupt
  Mali_WrReg(0x20,0x0,0x0020,0x00000001); // GPU_IRQ_RAWSTAT

  mali_step++;
  if (res == 0) {
   return 255;
  }
  return res;
}

int RunMaliTest_sim2_t760_part1 (void) {
  int res = 0;
  int gpu_irq_status, job_irq_status, mmu_irq_status;
  gpu_irq_status = Mali_RdReg(0x20,0x0,0x002c);
  job_irq_status = Mali_RdReg(0x20,0x0,0x100c);
  mmu_irq_status = Mali_RdReg(0x20,0x0,0x200c);
  if( (gpu_irq_status == 1) && (job_irq_status == 0) && (mmu_irq_status == 0) ) {
    printf("GPU Interrupt asserted\n");
    Mali_WrReg(0x20,0x0,0x0024,0xFFFFFFFF); // GPU_IRQ_CLEAR
    Mali_WrReg(0x20,0x0,0x0028,0x00000000); // GPU_IRQ_MASK
    Mali_WrReg(0x20,0x0,0x1008,0x00000001); // JOB_IRQ_MASK
    Mali_WrReg(0x20,0x0,0x1000,0x00000001); // JOB_IRQ_RAWSTAT
    Mali_WrReg(0x20,0x0,0x0020,0x00000000); // GPU_IRQ_RAWSTAT
    mali_step++;
    return 255;
  }
  printf("FAILURE: Expected GPU interrupt. Got the following interrupts: GPU: %x  JOB: %x  MMU: %x\n",
         gpu_irq_status, job_irq_status, mmu_irq_status);
  Mali_WrReg(0x20,0x0,0x0024,0xFFFFFFFF); // GPU_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x1004,0xFFFFFFFF); // JOB_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x2004,0xFFFFFFFF); // MMU_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x0028,0x00000000); // GPU_IRQ_MASK
  Mali_WrReg(0x20,0x0,0x1008,0x00000000); // JOB_IRQ_MASK
  Mali_WrReg(0x20,0x0,0x2008,0x00000000); // MMU_IRQ_MASK
  return 1;
}

int RunMaliTest_sim2_t760_part2 (void) {
  int res = 0;
  int gpu_irq_status, job_irq_status, mmu_irq_status;
  gpu_irq_status = Mali_RdReg(0x20,0x0,0x002c);
  job_irq_status = Mali_RdReg(0x20,0x0,0x100c);
  mmu_irq_status = Mali_RdReg(0x20,0x0,0x200c);
  if( (gpu_irq_status == 0) && (job_irq_status == 1) && (mmu_irq_status == 0) ) {
    printf("JOB Interrupt asserted\n");
    Mali_WrReg(0x20,0x0,0x1004,0xFFFFFFFF); // JOB_IRQ_CLEAR
    Mali_WrReg(0x20,0x0,0x1008,0x00000000); // JOB_IRQ_MASK
    Mali_WrReg(0x20,0x0,0x2008,0x00000001); // MMU_IRQ_MASK
    Mali_WrReg(0x20,0x0,0x2000,0x00000001); // MMU_IRQ_RAWSTAT
    Mali_WrReg(0x20,0x0,0x1000,0x00000000); // JOB_IRQ_RAWSTAT
    mali_step++;
    return 255;
  }
  printf("FAILURE: Expected JOB interrupt. Got the following interrupts: GPU: %x  JOB: %x  MMU: %x\n",
         gpu_irq_status, job_irq_status, mmu_irq_status);
  Mali_WrReg(0x20,0x0,0x0024,0xFFFFFFFF); // GPU_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x1004,0xFFFFFFFF); // JOB_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x2004,0xFFFFFFFF); // MMU_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x0028,0x00000000); // GPU_IRQ_MASK
  Mali_WrReg(0x20,0x0,0x1008,0x00000000); // JOB_IRQ_MASK
  Mali_WrReg(0x20,0x0,0x2008,0x00000000); // MMU_IRQ_MASK
  return 1;
}

int RunMaliTest_sim2_t760_part3 (void) {
  int res = 0;
  int gpu_irq_status, job_irq_status, mmu_irq_status;
  gpu_irq_status = Mali_RdReg(0x20,0x0,0x002c);
  job_irq_status = Mali_RdReg(0x20,0x0,0x100c);
  mmu_irq_status = Mali_RdReg(0x20,0x0,0x200c);
  if( (gpu_irq_status == 0) && (job_irq_status == 0) && (mmu_irq_status == 1) ) {
    printf("MMU Interrupt asserted\n");
    Mali_WrReg(0x20,0x0,0x0024,0xFFFFFFFF); // GPU_IRQ_CLEAR
    Mali_WrReg(0x20,0x0,0x1004,0xFFFFFFFF); // JOB_IRQ_CLEAR
    Mali_WrReg(0x20,0x0,0x2004,0xFFFFFFFF); // MMU_IRQ_CLEAR
    Mali_WrReg(0x20,0x0,0x0028,0xFFFFFFFF); // GPU_IRQ_MASK
    Mali_WrReg(0x20,0x0,0x1008,0xFFFFFFFF); // JOB_IRQ_MASK
    Mali_WrReg(0x20,0x0,0x2008,0xFFFFFFFF); // MMU_IRQ_MASK
    Mali_WrReg(0x20,0x0,0x2000,0x00000000); // MMU_IRQ_RAWSTAT
    mali_step++;
    return 0;
  }
  printf("FAILURE: Expected MMU interrupt. Got the following interrupts: GPU: %x  JOB: %x  MMU: %x\n",
         gpu_irq_status, job_irq_status, mmu_irq_status);
  Mali_WrReg(0x20,0x0,0x0024,0xFFFFFFFF); // GPU_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x1004,0xFFFFFFFF); // JOB_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x2004,0xFFFFFFFF); // MMU_IRQ_CLEAR
  Mali_WrReg(0x20,0x0,0x0028,0x00000000); // GPU_IRQ_MASK
  Mali_WrReg(0x20,0x0,0x1008,0x00000000); // JOB_IRQ_MASK
  Mali_WrReg(0x20,0x0,0x2008,0x00000000); // MMU_IRQ_MASK
  return 1;
}
#undef printf

