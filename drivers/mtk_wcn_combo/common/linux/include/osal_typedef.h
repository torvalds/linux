/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/


#ifndef _OSAL_TYPEDEF_H_
#define _OSAL_TYPEDEF_H_

typedef void VOID;
typedef void *PVOID;

typedef char CHAR;
typedef char *PCHAR;
typedef signed char INT8;
typedef signed char *PINT8;
typedef unsigned char UINT8;
typedef unsigned char *PUINT8;
typedef unsigned char UCHAR;
typedef unsigned char  *PUCHAR;

typedef signed short INT16;
typedef signed short *PINT16;
typedef unsigned short UINT16;
typedef unsigned short *PUINT16;

typedef signed long LONG;
typedef signed long *PLONG;

typedef signed int INT32;
typedef signed int *PINT32;
typedef unsigned int UINT32;
typedef unsigned int *PUINT32;

typedef unsigned long ULONG;
typedef unsigned long  *PULONG;

typedef int MTK_WCN_BOOL;
#ifndef MTK_WCN_BOOL_TRUE
#define MTK_WCN_BOOL_FALSE               ((MTK_WCN_BOOL) 0)
#define MTK_WCN_BOOL_TRUE                ((MTK_WCN_BOOL) 1)
#endif

#endif /*_OSAL_TYPEDEF_H_*/

