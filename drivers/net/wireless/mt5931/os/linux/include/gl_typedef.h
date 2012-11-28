/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/os/linux/include/gl_typedef.h#1 $
*/

/*! \file   gl_typedef.h
    \brief  Definition of basic data type(os dependent).

    In this file we define the basic data type.
*/



/*
** $Log: gl_typedef.h $
 *
 * 06 22 2012 cp.wu
 * [WCXRP00001257] [MT6620][MT5931][MT6628][Driver][Linux] Modify KAL_HZ to align ms accuracy
 * modify KAL_HZ to (1000) for correct definition.
 *
 * 03 21 2011 cp.wu
 * [WCXRP00000540] [MT5931][Driver] Add eHPI8/eHPI16 support to Linux Glue Layer
 * portability improvement
 *
 * 02 15 2011 jeffrey.chang
 * NULL
 * to support early suspend in android
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 06 2010 kevin.huang
 * [WPD00003832][MT6620 5931] Create driver base 
 * [MT6620 5931] Create driver base
 *
 * 03 24 2010 jeffrey.chang
 * [WPD00003826]Initial import for Linux port 
 * initial import for Linux port
**  \main\maintrunk.MT5921\6 2009-08-18 22:57:14 GMT mtk01090
**  Add Linux SDIO (with mmc core) support.
**  Add Linux 2.6.21, 2.6.25, 2.6.26.
**  Fix compile warning in Linux.
**  \main\maintrunk.MT5921\5 2008-09-22 23:19:30 GMT mtk01461
**  Update comment for code review
**  \main\maintrunk.MT5921\4 2008-09-05 17:25:16 GMT mtk01461
**  Update Driver for Code Review
**  \main\maintrunk.MT5921\3 2007-11-09 11:00:50 GMT mtk01425
**  1. Use macro to unify network-to-host and host-to-network related functions
** Revision 1.3  2007/06/27 02:18:51  MTK01461
** Update SCAN_FSM, Initial(Can Load Module), Proc(Can do Reg R/W), TX API
**
** Revision 1.2  2007/06/25 06:16:24  MTK01461
** Update illustrations, gl_init.c, gl_kal.c, gl_kal.h, gl_os.h and RX API
**
*/

#ifndef _GL_TYPEDEF_H
#define _GL_TYPEDEF_H

#if defined(CONFIG_HAS_EARLYSUSPEND)
#include <linux/earlysuspend.h>
#endif

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
/* Define HZ of timer tick for function kalGetTimeTick() */
#define KAL_HZ                  (1000)

/* Miscellaneous Equates */
#ifndef FALSE
    #define FALSE               ((BOOL) 0)
    #define TRUE                ((BOOL) 1)
#endif /* FALSE */

#ifndef NULL
    #if defined(__cplusplus)
        #define NULL            0
    #else
        #define NULL            ((void *) 0)
    #endif
#endif

#if defined(CONFIG_HAS_EARLYSUSPEND)
typedef void (*early_suspend_callback)(struct early_suspend *h);
typedef void (*late_resume_callback) (struct early_suspend *h);
#endif


/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
/* Type definition for void */
typedef void                    VOID, *PVOID, **PPVOID;

/* Type definition for Boolean */
typedef unsigned char           BOOL, *PBOOL, BOOLEAN, *PBOOLEAN;

/* Type definition for signed integers */
typedef signed char             CHAR, *PCHAR, **PPCHAR;
typedef signed char             INT_8, *PINT_8, **PPINT_8;
typedef signed short            INT_16, *PINT_16, **PPINT_16;
typedef signed long             INT_32, *PINT_32, **PPINT_32;
typedef signed long long        INT_64, *PINT_64, **PPINT_64;

/* Type definition for unsigned integers */
typedef unsigned char           UCHAR, *PUCHAR, **PPUCHAR;
typedef unsigned char           UINT_8, *PUINT_8, **PPUINT_8, *P_UINT_8;
typedef unsigned short          UINT_16, *PUINT_16, **PPUINT_16;
typedef unsigned long           ULONG, UINT_32, *PUINT_32, **PPUINT_32;
typedef unsigned long long      UINT_64, *PUINT_64, **PPUINT_64;

typedef unsigned long           OS_SYSTIME, *POS_SYSTIME, **PPOS_SYSTIME;


