
/*
 *
  Copyright (c) Eicon Technology Corporation, 2000.
 *
  This source file is supplied for the use with Eicon
  Technology Corporation's range of DIVA Server Adapters.
 *
  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.
 *
  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY OF ANY KIND WHATSOEVER INCLUDING ANY
  implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
  See the GNU General Public License for more details.
 *
  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
/*------------------------------------------------------------------*/
/* file: dbgioctl.h                                                 */
/*------------------------------------------------------------------*/

#if !defined(__DBGIOCTL_H__)

#define __DBGIOCTL_H__

#ifdef NOT_YET_NEEDED
/*
 * The requested operation is passed in arg0 of DbgIoctlArgs,
 * additional arguments (if any) in arg1, arg2 and arg3.
 */

typedef struct
{	ULONG	arg0 ;
	ULONG	arg1 ;
	ULONG	arg2 ;
	ULONG	arg3 ;
} DbgIoctlArgs ;

#define	DBG_COPY_LOGS	0	/* copy debugs to user until buffer full	*/
							/* arg1: size threshold						*/
							/* arg2: timeout in milliseconds			*/

#define DBG_FLUSH_LOGS	1	/* flush pending debugs to user buffer		*/
							/* arg1: internal driver id					*/

#define DBG_LIST_DRVS	2	/* return the list of registered drivers	*/

#define	DBG_GET_MASK	3	/* get current debug mask of driver			*/
							/* arg1: internal driver id					*/

#define	DBG_SET_MASK	4	/* set/change debug mask of driver			*/
							/* arg1: internal driver id					*/
							/* arg2: new debug mask						*/

#define	DBG_GET_BUFSIZE	5	/* get current buffer size of driver		*/
							/* arg1: internal driver id					*/
							/* arg2: new debug mask						*/

#define	DBG_SET_BUFSIZE	6	/* set new buffer size of driver			*/
							/* arg1: new buffer size					*/

/*
 *	common internal debug message structure
 */

typedef struct
{	unsigned short id ;		/* virtual driver id                  */
	unsigned short type ;	/* special message type               */
	unsigned long  seq ;	/* sequence number of message         */
	unsigned long  size ;	/* size of message in bytes           */
	unsigned long  next ;	/* offset to next buffered message    */
	LARGE_INTEGER  NTtime ;	/* 100 ns  since 1.1.1601             */
	unsigned char  data[4] ;/* message data                       */
} OldDbgMessage ;

typedef struct
{	LARGE_INTEGER  NTtime ;	/* 100 ns  since 1.1.1601             */
	unsigned short size ;	/* size of message in bytes           */
	unsigned short ffff ;	/* always 0xffff to indicate new msg  */
	unsigned short id ;		/* virtual driver id                  */
	unsigned short type ;	/* special message type               */
	unsigned long  seq ;	/* sequence number of message         */
	unsigned char  data[4] ;/* message data                       */
} DbgMessage ;

#endif

#define DRV_ID_UNKNOWN		0x0C	/* for messages via prtComp() */

#define	MSG_PROC_FLAG		0x80
#define	MSG_PROC_NO_GET(x)	(((x) & MSG_PROC_FLAG) ? (((x) >> 4) & 7) : -1)
#define	MSG_PROC_NO_SET(x)	(MSG_PROC_FLAG | (((x) & 7) << 4))

#define MSG_TYPE_DRV_ID		0x0001
#define MSG_TYPE_FLAGS		0x0002
#define MSG_TYPE_STRING		0x0003
#define MSG_TYPE_BINARY		0x0004

#define MSG_HEAD_SIZE	((unsigned long)&(((DbgMessage *)0)->data[0]))
#define MSG_ALIGN(len)	(((unsigned long)(len) + MSG_HEAD_SIZE + 3) & ~3)
#define MSG_SIZE(pMsg)	MSG_ALIGN((pMsg)->size)
#define MSG_NEXT(pMsg)	((DbgMessage *)( ((char *)(pMsg)) + MSG_SIZE(pMsg) ))

