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

This test performs a basic check on the GPU's ability to access external memory on its ACE-Lite port.

A Null job descriptor is placed in memory which is then run by the GPU.  The status of the job is written back to memory (each job descriptor has a status field).

On successful completion of the job, a job interrupt is triggered and the test is allowed to complete.  



DEBUG HINTS IN CASE OF FAILURE
===========================
a) Check the APB bus connections as this is the bus that is used to communicate with the Job manager.
b) Check that the GPU's reset signal is not asserted.
c) Check that the GPU input clock is toggling as expected.
d) Check that the DFT signals are disabled.
e) Check that MBIST is disabled.
f) Check every memory accesses from the GPU completes correctly.
g) Check the GPU interrupt signal IRQGPU is correctly connected (sim2 should pass if this is okay).
h) Check the GPU interrupt signal IRQMMU is correctly connected (sim2 should pass if this is okay).
i) Check the GPU interrupt signal IRQJOB is correctly connected (sim2 should pass if this is okay).

*/

#include "MaliFns.h"
#include "MaliDefines-t760.h"

#include "sim3_t760_check_0_a.h"
#include "sim3_t760_mem_0.h"

int Check_sim3_t760_check_0_a (struct t_sim3_t760_mem_0 *p);

int RunMaliTest_sim3_t760_part0 (void);
int RunMaliTest_sim3_t760_part1 (void);

static volatile struct t_sim3_t760_mem_0 *malidata_0;
static int mali_step;

int RunMaliTest_sim3_t760 (int *base) {
	
	Mali_SetBase(base);

    RunMaliTest_sim3_t760_part0();
    RunMaliTest_sim3_t760_part1();
 
};

#define printf printk
int RunMaliTest_sim3_t760_part0 (void) {
  int res = 0;

  int gpuid, num_cores, l2_size, axi_width, i, as_present, js_present, core_bitmap;

  printf("RUNNING TEST: sim3\n");
  printf("  Purpose: Check AXI connectivity\n");
  printf("  Will run a simple NULL job which reads and writes data to memory.\n");

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

  Mali_Reset();
  printk("JOB_IRQ_CLEAR is %x\n", JOB_IRQ_CLEAR);
  Mali_WrReg(0x20,0x0,JOB_IRQ_CLEAR,0xFFFFFFFF);
  Mali_WrReg(0x20,0x0,JOB_IRQ_MASK,0xFFFFFFFF);
  Mali_WrReg(0x20,0x0,JS1_HEAD_NEXT_LO,0xFFFFFFFF);
  Mali_WrReg(0x20,0x0,TILER_PWRON_LO,0xFFFFFFFF);
  Mali_WrReg(0x20,0x0,JS1_CONFIG_NEXT,0x00001000);
  Mali_WrReg(0x20,0x0,JS1_HEAD_NEXT_LO,0x00001000);
  if (num_cores>=6)
  {
    Mali_WrReg(0x20,0x0,JS1_AFFINITY_NEXT_LO,0x0000000F);
  }
  else
  {
    Mali_WrReg(0x20,0x0,JS1_AFFINITY_NEXT_LO,0xFFFFFFFF);
  }
  //(struct t_sim3_t760_mem_0 *)malidata_0 = Mali_LdMem(&sim3_t760_mem_0,sizeof(struct t_sim3_t760_mem_0),(int)sim3_t760_mem_0.ttb);
  
  malidata_0 = (struct t_sim3_t760_mem_0 *)Mali_LdMem(&sim3_t760_mem_0,sizeof(struct t_sim3_t760_mem_0),(int)sim3_t760_mem_0.ttb);
  /* Enable optional performance counting */
  Mali_InitPerfCounters();
  Mali_WrReg(0x20,0x0,GPU_IRQ_CLEAR,0xFFFFFFFF);
  Mali_WrReg(0x20,0x0,GPU_IRQ_MASK,0x00000000);
  Mali_WrReg(0x20,0x0,JS1_COMMAND_NEXT,0x00000001);
  mali_step++;
  if (res == 0) {
   return 254;
  }
  return res;
}

int RunMaliTest_sim3_t760_part1 (void) {
  int res = 0;
  int have_interrupts = 0;
  /* Check for any interrupt */
  have_interrupts = Mali_InterruptCheck(0xFFFFFFFF,0xFFFFFFFF);
  if (!have_interrupts) {
   return 255;
  }

  Mali_ReadPerfCounters();
  //Mali_JobPartDone();
  Mali_CheckReg(32, 0, 0x100c, 0x00000002);
  res |= Check_sim3_t760_check_0_a(malidata_0);

  Mali_MaskAllInterrupts();
  mali_step++;
  return res;
}

int Check_sim3_t760_check_0_a (struct t_sim3_t760_mem_0 *p) {
  struct t_CheckData_sim3_t760_check_0_a *r = &CheckData_sim3_t760_check_0_a;
  int res = 0;
  printk("Performing data check of 32 bytes\n");

  res |= Mali_MemCmp(p->data_00001000, 0x00001000, r->data_00001000, 0, 0x008);
  /* Return -1 if a compare fail */
  if (res) { return -1; };
  printk("Check okay \n");
  return 0;
};
#undef printf
