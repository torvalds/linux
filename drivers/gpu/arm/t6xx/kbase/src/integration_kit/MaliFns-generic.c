/*----------------------------------------------------------------------------
*
*The confidential and proprietary information contained in this file may
*only be used by a person authorised under and to the extent permitted
*by a subsisting licensing agreement from ARM Limited.
*
*        (C) COPYRIGHT 2008-2009,2011,2012-2013 ARM Limited.
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
* Abstract :Implements all the generic APIs used for mali memory and register
* compares
*-----------------------------------------------------------------------------
* Overview
*----------
* 
**************************************************************/

#include "MaliFns.h"
//#include <stdio.h>
//#include <string.h>
#include <linux/string.h>
#include <linux/kernel.h>

#define printf printk
int Mali_MemCpy(unsigned int *malidata_page,
                unsigned int mali_va,
                unsigned int *refdata,
                unsigned int refoffset,
                unsigned int len) 
{
  memcpy((void *)((char *)malidata_page + (mali_va % 4096)),
               (void *)((char *)refdata + (refoffset)),
               len * sizeof(unsigned int));

  return 0;
};

int Mali_MemCpyMasked(unsigned int *malidata_page,
                      unsigned int mali_va,
                      unsigned int *refdata,
                      unsigned int *refmask,
                      unsigned int refoffset,
                      unsigned int len) 
{
  unsigned int *maskptr = (unsigned int *)((char *)refmask + (refoffset));
  unsigned int *refptr  = (unsigned int *)((char *)refdata + (refoffset));
  unsigned int *dataptr = (unsigned int *)((char *)malidata_page + (mali_va % 4096));
  unsigned int len2 = len;
  unsigned int m;

  while (len2--) {
    m   = *maskptr++;
	if (~m == 0) {
		*dataptr = *refptr;
	} else {
		*dataptr = (*dataptr & ~m) | (*refptr & m);
	}

	*dataptr++;
	*refptr++;
  };

  return 0;
};

int Mali_MemCmp(unsigned int *malidata_page,
                unsigned int mali_va,
                unsigned int *refdata,
                unsigned int refoffset,
                unsigned int len) 
{
  int res;
  int i;
  int j;
  int step = 4;

#ifdef MALINOCHECK
  return 0;
#endif

  res = memcmp((void *)((char *)malidata_page + (mali_va % 4096)),
               (void *)((char *)refdata + (refoffset)),
               len * sizeof(unsigned int));

  if (res) {

    printf("Error during check of %x bytes from address PA:%x, VA:%x with reference data at address %x\n",
           len * 4,
           ((unsigned int)malidata_page + (mali_va % 4096)),
           mali_va,
           refdata);

    return -1;
  } else {
    return 0;
  };
};

int Mali_MemCmpMasked(unsigned int *malidata_page,
                      unsigned int mali_va,
                      unsigned int *refdata,
                      unsigned int *refmask,
                      unsigned int refoffset,
                      unsigned int len) 
{
  int res = 0;

  unsigned int *maskptr = (unsigned int *)((char *)refmask + (refoffset));
  unsigned int *refptr  = (unsigned int *)((char *)refdata + (refoffset));
  unsigned int *dataptr = (unsigned int *)((char *)malidata_page + (mali_va % 4096));
  unsigned int len2 = len;
  unsigned int m;

#ifdef MALINOCHECK
  return 0;
#endif

  while (len2--) {
    m   = *maskptr++;
    if ((*dataptr++ & m) != (*refptr++ & m)) {
      res  = 1;
      len2 = 0;
    };
  };

  if (res) {
    printf("Error during check of %x bytes from address PA:%x, VA:%x with reference data at address %x\n",
           len * 4,
           ((unsigned int)malidata_page + (mali_va % 4096)),
           mali_va,
           refdata);
    return -1;
  } else {
    return 0;
  };
};

int Mali_CompareRegs(unsigned int *reference_ptr, 
                      int mali_unit,
                     int mali_core,
                     int lowreg, 
                     int highreg)
{
  unsigned int reference_base = (unsigned int)reference_ptr;
  unsigned int *p;
  int i;
  unsigned int v_mali;
  unsigned int v_testvalue;

#ifdef MALINOCHECK
  return 0;
#endif

  reference_base = reference_base + (lowreg % 0x10);
  p = (unsigned int *)reference_base;

  for (i=0; i < (highreg-lowreg); i += 4) {
    v_mali = Mali_RdReg(mali_unit,mali_core,lowreg+i);
    v_testvalue = *(p + (i/4));
    printf ("Checking Register: %08x is value %08x\n",(mali_unit << 28) + (mali_core << 16) + lowreg + i,v_testvalue);
    if ( v_mali != v_testvalue) {
      printf ("Reg Compare Fail: %08x; value=%08x should be=%08x\n",(mali_unit << 28) + (mali_core << 16) + lowreg + i,v_mali,v_testvalue);
      return -1;
    };
  };
  return 0;
};

void Mali_DisplayReg(int unit,int core, int regnum) {
  int v = Mali_RdReg(unit,core,regnum);
  printf ("Reg: 0x%08x Value: %08x\n",(unit <<28)+(core<<16)+regnum,v);
};

void Mali_CheckReg(int unit,int core, int regnum, int value) {
  int v = Mali_RdReg(unit,core,regnum);
  if(v != value)
    printf ("Reg Compare Fail: 0x%08x Value: %08x should be=%08x\n",(unit <<28)+(core<<16)+regnum,v,value);
  else
    printf ("Reg Compare: 0x%08x Value: %08x\n",(unit <<28)+(core<<16)+regnum,v);
};

/*
 * This function simply prints out a given number of 32-bit words from a given location in memory
 * 
 * It is useful for debugging the GPU's job descriptors and any memory related issues
 */
void Mali_PrintMem (volatile unsigned int * memory_address, unsigned int word_count)
{
    int j;
    
    printf ("Memory readout for 0x%x:\n  ", memory_address); 
    for (j=0;j<word_count;j++)
    {
      if ((j%4)==0 && j != 0)
      {
        printf ("\n  ");
      }
      printf ("word[%02d]=0x%08x ", j, *memory_address); 
      memory_address += 1;
    }
    printf ("\n");

}

void Mali_ReadDescriptor (volatile unsigned int * descriptor_address)
{
  Mali_PrintMem (descriptor_address, 12);
}
#undef printf
