
#ifndef _VGATYPES_
#define _VGATYPES_

#include "osdef.h"


#ifdef LINUX_KERNEL  /* We don't want the X driver to depend on kernel source */
#include <linux/ioctl.h>
#endif

#ifndef FALSE
#define FALSE   0
#endif

#ifndef TRUE
#define TRUE    1
#endif

#ifndef NULL
#define NULL    0
#endif

#ifndef CHAR
typedef char CHAR;
#endif

#ifndef SHORT
typedef short SHORT;
#endif

#ifndef LONG
typedef long  LONG;
#endif

#ifndef UCHAR
typedef unsigned char UCHAR;
#endif

#ifndef USHORT
typedef unsigned short USHORT;
#endif

#ifndef ULONG
typedef unsigned long ULONG;
#endif

#ifndef PUCHAR
typedef UCHAR *PUCHAR;
#endif

#ifndef PUSHORT
typedef USHORT *PUSHORT;
#endif

#ifndef PLONGU
typedef ULONG *PULONG;
#endif

#ifndef VOID
typedef void VOID;
#endif

#ifndef PVOID
typedef void *PVOID;
#endif

#ifndef BOOLEAN
typedef UCHAR BOOLEAN;
#endif
/*
#ifndef bool
typedef UCHAR bool;
#endif
*/
#ifdef LINUX_KERNEL
typedef unsigned long XGIIOADDRESS;
#endif


#ifndef VBIOS_VER_MAX_LENGTH
#define VBIOS_VER_MAX_LENGTH    4
#endif

#ifndef LINUX_KERNEL   /* For the linux kernel, this is defined in xgifb.h */
#ifndef XGI_CHIP_TYPE
typedef enum _XGI_CHIP_TYPE {
    XGI_VGALegacy = 0,
    XGI_300,
    XGI_630,
    XGI_640,
    XGI_315H,
    XGI_315,
    XGI_315PRO,
    XGI_550,
    XGI_650,
    XGI_650M,
    XGI_740,
    XGI_330,
    XGI_661,
    XGI_660,
    XGI_760,
    XG40 = 32,
    XG41,
    XG42,
    XG45,
    XG20 = 48,
    XG21,
    XG27,
    MAX_XGI_CHIP
} XGI_CHIP_TYPE;
#endif
#endif

#ifndef XGI_VB_CHIP_TYPE
typedef enum _XGI_VB_CHIP_TYPE {
    VB_CHIP_Legacy = 0,
    VB_CHIP_301,
    VB_CHIP_301B,
    VB_CHIP_301LV,
    VB_CHIP_302,
    VB_CHIP_302B,
    VB_CHIP_302LV,
    VB_CHIP_301C,
    VB_CHIP_302ELV,
    VB_CHIP_UNKNOWN, /* other video bridge or no video bridge */
    MAX_VB_CHIP
} XGI_VB_CHIP_TYPE;
#endif

#ifndef XGI_LCD_TYPE
typedef enum _XGI_LCD_TYPE {
    LCD_INVALID = 0,
    LCD_320x480,       /* FSTN, DSTN */
    LCD_640x480,
    LCD_640x480_2,     /* FSTN, DSTN */
    LCD_640x480_3,     /* FSTN, DSTN */
    LCD_800x600,
    LCD_848x480,
    LCD_1024x600,
    LCD_1024x768,
    LCD_1152x768,
    LCD_1152x864,
    LCD_1280x720,
    LCD_1280x768,
    LCD_1280x800,
    LCD_1280x960,
    LCD_1280x1024,
    LCD_1400x1050,
    LCD_1600x1200,
    LCD_1680x1050,
    LCD_1920x1440,
    LCD_2048x1536,
    LCD_CUSTOM,
    LCD_UNKNOWN
} XGI_LCD_TYPE;
#endif

#ifndef PXGI_DSReg
typedef struct _XGI_DSReg
{
  UCHAR  jIdx;
  UCHAR  jVal;
} XGI_DSReg, *PXGI_DSReg;
#endif

#ifndef XGI_HW_DEVICE_INFO

typedef struct _XGI_HW_DEVICE_INFO  XGI_HW_DEVICE_INFO, *PXGI_HW_DEVICE_INFO;

typedef BOOLEAN (*PXGI_QUERYSPACE)   (PXGI_HW_DEVICE_INFO, ULONG, ULONG, ULONG *);

struct _XGI_HW_DEVICE_INFO
{
    ULONG  ulExternalChip;       /* NO VB or other video bridge*/
                                 /* if ujVBChipID = VB_CHIP_UNKNOWN, */

    PUCHAR  pjVirtualRomBase;    /* ROM image */

    BOOLEAN UseROM;		 /* Use the ROM image if provided */

    PVOID   pDevice;

    PUCHAR  pjVideoMemoryAddress;/* base virtual memory address */
                                 /* of Linear VGA memory */

    ULONG  ulVideoMemorySize;    /* size, in bytes, of the memory on the board */

    PUCHAR pjIOAddress;          /* base I/O address of VGA ports (0x3B0) */

    PUCHAR pjCustomizedROMImage;

    PUCHAR pj2ndVideoMemoryAddress;
    ULONG  ul2ndVideoMemorySize;

    PUCHAR pj2ndIOAddress;
    UCHAR  jChipType;            /* Used to Identify Graphics Chip */
                                 /* defined in the data structure type  */
                                 /* "XGI_CHIP_TYPE" */

    UCHAR  jChipRevision;        /* Used to Identify Graphics Chip Revision */

    UCHAR  ujVBChipID;           /* the ID of video bridge */
                                 /* defined in the data structure type */
                                 /* "XGI_VB_CHIP_TYPE" */

    BOOLEAN    bNewScratch;

    ULONG  ulCRT2LCDType;        /* defined in the data structure type */

    ULONG usExternalChip;       /* NO VB or other video bridge (other than  */
                                 /*  video bridge) */

    BOOLEAN bIntegratedMMEnabled;/* supporting integration MM enable */

    BOOLEAN bSkipDramSizing;     /* True: Skip video memory sizing. */

    BOOLEAN bSkipSense;

    BOOLEAN bIsPowerSaving;     /* True: XGIInit() is invoked by power management,
                                   otherwise by 2nd adapter's initialzation */

    PXGI_DSReg  pSR;             /* restore SR registers in initial function. */
                                 /* end data :(idx, val) =  (FF, FF). */
                                 /* Note : restore SR registers if  */
                                 /* bSkipDramSizing = TRUE */

    PXGI_DSReg  pCR;             /* restore CR registers in initial function. */
                                 /* end data :(idx, val) =  (FF, FF) */
                                 /* Note : restore cR registers if  */
                                 /* bSkipDramSizing = TRUE */
/*
#endif
*/

    PXGI_QUERYSPACE  pQueryVGAConfigSpace;

    PXGI_QUERYSPACE  pQueryNorthBridgeSpace;

    UCHAR  szVBIOSVer[VBIOS_VER_MAX_LENGTH];

};
#endif

/* Addtional IOCTL for communication xgifb <> X driver        */
/* If changing this, xgifb.h must also be changed (for xgifb) */


#endif

