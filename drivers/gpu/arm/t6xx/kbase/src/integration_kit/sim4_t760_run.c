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

This test checks that the GPU's different power domains can be turned on and off.

It makes sure the power on or off status corresponds to the relevant domain's READY status.


DEBUG HINTS IN CASE OF FAILURE
===========================
a) Check the APB bus connections as this is the bus that is used to communicate with the Job manager.
b) Check that the GPU's reset signal is not asserted.
c) Check that the GPU input clock is toggling as expected.
d) Check that the DFT signals are disabled.
e) Check that MBIST is disabled.

*/

#include "MaliFns.h"
#include "MaliDefines-t760.h"

int RunMaliTest_sim4_t760_part0 (void);

static int mali_step;
static int Mali_test_reg(int unit, int core, int regnum, int read_mask, int write_mask, int reset_value, int access);

int RunMaliTest_sim4_t760 (int *base) {
	
  	Mali_SetBase(base);
	RunMaliTest_sim4_t760_part0();
  
};
#define printf printk
int RunMaliTest_sim4_t760_part0 (void) {
  int res = 0;

  int gpuid, num_cores, l2_size, axi_width, i, as_present, js_present, core_bitmap;

  printf("RUNNING TEST: sim4\n");
  printf("  Purpose: Check APB register accesses\n");
  printf("  Will check register read/write and reset value\n");

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

  res |= Mali_test_reg(0, 0, 0x000, 0xffff0000, 0xffff0000, GPU_ID_VALUE, 1); /* GPU_ID */
  res |= Mali_test_reg(0, 0, 0x100, 0xffffffff, 0xffffffff,  core_bitmap, 1); /* SHADER_PRESENT_LO */
  res |= Mali_test_reg(0, 0, 0x110, 0xffffffff, 0xffffffff, 0x00000001, 1); /* TILER_PRESENT_LO */

#if MALI == t760
  /* T760 has only one logical L2C */
  res |= Mali_test_reg (0, 0, 0x120, 0xffffffff, 0xffffffff, 0x00000001, 1); /* L2_PRESENT_LO */
#else
  if (num_cores>=6)
  { /* T608 MP6 and T608 MP8 have two L2Cs*/
    res |= Mali_test_reg (0, 0, 0x120, 0xffffffff, 0xffffffff, 0x00000011, 1); /* L2_PRESENT_LO */
  }
  else
  {
    res |= Mali_test_reg (0, 0, 0x120, 0xffffffff, 0xffffffff, 0x00000001, 1); /* L2_PRESENT_LO */
  }
#endif

  res |= Mali_test_reg(0, 0, 0x140, 0xffffffff, 0xffffffff, 0x00000000, 1); /* SHADER_READY_LO */
  res |= Mali_test_reg(0, 0, 0x150, 0xffffffff, 0xffffffff, 0x00000000, 1); /* TILER_READY_LO */
  res |= Mali_test_reg(0, 0, 0x160, 0xffffffff, 0xffffffff, 0x00000000, 1); /* L2_READY_LO */
  for(i=0; i<num_cores; i++) {
    printf("Power on/off shader core #%d\n", i);
    Mali_WrReg(0, 0, 0x180, (1<<i)); /* SHADER_PWRON_LO */
    while( Mali_RdReg(0, 0, 0x200) ) {}
    res |= Mali_test_reg(0, 0, 0x140, 0xffffffff, 0xffffffff, (1<<i), 1); /* SHADER_READY_LO */
    Mali_WrReg(0, 0, 0x1c0, (1<<i)); /* SHADER_PWROFF_LO */
    while( Mali_RdReg(0, 0, 0x200) ) {}
    res |= Mali_test_reg(0, 0, 0x140, 0xffffffff, 0xffffffff, 0, 1); /* SHADER_READY_LO */
  }

  printf("Power on/off tiler\n", i);
  Mali_WrReg(0, 0, 0x190, 1); /* TILER_PWRON_LO */
  while( Mali_RdReg(0, 0, 0x210) ) {}
  res |= Mali_test_reg(0, 0, 0x150, 0xffffffff, 0xffffffff, 1, 1); /* TILER_READY_LO */
  Mali_WrReg(0, 0, 0x1d0, 1); /* TILER_PWROFF_LO */
  while( Mali_RdReg(0, 0, 0x210) ) {}
  res |= Mali_test_reg(0, 0, 0x150, 0xffffffff, 0xffffffff, 0, 1); /* TILER_READY_LO */

  printf("Power on/off L2\n", i);
#if MALI == t760
  /* T760 has only one logical L2C */
  Mali_WrReg(0, 0, 0x1a0, 1); /* L2_PWRON_LO */
  while( Mali_RdReg(0, 0, 0x220) ) {}
  res |= Mali_test_reg(0, 0, 0x160, 0xffffffff, 0xffffffff, 1, 1); /* L2_READY_LO */
  Mali_WrReg(0, 0, 0x1e0, 1); /* L2_PWROFF_LO */
  while( Mali_RdReg(0, 0, 0x220) ) {}
  res |= Mali_test_reg(0, 0, 0x160, 0xffffffff, 0xffffffff, 0, 1); /* L2_READY_LO */
#else
  if (num_cores>=6)
  { /* T608 MP6 and T608 MP8 have two L2Cs*/
    Mali_WrReg(0, 0, 0x1a0, 17); /* L2_PWRON_LO */
    while( Mali_RdReg(0, 0, 0x220) ) {}
    res |= Mali_test_reg(0, 0, 0x160, 0xffffffff, 0xffffffff, 17, 1); /* L2_READY_LO */
    Mali_WrReg(0, 0, 0x1e0, 17); /* L2_PWROFF_LO */
    while( Mali_RdReg(0, 0, 0x220) ) {}
    res |= Mali_test_reg(0, 0, 0x160, 0xffffffff, 0xffffffff, 0, 1); /* L2_READY_LO */
  }
  else
  {
    Mali_WrReg(0, 0, 0x1a0, 1); /* L2_PWRON_LO */
    while( Mali_RdReg(0, 0, 0x220) ) {}
    res |= Mali_test_reg(0, 0, 0x160, 0xffffffff, 0xffffffff, 1, 1); /* L2_READY_LO */
    Mali_WrReg(0, 0, 0x1e0, 1); /* L2_PWROFF_LO */
    while( Mali_RdReg(0, 0, 0x220) ) {}
    res |= Mali_test_reg(0, 0, 0x160, 0xffffffff, 0xffffffff, 0, 1); /* L2_READY_LO */
  }
#endif


  mali_step++;
  return res;
}


