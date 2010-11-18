/****************************
*	Typedefs.h
****************************/
#ifndef	__TYPEDEFS_H__
#define	__TYPEDEFS_H__
#define  STATUS_SUCCESS	0
#define  STATUS_FAILURE -1

#define	 FALSE		0
#define	 TRUE		1

typedef char BOOLEAN;
typedef char CHAR;
typedef int INT;
typedef short SHORT;
typedef long LONG;
typedef void VOID;

typedef unsigned char UCHAR;
typedef unsigned char B_UINT8;
typedef unsigned short USHORT;
typedef unsigned short B_UINT16;
typedef unsigned int UINT;
typedef unsigned int B_UINT32;
typedef unsigned long ULONG;
typedef unsigned long DWORD;

typedef char* PCHAR;
typedef short* PSHORT;
typedef int* PINT;
typedef long* PLONG;
typedef void* PVOID;

typedef unsigned char* PUCHAR;
typedef unsigned short* PUSHORT;
typedef unsigned int* PUINT;
typedef unsigned long* PULONG;
typedef unsigned long long ULONG64;
typedef unsigned long long LARGE_INTEGER;
typedef unsigned int UINT32;
#ifndef NULL
#define NULL 0
#endif


#endif	//__TYPEDEFS_H__

