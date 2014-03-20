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
#define MALI_ATTR (0xC0+0x200)
/* ###########################
 * Data structure definition for: sim3_t760_mem_0
 * ########################### */
struct t_sim3_t760_mem_0 {
  unsigned int **ttb[4 * 1024];
  unsigned int data_00001000[1024];
};
extern volatile struct t_sim3_t760_mem_0 sim3_t760_mem_0;