/* Mali_test_reg
   Test the register.
   access: 0=RW, 1=RO
*/
static int Mali_test_reg(int unit, int core, int regnum, int read_mask, int write_mask, int reset_value, int access) {
  int value = Mali_RdReg(unit, core, regnum);
  if( value != reset_value ) {
    printf("FAILURE: Wrong reset value. Addr: 0x%08x Value: 0x%08x Expected: 0x%08x\n",
           (unit<<28)+(core<<16)+regnum, value, reset_value);
    return -1;
  }
  if( access == 0 ) {
    Mali_WrReg(unit, core, regnum, (0xffffffff & write_mask));
    value = Mali_RdReg(unit, core, regnum) & read_mask;
    if( value != (0xffffffff & write_mask & read_mask) ) {
      printf("FAILURE: Wrong value. Addr: 0x%08x Value: 0x%08x Expected: 0x%08x\n",
             (unit<<28)+(core<<16)+regnum, value, (0xffffffff & write_mask & read_mask));
      return -2;
    }
    Mali_WrReg(unit, core, regnum, (0x12345678 & write_mask));
    value = Mali_RdReg(unit, core, regnum) & read_mask;
    if( value != (0x12345678 & write_mask & read_mask) ) {
      printf("FAILURE: Wrong value. Addr: 0x%08x Value: 0x%08x Expected: 0x%08x\n",
             (unit<<28)+(core<<16)+regnum, value, (0x12345678 & write_mask & read_mask));
      return -2;
    }
  }
  printf("Register %08x: Success!\n", (unit<<28)+(core<<16)+regnum);
  return 0;
}

#undef printf
