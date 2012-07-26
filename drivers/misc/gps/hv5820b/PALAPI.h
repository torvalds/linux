///////////////////////////////////////////////////////////////////////////////////
//
// Filename: PALAPI.h
// Author:	sjchen
// Copyright: 
// Date: 2010/12/01
// Description:
//			declare linux driver function 
//
// Revision:
//		0.0.1
//
///////////////////////////////////////////////////////////////////////////////////

#ifndef __PALAPI_H___
#define __PALAPI_H___

extern void* PAL_AllocMem(int nBytes);
extern void  PAL_FreeMem(void* pMem);
extern int   PAL_CreateMutex	( void);
extern void  PAL_ReleaseMutex	( int hMutex );
extern void  PAL_WaitMutex(int hMutex);
extern void  PAL_CloseMutes(int hMutex);

extern void  PAL_memset (
	void*	        pbuf,
	unsigned char	nval,
	int         	nsize
	);

extern void PAL_memcpy (
	void*	pDst,
	void*	pSrc,
	int	nSize
	);

void PAL_GetCurTimer_US(
	  int *n32Second,
	  int *n32us
	);

void PAL_GetSysTimer_US(
						int *n32Second,
						int *n32us
						);

int  PAL_GetKernelFreePage(int nPageSize);

void PAL_FreeKernelPage(int nAddr,int nPageSize);

int  PAL_GetUser(int *p);

void PAL_CopyFromUser(void *pDest,void *pSrc,int nSize);

int  PAL_CopyToUser(void *pDest,void *pSrc,int nSize);

int  PAL_Sprintf (
	   char  * pDst,
	   const char * pFormat,
    	...
	);

void PAL_mdelay(int ms);

void PAL_KDEBUG (
	const char * pFormat,
	...
	);
void PAL_Set_GPIO_Pin(int gpio);
void PAL_Clr_GPIO_Pin(int gpio);

#endif //__PALAPI_H___
