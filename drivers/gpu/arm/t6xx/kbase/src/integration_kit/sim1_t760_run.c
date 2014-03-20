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

This test tests that most GPU registers can be read and written via the APB interface.



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



int RunMaliTest_sim1_t760_part0 ();
static int Mali_test_reg(int unit, int core, int regnum, int read_mask, int write_mask, int reset_value, int access, char * reg_str);

int log2_of_x (int myval);

static int mali_step;


int RunMaliTest_sim1_t760 (int *base) {

	Mali_SetBase(base);
    RunMaliTest_sim1_t760_part0();
 
};

#define printf printk

int RunMaliTest_sim1_t760_part0 () {
  int res = 0;		
  int gpuid, num_cores, l2_size, axi_width, i, as_present, js_present, core_bitmap, vTEXTURE_FEATURES_0;


  printf("RUNNING TEST: sim1\n");
  printf("  Purpose: Check APB register accesses\n");
  printf("  Will check register read/write and reset value\n");

  // Get current configuration to allow testing all registers
  gpuid         = Mali_RdReg(0x20, 0, 0x0000);
  core_bitmap   = Mali_RdReg(0x20, 0, 0x0100);
  l2_size       = ((Mali_RdReg(0x20, 0, 0x0004) >> 16) & 0xFF);
  axi_width     = (1 <<((Mali_RdReg(0x20, 0, 0x0004) >> 24) & 0xFF));
  as_present    = Mali_RdReg(0x20, 0, 0x0018);
  js_present    = Mali_RdReg(0x20, 0, 0x001c);
  vTEXTURE_FEATURES_0 = Mali_RdReg(0x20, 0, 0x00b0);

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

  printf("Reading/Writing GPU Configuration and Control registers:\n");
  res |= Mali_test_reg (0, 0, 0x000, 0xffff0000, 0xffff0000, GPU_ID_VALUE, 1, "GPU_ID"); 
  res |= Mali_test_reg (0, 0, 0x004, 0xffffffff, 0xffffffff, (0x00000206 + (l2_size << 16) | (log2_of_x(axi_width) << 24)), 1, "L2_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x008, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x00c, 0xffffffff, 0xffffffff, 0x00000809, 1, "TILER_FEATURES"); 
#if MALI == t760
  /*
   Skrymir has a new field 
    MEM_FEATURES[11:8] = L2_SLICES 
  */
  res |= Mali_test_reg (0, 0, 0x010, 0xffffffff, 0xffffffff, 0x00000101, 1, "MEM_FEATURES"); 
#else
  res |= Mali_test_reg (0, 0, 0x010, 0xffffffff, 0xffffffff, 0x00000001, 1, "MEM_FEATURES"); 
#endif

  res |= Mali_test_reg (0, 0, 0x014, 0xffffffff, 0xffffffff, 0x00002830, 1, "MMU_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x018, 0xffffffff, 0xffffffff, as_present, 1, "AS_PRESENT"); 
  res |= Mali_test_reg (0, 0, 0x01c, 0xffffffff, 0xffffffff, js_present, 1, "JS_PRESENT"); 
  /* GPU config regs */

   res |= Mali_test_reg(0, 0, 0x020, 0xffffffff, 0xffffffff, 0x00000100, 1, "GPU_IRQ_RAWSTAT");
   res |= Mali_test_reg(0, 0, 0x028, 0xffffffff, 0xffffffff, 0x00000000, 1, "GPU_IRQ_MASK");
   res |= Mali_test_reg(0, 0, 0x02c, 0xffffffff, 0xffffffff, 0x00000000, 1, "GPU_IRQ_STATUS");

  /* Job Control */

   res |= Mali_test_reg(0, 0, 0x1000, 0xffffffff, 0xffffffff, 0x00000000, 1, "JOB_IRQ_RAWSTAT");
   res |= Mali_test_reg(0, 0, 0x1008, 0xffffffff, 0xffffffff, 0x00000000, 1, "JOB_IRQ_MASK");
   res |= Mali_test_reg(0, 0, 0x100c, 0xffffffff, 0xffffffff, 0x00000000, 1, "JOB_IRQ_STATUS");

  /* MMU regs */

  res |= Mali_test_reg(0, 0, 0x2000, 0xffffffff, 0xffffffff, 0x00000000, 1, "MMU_IRQ_RAWSTAT");
  res |= Mali_test_reg(0, 0, 0x2008, 0xffffffff, 0xffffffff, 0x00000000, 1, "MMU_IRQ_MASK");
  res |= Mali_test_reg(0, 0, 0x200c, 0xffffffff, 0xffffffff, 0x00000000, 1, "MMU_IRQ_STATUS");
  
  /*  res |= Mali_test_reg(0, 0, 0x030, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* GPU_COMMAND */ /* Write only */
  res |= Mali_test_reg (0, 0, 0x034, 0xffffffff, 0xffffffff, 0x00000000, 1, "GPU_STATUS"); 
  /*  res |= Mali_test_reg(0, 0, 0x038, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x03c, 0xffffffff, 0xffffffff, 0x00000000, 1, "GPU_FAULTSTATUS"); 
  res |= Mali_test_reg (0, 0, 0x040, 0xffffffff, 0xffffffff, 0x00000000, 1, "GPU_FAULTADDRESS_LO"); 
  res |= Mali_test_reg (0, 0, 0x044, 0xffffffff, 0xffffffff, 0x00000000, 1, "GPU_FAULTADDRESS_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x048, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x04c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x050, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* PWR_KEY */ /* Write only */
  /*  res |= Mali_test_reg(0, 0, 0x054, 0xffffffff, 0xffffffff, 0x00000000, 0); */ /* PWR_OVERRIDE0 */ /* Need PWR_KEY to work */
  /*  res |= Mali_test_reg(0, 0, 0x058, 0xffffffff, 0xffffffff, 0x00000000, 0); */ /* PWR_OVERRIDE1 */ /* Need PWR_KEY to work */ 
  /*  res |= Mali_test_reg(0, 0, 0x05c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x060, 0xffffffff, 0xfffff800, 0x00000000, 0, "PRFCNT_BASE_LO"); 
  res |= Mali_test_reg (0, 0, 0x064, 0xffffffff, 0x0000ffff, 0x00000000, 0, "PRFCNT_BASE_HI"); 
  res |= Mali_test_reg (0, 0, 0x068, 0xffffffff, 0x000000ff, 0x00000000, 0, "PRFCNT_CONFIG"); 
  res |= Mali_test_reg (0, 0, 0x06c, 0xffffffff, 0x000000ff, 0x00000000, 0, "PRFCNT_JM_EN"); 
  res |= Mali_test_reg (0, 0, 0x070, 0xffffffff, 0x0000ffff, 0x00000000, 0, "PRFCNT_SHADER_EN"); 
  res |= Mali_test_reg (0, 0, 0x074, 0xffffffff, 0x0000ffff, 0x00000000, 0, "PRFCNT_TILER_EN"); 
  res |= Mali_test_reg (0, 0, 0x078, 0xffffffff, 0xffffffff, 0x00000000, 1, "PRFCNT_L3_CACHE_EN"); 
  res |= Mali_test_reg (0, 0, 0x07c, 0xffffffff, 0x0000ffff, 0x00000000, 0, "PRFCNT_MMU_L2_EN"); 
  /*  res |= Mali_test_reg(0, 0, 0x080, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x084, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x088, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x08c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x090, 0xffffffff, 0xffffffff, 0x00000000, 1, "CYCLE_COUNT_LO"); 
  res |= Mali_test_reg (0, 0, 0x094, 0xffffffff, 0xffffffff, 0x00000000, 1, "CYCLE_COUNT_HI"); 
  res |= Mali_test_reg (0, 0, 0x098, 0xffffffff, 0xffffffff, 0x00000000, 1, "TIMESTAMP_LO"); 
  res |= Mali_test_reg (0, 0, 0x09c, 0xffffffff, 0xffffffff, 0x00000000, 1, "TIMESTAMP_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x0a0, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x0a4, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x0a8, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x0ac, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */

  /* printf ("TEXTURE_FEATURES_0 is set to 0x%08x", Mali_RdReg(0x20, 0, 0x00b0)); */
  printf ("                         \n");
  printf ("                        Texture Compression Formats::\n");
  printf ("                        TEXTURE_FEATURES_0 = 0x%08x\n"   , vTEXTURE_FEATURES_0);
  printf ("                         \n");
  printf ("                        ETC2=%d\n"                       , ((vTEXTURE_FEATURES_0>> 1)&0x00000001)                                        ); 
  printf ("                        ETC2+EAC=%d\n"                   , ((vTEXTURE_FEATURES_0>> 3)&0x00000001)                                        );
  printf ("                        EAC_1_COMPONENT=%d\n"            , ((vTEXTURE_FEATURES_0>> 2)&0x00000001)                                        );
  printf ("                        EAC_2_COMPONENTS=%d\n"           , ((vTEXTURE_FEATURES_0>> 4)&0x00000001)                                        );
  printf ("                        EAC_SNORM_1_COMPONENT=%d\n"      , ((vTEXTURE_FEATURES_0>>17)&0x00000001)                                        );
  printf ("                        EAC_SNORM_2_COMPONENTS=%d\n"     , ((vTEXTURE_FEATURES_0>>18)&0x00000001)                                        );
  printf ("                        ETC2+Punch-Through Alpha=%d\n"   , ((vTEXTURE_FEATURES_0>>19)&0x00000001)                                        );
  printf ("                         \n");
  printf ("                        NXR=%d\n"                        , ((vTEXTURE_FEATURES_0>> 6)&0x00000001)                                        );
  printf ("                         \n");
  printf ("                        BC1_UNORM=%d\n"                  , ((vTEXTURE_FEATURES_0>> 7)&0x00000001)                                        );
  printf ("                        BC2_UNORM=%d\n"                  , ((vTEXTURE_FEATURES_0>> 8)&0x00000001)                                        );
  printf ("                        BC3_UNORM=%d\n"                  , ((vTEXTURE_FEATURES_0>> 9)&0x00000001)                                        );
  printf ("                        BC4_UNORM=%d BC4_SNORM=%d\n"     , ((vTEXTURE_FEATURES_0>>10)&0x00000001), ((vTEXTURE_FEATURES_0>>11)&0x00000001)); 
  printf ("                        BC5_UNORM=%d BC5_SNORM=%d\n"     , ((vTEXTURE_FEATURES_0>>12)&0x00000001), ((vTEXTURE_FEATURES_0>>13)&0x00000001)); 
  printf ("                        BC6H_UF16=%d BC6H_SF16=%d\n"     , ((vTEXTURE_FEATURES_0>>14)&0x00000001), ((vTEXTURE_FEATURES_0>>15)&0x00000001)); 
  printf ("                        BC7_UNORM=%d\n"                  , ((vTEXTURE_FEATURES_0>>16)&0x00000001)                                        ); 
  printf ("                         \n");
  printf ("                        ASTC_3D_LDR=%d ASTC_3D_HDR=%d\n" , ((vTEXTURE_FEATURES_0>>20)&0x00000001), ((vTEXTURE_FEATURES_0>>21)&0x00000001)); 
  printf ("                        ASTC_2D_LDR=%d ASTC_2D_HDR=%d\n" , ((vTEXTURE_FEATURES_0>>22)&0x00000001), ((vTEXTURE_FEATURES_0>>23)&0x00000001)); 
  printf ("                         \n");
  res |= Mali_test_reg (0, 0, 0x0b4, 0xffffffff, 0xffffffff, 0x0000ffff, 1, "TEXTURE_FEATURES_1"); 
  res |= Mali_test_reg (0, 0, 0x0b8, 0xffffffff, 0xffffffff, 0x9f81ffff, 1, "TEXTURE_FEATURES_2"); 
  /*  res |= Mali_test_reg(0, 0, 0x0bc, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x0c0, 0xffffffff, 0xffffffff, 0x0000020e, 1, "JS0_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0c4, 0xffffffff, 0xffffffff, 0x000001fe, 1, "JS1_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0c8, 0xffffffff, 0xffffffff, 0x0000007e, 1, "JS2_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0cc, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS3_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0d0, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS4_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0d4, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS5_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0d8, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS6_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0dc, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS7_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0e0, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS8_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0e4, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS9_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0e8, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS10_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0ec, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS11_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0f0, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS12_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0f4, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS13_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0f8, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS14_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x0fc, 0xffffffff, 0xffffffff, 0x00000000, 1, "JS15_FEATURES"); 
  res |= Mali_test_reg (0, 0, 0x100, 0xffffffff, 0xffffffff,  core_bitmap, 1, "SHADER_PRESENT_LO"); 
  res |= Mali_test_reg (0, 0, 0x104, 0xffffffff, 0xffffffff, 0x00000000, 1, "SHADER_PRESENT_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x108, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x10c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x110, 0xffffffff, 0xffffffff, 0x00000001, 1, "TILER_PRESENT_LO"); 
  res |= Mali_test_reg (0, 0, 0x114, 0xffffffff, 0xffffffff, 0x00000000, 1, "TILER_PRESENT_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x118, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x11c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */

#if MALI == t760
  /* T760 has only one logical L2C */
  res |= Mali_test_reg (0, 0, 0x120, 0xffffffff, 0xffffffff, 0x00000001, 1, "L2_PRESENT_LO"); 
#else
  if (num_cores>=6)
  { /* T608 MP6 and T608 MP8 have two L2Cs*/
    res |= Mali_test_reg (0, 0, 0x120, 0xffffffff, 0xffffffff, 0x00000011, 1, "L2_PRESENT_LO"); 
  }
  else
  {
    res |= Mali_test_reg (0, 0, 0x120, 0xffffffff, 0xffffffff, 0x00000001, 1, "L2_PRESENT_LO"); 
  }
#endif
  res |= Mali_test_reg (0, 0, 0x124, 0xffffffff, 0xffffffff, 0x00000000, 1, "L2_PRESENT_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x128, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x12c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x130, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_PRESENT_LO"); 
  res |= Mali_test_reg (0, 0, 0x134, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_PRESENT_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x138, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x13c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x140, 0xffffffff, 0xffffffff, 0x00000000, 1, "SHADER_READY_LO"); 
  res |= Mali_test_reg (0, 0, 0x144, 0xffffffff, 0xffffffff, 0x00000000, 1, "SHADER_READY_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x148, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x14c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x150, 0xffffffff, 0xffffffff, 0x00000000, 1, "TILER_READY_LO"); 
  res |= Mali_test_reg (0, 0, 0x154, 0xffffffff, 0xffffffff, 0x00000000, 1, "TILER_READY_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x158, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x15c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x160, 0xffffffff, 0xffffffff, 0x00000000, 1, "L2_READY_LO"); 
  res |= Mali_test_reg (0, 0, 0x164, 0xffffffff, 0xffffffff, 0x00000000, 1, "L2_READY_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x168, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x16c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x170, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_READY_LO"); 
  res |= Mali_test_reg (0, 0, 0x174, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_READY_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x178, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x17c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x180, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* SHADER_PWRON_LO */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x184, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* SHADER_PWRON_HI */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x188, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x18c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x190, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* TILER_PWRON_LO */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x194, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* TILER_PWRON_HI */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x198, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x19c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1a0, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* L2_PWRON_LO */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1a4, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* L2_PWRON_HI */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1a8, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1ac, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1b0, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* L3_PWRON_LO */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1b4, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* L3_PWRON_HI */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1b8, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1bc, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1c0, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* SHADER_PWROFF_LO */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1c4, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* SHADER_PWROFF_HI */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1c8, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1cc, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1d0, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* TILER_PWROFF_LO */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1d4, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* TILER_PWROFF_HI */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1d8, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1dc, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1e0, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* L2_PWROFF_LO */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1e4, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* L2_PWROFF_HI */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1e8, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1ec, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1f0, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* L3_PWROFF_LO */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1f4, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* L3_PWROFF_HI */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1f8, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x1fc, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x200, 0xffffffff, 0xffffffff, 0x00000000, 1, "SHADER_PWRTRANS_LO"); 
  res |= Mali_test_reg (0, 0, 0x204, 0xffffffff, 0xffffffff, 0x00000000, 1, "SHADER_PWRTRANS_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x208, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x20c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x210, 0xffffffff, 0xffffffff, 0x00000000, 1, "TILER_PWRTRANS_LO"); 
  res |= Mali_test_reg (0, 0, 0x214, 0xffffffff, 0xffffffff, 0x00000000, 1, "TILER_PWRTRANS_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x218, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x21c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x220, 0xffffffff, 0xffffffff, 0x00000000, 1, "L2_PWRTRANS_LO"); 
  res |= Mali_test_reg (0, 0, 0x224, 0xffffffff, 0xffffffff, 0x00000000, 1, "L2_PWRTRANS_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x228, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x22c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x230, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_PWRTRANS_LO"); 
  res |= Mali_test_reg (0, 0, 0x234, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_PWRTRANS_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x238, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x23c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x240, 0xffffffff, 0xffffffff, 0x00000000, 1, "SHADER_PWRACTIVE_LO"); 
  res |= Mali_test_reg (0, 0, 0x244, 0xffffffff, 0xffffffff, 0x00000000, 1, "SHADER_PWRACTIVE_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x248, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x24c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x250, 0xffffffff, 0xffffffff, 0x00000000, 1, "TILER_PWRACTIVE_LO"); 
  res |= Mali_test_reg (0, 0, 0x254, 0xffffffff, 0xffffffff, 0x00000000, 1, "TILER_PWRACTIVE_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x258, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x25c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x260, 0xffffffff, 0xffffffff, 0x00000000, 1, "L2_PWRACTIVE_LO"); 
  res |= Mali_test_reg (0, 0, 0x264, 0xffffffff, 0xffffffff, 0x00000000, 1, "L2_PWRACTIVE_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x268, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x26c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  res |= Mali_test_reg (0, 0, 0x270, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_PWRACTIVE_LO"); 
  res |= Mali_test_reg (0, 0, 0x274, 0xffffffff, 0xffffffff, 0x00000000, 1, "L3_PWRACTIVE_HI"); 
  /*  res |= Mali_test_reg(0, 0, 0x278, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
  /*  res |= Mali_test_reg(0, 0, 0x27c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */

  printf("Reading/Writing JOB Configuration and Control registers:\n");
  /*  res |= Mali_test_reg(0, 0, 0x1000, 0xffffffff, 0xffffffff, 0x00000000, 0); */ /* JOB_IRQ_RAWSTAT */
  /*  res |= Mali_test_reg(0, 0, 0x1004, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* JOB_IRQ_CLEAR */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x1008, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* JOB_IRQ_MASK */
  /*  res |= Mali_test_reg(0, 0, 0x100c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* JOB_IRQ_STATUS */
  for(i=0;i<16;i++) {
    if( ((js_present >> i) & 1) ) {
      res |= Mali_test_reg (0, 0, 0x1800 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1, "JSx_HEAD_LO"); 
      res |= Mali_test_reg (0, 0, 0x1804 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1, "JSx_HEAD_HI"); 
      res |= Mali_test_reg (0, 0, 0x1808 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1, "JSx_TAIL_LO"); 
      res |= Mali_test_reg (0, 0, 0x180c + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1, "JSx_TAIL_HI"); 
      res |= Mali_test_reg (0, 0, 0x1810 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1, "JSx_AFFINITY_LO"); 
      res |= Mali_test_reg (0, 0, 0x1814 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1, "JSx_AFFINITY_HI"); 
      res |= Mali_test_reg (0, 0, 0x1818 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1, "JSx_CONFIG"); 
      /*  res |= Mali_test_reg(0, 0, 0x181c + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      /*  res |= Mali_test_reg(0, 0, 0x1820 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* JSx_COMMAND */ /* Write Only */
      res |= Mali_test_reg (0, 0, 0x1824 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1, "JSx_STATUS"); 
      /*  res |= Mali_test_reg(0, 0, 0x1828 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      /*  res |= Mali_test_reg(0, 0, 0x182c + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      /*  res |= Mali_test_reg(0, 0, 0x1830 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      /*  res |= Mali_test_reg(0, 0, 0x1834 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      /*  res |= Mali_test_reg(0, 0, 0x1838 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      /*  res |= Mali_test_reg(0, 0, 0x183c + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      res |= Mali_test_reg (0, 0, 0x1840 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 0, "JSx_HEAD_NEXT_LO"); 
      res |= Mali_test_reg (0, 0, 0x1844 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 0, "JSx_HEAD_NEXT_HI"); 
      /*  res |= Mali_test_reg(0, 0, 0x1848 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      /*  res |= Mali_test_reg(0, 0, 0x184c + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      res |= Mali_test_reg (0, 0, 0x1850 + i*0x80, 0xffffffff, 0x00000001, 0x00000000, 0, "JSx_AFFINITY_NEXT_LO"); 
      res |= Mali_test_reg (0, 0, 0x1854 + i*0x80, 0xffffffff, 0x00000000, 0x00000000, 0, "JSx_AFFINITY_NEXT_HI"); 
      res |= Mali_test_reg (0, 0, 0x1858 + i*0x80, 0xffffffff, 0x0000370f, 0x00000000, 0, "JSx_CONFIG_NEXT"); 
      /*  res |= Mali_test_reg(0, 0, 0x185c + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* Reserved */
      /*  res |= Mali_test_reg(0, 0, 0x1860 + i*0x80, 0xffffffff, 0xffffffff, 0x00000000, 0); */ /* JSx_COMMAND_NEXT */ /* Write Only? */
    }
  }

  printf("Reading/Writing MMU Configuration and Control registers:\n");
  /*  res |= Mali_test_reg(0, 0, 0x2000, 0xffffffff, 0xffffffff, 0x00000000, 0); */ /* MMU_IRQ_RAWSTAT */
  /*  res |= Mali_test_reg(0, 0, 0x2004, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* MMU_IRQ_CLEAR */ /* Write Only */
  /*  res |= Mali_test_reg(0, 0, 0x2008, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* MMU_IRQ_MASK */
  /*  res |= Mali_test_reg(0, 0, 0x200c, 0xffffffff, 0xffffffff, 0x00000000, 1); */ /* MMU_IRQ_STATUS */
  for(i=0;i<16;i++) {
    if( ((as_present >> i) & 1) ) {
      res |= Mali_test_reg (0, 0, 0x2400 + i*0x40, 0xffffffff, 0xfffff01f, 0x00000000, 0, "ASx_TRANSTAB_LO"); 
      res |= Mali_test_reg (0, 0, 0x2404 + i*0x40, 0xffffffff, 0x000000ff, 0x00000000, 0, "ASx_TRANSTAB_HI"); 
      res |= Mali_test_reg (0, 0, 0x2408 + i*0x40, 0xffffffff, 0xcfcfcfcf, 0x00000000, 0, "ASx_MEMATTR_LO"); 
      res |= Mali_test_reg (0, 0, 0x240c + i*0x40, 0xffffffff, 0xcfcfcfcf, 0x00000000, 0, "ASx_MEMATTR_HI"); 
      res |= Mali_test_reg (0, 0, 0x2410 + i*0x40, 0xffffffff, 0xfffff03f, 0x00000000, 0, "ASx_LOCKADDR_LO"); 
      res |= Mali_test_reg (0, 0, 0x2414 + i*0x40, 0xffffffff, 0x0000ffff, 0x00000000, 0, "ASx_LOCKADDR_HI"); 
      /*    res |= Mali_test_reg(0, 0, 0x2418 + i*0x40, 0xffffffff, 0xffffffff, 0x00000000, 0); */ /* ASx_COMMAND */ /* Write Only */
      res |= Mali_test_reg (0, 0, 0x241c + i*0x40, 0xffffffff, 0xffffffff, 0x00000000, 1, "ASx_FAULTSTATUS"); 
      res |= Mali_test_reg (0, 0, 0x2420 + i*0x40, 0xffffffff, 0xffffffff, 0x00000000, 1, "ASx_FAULTADDRESS_LO"); 
      res |= Mali_test_reg (0, 0, 0x2424 + i*0x40, 0xffffffff, 0xffffffff, 0x00000000, 1, "ASx_FAULTADDRESS_HI"); 
      res |= Mali_test_reg (0, 0, 0x2428 + i*0x40, 0xffffffff, 0xffffffff, 0x00000000, 1, "ASx_STATUS"); 
    }
  }
  mali_step++;
  return res;
}


/* Mali_test_reg
   Test the register.
   access: 0=RW, 1=RO
*/
static int Mali_test_reg(int unit, int core, int regnum, int read_mask, int write_mask, int reset_value, int access, char * reg_str) {
  int value = Mali_RdReg(unit, core, regnum);
  if( value != reset_value ) {
    printf("FAILURE: Wrong reset value. Addr: 0x%08x Value: 0x%08x Expected: 0x%08x for %s\n",
           (unit<<28)+(core<<16)+regnum, value, reset_value, reg_str);
    return -1;
  }
  if( access == 0 ) {
    Mali_WrReg(unit, core, regnum, (0xffffffff & write_mask));
    value = Mali_RdReg(unit, core, regnum) & read_mask;
    if( value != (0xffffffff & write_mask & read_mask) ) {
      printf("FAILURE: Wrong value. Addr: 0x%08x Value: 0x%08x Expected: 0x%08x for %s\n",
             (unit<<28)+(core<<16)+regnum, value, (0xffffffff & write_mask & read_mask), reg_str);
      return -2;
    }
    Mali_WrReg(unit, core, regnum, (0x12345678 & write_mask));
    value = Mali_RdReg(unit, core, regnum) & read_mask;
    if( value != (0x12345678 & write_mask & read_mask) ) {
      printf("FAILURE: Wrong value. Addr: 0x%08x Value: 0x%08x Expected: 0x%08x for %s\n",
             (unit<<28)+(core<<16)+regnum, value, (0x12345678 & write_mask & read_mask), reg_str);
      return -2;
    }
  }
  printf("Register %08x:  for %s  Success!\n", (unit<<28)+(core<<16)+regnum, reg_str);
  return 0;
}

#undef printf


int log2_of_x (int myval) {
  int myresult = 0;
  while (myval > 1)
  {
    myval >>= 1;
    myresult++;
  }
  
  return myresult;
}
