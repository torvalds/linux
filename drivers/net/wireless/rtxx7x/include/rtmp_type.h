/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#ifndef __RTMP_TYPE_H__
#define __RTMP_TYPE_H__



#ifndef GNU_PACKED
#define GNU_PACKED  __attribute__ ((packed))
#endif /* GNU_PACKED */


#ifdef LINUX
/* Put platform dependent declaration here */
/* For example, linux type definition */
typedef unsigned char UINT8;
typedef unsigned short UINT16;
typedef unsigned int UINT32;
typedef unsigned long long UINT64;
typedef short INT16;
typedef int INT32;
typedef long long INT64;

typedef unsigned char UCHAR;
typedef unsigned short USHORT;
typedef unsigned int UINT;
typedef unsigned long ULONG;
#endif /* LINUX */

typedef unsigned char *PUINT8;
typedef unsigned short *PUINT16;
typedef unsigned int *PUINT32;
typedef unsigned long long *PUINT64;
typedef int *PINT32;
typedef long long *PINT64;

/* modified for fixing compile warning on Sigma 8634 platform */
typedef char STRING;

typedef signed char CHAR;

typedef signed short SHORT;
typedef signed int INT;
typedef signed long LONG;
typedef signed long long LONGLONG;

typedef unsigned long long ULONGLONG;

typedef unsigned char BOOLEAN;
#ifdef LINUX
typedef void VOID;
#endif /* LINUX */

typedef char *PSTRING;
typedef VOID *PVOID;
typedef CHAR *PCHAR;
typedef UCHAR *PUCHAR;
typedef USHORT *PUSHORT;
typedef LONG *PLONG;
typedef ULONG *PULONG;
typedef UINT *PUINT;

typedef unsigned int NDIS_MEDIA_STATE;

typedef union _LARGE_INTEGER {
	struct {
#ifdef RT_BIG_ENDIAN
		INT32 HighPart;
		UINT LowPart;
#else
		UINT LowPart;
		INT32 HighPart;
#endif
	} u;
	INT64 QuadPart;
} LARGE_INTEGER;

/* */
/* Register set pair for initialzation register set definition */
/* */
typedef struct _RTMP_REG_PAIR {
	ULONG Register;
	ULONG Value;
} RTMP_REG_PAIR, *PRTMP_REG_PAIR;

typedef struct _REG_PAIR {
	UCHAR Register;
	UCHAR Value;
} REG_PAIR, *PREG_PAIR;

/* */
/* Register set pair for initialzation register set definition */
/* */
typedef struct _RTMP_RF_REGS {
	UCHAR Channel;
	UINT32 R1;
	UINT32 R2;
	UINT32 R3;
	UINT32 R4;
} RTMP_RF_REGS, *PRTMP_RF_REGS;

typedef struct _FREQUENCY_ITEM {
	UCHAR Channel;
	UCHAR N;
	UCHAR R;
	UCHAR K;
} FREQUENCY_ITEM, *PFREQUENCY_ITEM;

typedef int NTSTATUS;

#define STATUS_SUCCESS			0x00
#define STATUS_UNSUCCESSFUL 		0x01

typedef struct _QUEUE_ENTRY {
	struct _QUEUE_ENTRY *Next;
} QUEUE_ENTRY, *PQUEUE_ENTRY;

/* Queue structure */
typedef struct _QUEUE_HEADER {
	PQUEUE_ENTRY Head;
	PQUEUE_ENTRY Tail;
	ULONG Number;
} QUEUE_HEADER, *PQUEUE_HEADER;

#endif /* __RTMP_TYPE_H__ */