/* Type definition of large integer (64bits) union to be comptaible with
 * Windows definition, so we won't apply our own coding style to these data types.
 * NOTE: LARGE_INTEGER must NOT be floating variable.
 * <TODO>: Check for big-endian compatibility.
 */
typedef union _LARGE_INTEGER {
    struct {
        UINT_32  LowPart;
        INT_32   HighPart;
    } u;
    INT_64       QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
    struct {
        UINT_32  LowPart;
        UINT_32  HighPart;
    } u;
    UINT_64      QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;


typedef INT_32 (*probe_card)(PVOID pvData);
typedef VOID (*remove_card)(VOID);

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#define IN  //volatile
#define OUT //volatile

#define __KAL_INLINE__                  static __inline__
#define __KAL_ATTRIB_PACKED__           __attribute__((__packed__))
#define __KAL_ATTRIB_ALIGN_4__          __attribute__ ((aligned (4)))


#ifndef BIT
#define BIT(n)                          ((UINT_32) 1UL << (n))
#endif /* BIT */

#ifndef BITS
/* bits range: for example BITS(16,23) = 0xFF0000
 *   ==>  (BIT(m)-1)   = 0x0000FFFF     ~(BIT(m)-1)   => 0xFFFF0000
 *   ==>  (BIT(n+1)-1) = 0x00FFFFFF
 */
#define BITS(m,n)                       (~(BIT(m)-1) & ((BIT(n) - 1) | BIT(n)))
#endif /* BIT */


/* This macro returns the byte offset of a named field in a known structure
   type.
   _type - structure name,
   _field - field name of the structure */
#ifndef OFFSET_OF
    #define OFFSET_OF(_type, _field)    ((UINT_32)&(((_type *)0)->_field))
#endif /* OFFSET_OF */


/* This macro returns the base address of an instance of a structure
 * given the type of the structure and the address of a field within the
 * containing structure.
 * _addrOfField - address of current field of the structure,
 * _type - structure name,
 * _field - field name of the structure
 */
#ifndef ENTRY_OF
    #define ENTRY_OF(_addrOfField, _type, _field) \
        ((_type *)((PINT_8)(_addrOfField) - (PINT_8)OFFSET_OF(_type, _field)))
#endif /* ENTRY_OF */


/* This macro align the input value to the DW boundary.
 * _value - value need to check
 */
#ifndef ALIGN_4
    #define ALIGN_4(_value)             (((_value) + 3) & ~3u)
#endif /* ALIGN_4 */

/* This macro check the DW alignment of the input value.
 * _value - value of address need to check
 */
#ifndef IS_ALIGN_4
    #define IS_ALIGN_4(_value)          (((_value) & 0x3) ? FALSE : TRUE)
#endif /* IS_ALIGN_4 */

#ifndef IS_NOT_ALIGN_4
    #define IS_NOT_ALIGN_4(_value)      (((_value) & 0x3) ? TRUE : FALSE)
#endif /* IS_NOT_ALIGN_4 */


/* This macro evaluate the input length in unit of Double Word(4 Bytes).
 * _value - value in unit of Byte, output will round up to DW boundary.
 */
#ifndef BYTE_TO_DWORD
    #define BYTE_TO_DWORD(_value)       ((_value + 3) >> 2)
#endif /* BYTE_TO_DWORD */

/* This macro evaluate the input length in unit of Byte.
 * _value - value in unit of DW, output is in unit of Byte.
 */
#ifndef DWORD_TO_BYTE
    #define DWORD_TO_BYTE(_value)       ((_value) << 2)
#endif /* DWORD_TO_BYTE */

#if 1 // Little-Endian
    #define CONST_NTOHS(_x)     __constant_ntohs(_x)

    #define CONST_HTONS(_x)     __constant_htons(_x)

    #define NTOHS(_x)           ntohs(_x)

    #define HTONS(_x)           htons(_x)

    #define NTOHL(_x)           ntohl(_x)

    #define HTONL(_x)           htonl(_x)

#else // Big-Endian

    #define CONST_NTOHS(_x)

    #define CONST_HTONS(_x)

    #define NTOHS(_x)

    #define HTONS(_x)

#endif

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _GL_TYPEDEF_H */

