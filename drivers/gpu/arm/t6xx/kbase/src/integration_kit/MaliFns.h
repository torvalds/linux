/*----------------------------------------------------------------------------
*
*The confidential and proprietary information contained in this file may
*only be used by a person authorised under and to the extent permitted
*by a subsisting licensing agreement from ARM Limited.
*
*        (C) COPYRIGHT 2010-2013 ARM Limited.
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
* Abstract :Declaration of Mali functions 
*-----------------------------------------------------------------------------
* Overview
*----------
* 
**************************************************************/

//#define  t760 1
/*
#ifdef VE
#define  MALI_BASE 0x2D000000
#else
#define  MALI_BASE 0xC0000000
#endif
*/
/****************************************************************************
 * SYSTEM SPECIFIC FUNCTIONS                                                  
 ****************************************************************************/
void CPU_EnableInterrupts(void);
void CPU_DisableInterrupts(void);
void CPU_InitialiseInterrupts(void);

void Mali_WaitForClk(int);
void Mali_WaitForInterrupt(void);
void Mali_InstallIntHandlers(void (*callback_mali_gpu)(void), 
                             void (*callback_mali_mmu)(void), 
                             void (*callback_mali_job)(void));
void Mali_Message(char *s);
void Mali_SetupOutputFrame(void);
void Mali_JobPartDone(void);

/****************************************************************************
 * MALI REGISTER READ / WRITE
 ****************************************************************************/
void Mali_Reset(void);
void *Mali_LdMem(void *srcptr,int size,int ttb_base);
void Mali_WrReg(int unit,int core,int regnum,int value);
int  Mali_RdReg(int unit,int core,int regnum);
unsigned int Mali_PollReg (unsigned int unit, unsigned int core, unsigned int regnum, unsigned int bit_location, unsigned int val, char * name_str);

void Mali_DisplayReg(int unit,int core, int regnum);

void Mali_Identify(void);

int Mali_InterruptCheck(int i_mask, int i_value);
int Mali_AnyInterruptCheck(int type, int i_mask, int i_value);
void Mali_MaskAllInterrupts(void);

void Mali_SetInterruptErrMask(int type, int e_mask);

/****************************************************************************
 * MALI PERFORMANCE COUNTERS
 ****************************************************************************/
void Mali_InitPerfCounters(void);
void Mali_ReadPerfCounters(void);

/****************************************************************************
 * UTILITY FUNCTIONS FOR COPYING DATA INTO MALI MEMORY
 ****************************************************************************/
int Mali_MemCpy(unsigned int *malidata_page,
                unsigned int mali_va,
                unsigned int *refdata,
                unsigned int refoffset,
                unsigned int len);

int Mali_MemCpyMasked(unsigned int *malidata_page,
                      unsigned int mali_va,
                      unsigned int *refdata,
                      unsigned int *refmask,
                      unsigned int refoffset,
                      unsigned int len);

/****************************************************************************
 * UTILITY FUNCTIONS FOR CHECKING TEST RESULTS
 ****************************************************************************/
int Mali_MemCmp(unsigned int *malidata_page,
                unsigned int mali_va,
                unsigned int *refdata,
                unsigned int refoffset,
                unsigned int len);

int Mali_MemCmpMasked(unsigned int *malidata_page,
                      unsigned int mali_va,
                      unsigned int *refdata,
                      unsigned int *refmask,
                      unsigned int refoffset,
                      unsigned int len);

int Mali_CompareRegs(unsigned int *reference_ptr, 
                      int mali_unit,
                     int mali_core,
                     int lowreg, 
                     int highreg);

/****************************************************************************
 * FUNCTIONS THAT ARE USEFUL FOR DEBUG
 ****************************************************************************/
void Mali_PrintMem       (volatile unsigned int * memory_address, unsigned int word_count );
void Mali_ReadDescriptor (volatile unsigned int * descriptor_address                      );


int RunMaliTest_sim1_t760 (int *base);
int RunMaliTest_sim2_t760 (int *base);
int RunMaliTest_sim3_t760 (int *base);
int RunMaliTest_sim4_t760 (int *base);