#define OLD_MSG_HEAD_SIZE	((unsigned long)&(((OldDbgMessage *)0)->data[0]))
#define OLD_MSG_ALIGN(len)	(((unsigned long)(len)+OLD_MSG_HEAD_SIZE+3) & ~3)

/*
 * manifest constants
 */

#define MSG_FRAME_MAX_SIZE	2150		/* maximum size of B1 frame	 */
#define MSG_TEXT_MAX_SIZE	1024		/* maximum size of msg text	 */
#define MSG_MAX_SIZE		MSG_ALIGN(MSG_FRAME_MAX_SIZE)
#define DBG_MIN_BUFFER_SIZE	0x00008000	/* minimal total buffer size  32 KB */
#define DBG_DEF_BUFFER_SIZE	0x00020000	/* default total buffer size 128 KB */
#define DBG_MAX_BUFFER_SIZE	0x00400000	/* maximal total buffer size   4 MB */

#define DBGDRV_NAME		"Diehl_DIMAINT"
#define UNIDBG_DRIVER	L"\\Device\\Diehl_DIMAINT" /* UNICODE name for kernel */
#define DEBUG_DRIVER	"\\\\.\\" DBGDRV_NAME  /* traditional string for apps */
#define DBGVXD_NAME		"DIMAINT"
#define DEBUG_VXD		"\\\\.\\" DBGVXD_NAME  /* traditional string for apps */

/*
 *	Special IDI interface debug construction
 */

#define	DBG_IDI_SIG_REQ		(unsigned long)0xF479C402
#define	DBG_IDI_SIG_IND		(unsigned long)0xF479C403
#define	DBG_IDI_NL_REQ		(unsigned long)0xF479C404
#define	DBG_IDI_NL_IND		(unsigned long)0xF479C405

typedef struct
{	unsigned long  magic_type ;
	unsigned short data_len ;
	unsigned char  layer_ID ;
	unsigned char  entity_ID ;
	unsigned char  request ;
	unsigned char  ret_code ;
	unsigned char  indication ;
	unsigned char  complete ;
	unsigned char  data[4] ;
} DbgIdiAct, *DbgIdiAction ;

/*
 * We want to use the same IOCTL codes in Win95 and WinNT.
 * The official constructor for IOCTL codes is the CTL_CODE macro
 * from <winoctl.h> (<devioctl.h> in WinNT DDK environment).
 * The problem here is that we don't know how to get <winioctl.h>
 * working in a Win95 DDK environment!
 */

# ifdef CTL_CODE	/*{*/

/* Assert that we have the same idea of the CTL_CODE macro.	*/

#define CTL_CODE( DeviceType, Function, Method, Access ) (                 \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)

# else	/* !CTL_CODE */ /*}{*/

/* Use the definitions stolen from <winioctl.h>.  */

#define CTL_CODE( DeviceType, Function, Method, Access ) (                 \
    ((DeviceType) << 16) | ((Access) << 14) | ((Function) << 2) | (Method) \
)

#define METHOD_BUFFERED                 0
#define METHOD_IN_DIRECT                1
#define METHOD_OUT_DIRECT               2
#define METHOD_NEITHER                  3

#define FILE_ANY_ACCESS                 0
#define FILE_READ_ACCESS          ( 0x0001 )    // file & pipe
#define FILE_WRITE_ACCESS         ( 0x0002 )    // file & pipe

# endif	/* CTL_CODE */ /*}*/

/*
 * Now we can define WinNT/Win95 DeviceIoControl codes.
 *
 * These codes are defined in di_defs.h too, a possible mismatch will be
 * detected when the dbgtool is compiled.
 */

#define IOCTL_DRIVER_LNK \
	CTL_CODE(0x8001U,0x701,METHOD_OUT_DIRECT,FILE_ANY_ACCESS)
#define IOCTL_DRIVER_DBG \
	CTL_CODE(0x8001U,0x702,METHOD_OUT_DIRECT,FILE_ANY_ACCESS)

#endif /* __DBGIOCTL_H__ */
