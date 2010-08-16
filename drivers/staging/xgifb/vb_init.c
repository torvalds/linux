#include "osdef.h"
#include "vgatypes.h"


#ifdef LINUX_KERNEL
#include <linux/version.h>
#include <linux/types.h>
#include <linux/delay.h> /* udelay */
#include "XGIfb.h"
/*#if LINUX_VERSxION_CODE >= KERNEL_VERSION(2,5,0)
#include <video/XGIfb.h>
#else
#include <linux/XGIfb.h>
#endif */
#endif

#ifdef WIN2000
#include <dderror.h>
#include <devioctl.h>
#include <miniport.h>
#include <ntddvdeo.h>
#include <video.h>
#include "xgiv.h"
#include "dd_i2c.h"
#include "tools.h"
#endif

#include "vb_def.h"
#include "vb_struct.h"
#include "vb_util.h"
#include "vb_setmode.h"
#include "vb_init.h"
#include "vb_ext.h"

#ifdef LINUX_XF86
#include "xf86.h"
#include "xf86PciInfo.h"
#include "xgi.h"
#include "xgi_regs.h"
#endif

#ifdef LINUX_KERNEL
#include <asm/io.h>
#include <linux/types.h>
#endif




UCHAR    XGINew_ChannelAB,XGINew_DataBusWidth;

USHORT XGINew_DRAMType[17][5]={{0x0C,0x0A,0x02,0x40,0x39},{0x0D,0x0A,0x01,0x40,0x48},
                     {0x0C,0x09,0x02,0x20,0x35},{0x0D,0x09,0x01,0x20,0x44},
                     {0x0C,0x08,0x02,0x10,0x31},{0x0D,0x08,0x01,0x10,0x40},
                     {0x0C,0x0A,0x01,0x20,0x34},{0x0C,0x09,0x01,0x08,0x32},
                     {0x0B,0x08,0x02,0x08,0x21},{0x0C,0x08,0x01,0x08,0x30},
                     {0x0A,0x08,0x02,0x04,0x11},{0x0B,0x0A,0x01,0x10,0x28},
                     {0x09,0x08,0x02,0x02,0x01},{0x0B,0x09,0x01,0x08,0x24},
                     {0x0B,0x08,0x01,0x04,0x20},{0x0A,0x08,0x01,0x02,0x10},
                     {0x09,0x08,0x01,0x01,0x00}};

USHORT XGINew_SDRDRAM_TYPE[13][5]=
{
{ 2,12, 9,64,0x35},
{ 1,13, 9,64,0x44},
{ 2,12, 8,32,0x31},
{ 2,11, 9,32,0x25},
{ 1,12, 9,32,0x34},
{ 1,13, 8,32,0x40},
{ 2,11, 8,16,0x21},
{ 1,12, 8,16,0x30},
{ 1,11, 9,16,0x24},
{ 1,11, 8, 8,0x20},
{ 2, 9, 8, 4,0x01},
{ 1,10, 8, 4,0x10},
{ 1, 9, 8, 2,0x00}
};

USHORT XGINew_DDRDRAM_TYPE[4][5]=
{
{ 2,12, 9,64,0x35},
{ 2,12, 8,32,0x31},
{ 2,11, 8,16,0x21},
{ 2, 9, 8, 4,0x01}
};
USHORT XGINew_DDRDRAM_TYPE340[4][5]=
{
{ 2,13, 9,64,0x45},
{ 2,12, 9,32,0x35},
{ 2,12, 8,16,0x31},
{ 2,11, 8, 8,0x21}
};
USHORT XGINew_DDRDRAM_TYPE20[12][5]=
{
{ 2,14,11,128,0x5D},
{ 2,14,10,64,0x59},
{ 2,13,11,64,0x4D},
{ 2,14, 9,32,0x55},
{ 2,13,10,32,0x49},
{ 2,12,11,32,0x3D},
{ 2,14, 8,16,0x51},
{ 2,13, 9,16,0x45},
{ 2,12,10,16,0x39},
{ 2,13, 8, 8,0x41},
{ 2,12, 9, 8,0x35},
{ 2,12, 8, 4,0x31}
};

void     XGINew_SetDRAMSize_340(PXGI_HW_DEVICE_INFO, PVB_DEVICE_INFO);
void     XGINew_SetDRAMSize_310(PXGI_HW_DEVICE_INFO, PVB_DEVICE_INFO);
void     XGINew_SetMemoryClock(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO );
void     XGINew_SetDRAMModeRegister(PVB_DEVICE_INFO );
void     XGINew_SetDRAMModeRegister340( PXGI_HW_DEVICE_INFO HwDeviceExtension );
void 	 XGINew_SetDRAMDefaultRegister340(PXGI_HW_DEVICE_INFO HwDeviceExtension, ULONG, PVB_DEVICE_INFO );
UCHAR    XGINew_GetXG20DRAMType( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo);
BOOLEAN  XGIInitNew( PXGI_HW_DEVICE_INFO HwDeviceExtension) ;

int      XGINew_DDRSizing340( PXGI_HW_DEVICE_INFO, PVB_DEVICE_INFO );
void     XGINew_DisableRefresh( PXGI_HW_DEVICE_INFO ,PVB_DEVICE_INFO) ;
void     XGINew_CheckBusWidth_310( PVB_DEVICE_INFO) ;
int      XGINew_SDRSizing(PVB_DEVICE_INFO);
int      XGINew_DDRSizing( PVB_DEVICE_INFO );
void     XGINew_EnableRefresh( PXGI_HW_DEVICE_INFO, PVB_DEVICE_INFO);
int      XGINew_RAMType;                  /*int      ModeIDOffset,StandTable,CRT1Table,ScreenOffset,REFIndex;*/
ULONG	 UNIROM;			  /* UNIROM */
BOOLEAN  ChkLFB( PVB_DEVICE_INFO );
void     XGINew_Delay15us(ULONG);
void     SetPowerConsume (PXGI_HW_DEVICE_INFO HwDeviceExtension,ULONG XGI_P3d4Port);
void 	 ReadVBIOSTablData( UCHAR ChipType , PVB_DEVICE_INFO pVBInfo);
void 	 XGINew_DDR1x_MRS_XG20( ULONG P3c4 , PVB_DEVICE_INFO pVBInfo);
void     XGINew_SetDRAMModeRegister_XG20( PXGI_HW_DEVICE_INFO HwDeviceExtension );
void     XGINew_SetDRAMModeRegister_XG27( PXGI_HW_DEVICE_INFO HwDeviceExtension );
void 	 XGINew_ChkSenseStatus ( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo ) ;
void     XGINew_SetModeScratch ( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo ) ;
void     XGINew_GetXG21Sense(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO pVBInfo) ;
UCHAR    GetXG21FPBits(PVB_DEVICE_INFO pVBInfo);
void     XGINew_GetXG27Sense(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO pVBInfo) ;
UCHAR    GetXG27FPBits(PVB_DEVICE_INFO pVBInfo);

#ifdef WIN2000
/* [Billy] 2007/05/20 For CH7007 */
extern  UCHAR CH7007TVReg_UNTSC[][8],CH7007TVReg_ONTSC[][8],CH7007TVReg_UPAL[][8],CH7007TVReg_OPAL[][8];
extern  UCHAR XGI7007_CHTVVCLKUNTSC[],XGI7007_CHTVVCLKONTSC[],XGI7007_CHTVVCLKUPAL[],XGI7007_CHTVVCLKOPAL[];
#endif

#ifdef LINUX_KERNEL
void DelayUS(ULONG MicroSeconds)
{
	udelay(MicroSeconds);
}
#endif

/* --------------------------------------------------------------------- */
/* Function : XGIInitNew */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
BOOLEAN XGIInitNew( PXGI_HW_DEVICE_INFO HwDeviceExtension )
{

    VB_DEVICE_INFO VBINF;
    PVB_DEVICE_INFO pVBInfo = &VBINF;
    UCHAR   i , temp = 0 , temp1 ;
     //       VBIOSVersion[ 5 ] ;
    PUCHAR  volatile pVideoMemory;

    /* ULONG j, k ; */

    PXGI_DSReg pSR ;

    ULONG Temp ;

    pVBInfo->ROMAddr = HwDeviceExtension->pjVirtualRomBase ;

    pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress ;

    pVBInfo->BaseAddr = (ULONG)HwDeviceExtension->pjIOAddress ;

    pVideoMemory = ( PUCHAR )pVBInfo->ROMAddr;


//    Newdebugcode( 0x99 ) ;


   /* if ( pVBInfo->ROMAddr == 0 ) */
   /* return( FALSE ) ; */

    if ( pVBInfo->FBAddr == 0 )
{
       printk("\n pVBInfo->FBAddr == 0 ");
       return( FALSE ) ;
}
printk("1");
    if ( pVBInfo->BaseAddr == 0 )
{
       printk("\npVBInfo->BaseAddr == 0 ");
        return( FALSE ) ;
}
printk("2");

    XGINew_SetReg3( ( pVBInfo->BaseAddr + 0x12 ) , 0x67 ) ;	/* 3c2 <- 67 ,ynlai */

    pVBInfo->ISXPDOS = 0 ;
printk("3");

if ( !HwDeviceExtension->bIntegratedMMEnabled )
{
        return( FALSE ) ;	/* alan */
}
printk("4");

//    XGI_MemoryCopy( VBIOSVersion , HwDeviceExtension->szVBIOSVer , 4 ) ;

 //   VBIOSVersion[ 4 ] = 0x0 ;

    /* 09/07/99 modify by domao */

    pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14 ;
    pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24 ;
    pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10 ;
    pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e ;
    pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12 ;
    pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a ;
    pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16 ;
    pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17 ;
    pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18 ;
    pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19 ;
    pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A ;
    pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00 ;
    pVBInfo->Part1Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_04 ;
    pVBInfo->Part2Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_10 ;
    pVBInfo->Part3Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_12 ;
    pVBInfo->Part4Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 ;
    pVBInfo->Part5Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 + 2 ;
printk("5");

    if ( HwDeviceExtension->jChipType < XG20 )			/* kuku 2004/06/25 */
    XGI_GetVBType( pVBInfo ) ;         /* Run XGI_GetVBType before InitTo330Pointer */

    InitTo330Pointer( HwDeviceExtension->jChipType,  pVBInfo ) ;

    /* ReadVBIOSData */
    ReadVBIOSTablData( HwDeviceExtension->jChipType , pVBInfo) ;

    /* 1.Openkey */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x05 , 0x86 ) ;
printk("6");

    /* GetXG21Sense (GPIO) */
    if ( HwDeviceExtension->jChipType == XG21 )
    {
    	XGINew_GetXG21Sense(HwDeviceExtension, pVBInfo) ;
    }
    if ( HwDeviceExtension->jChipType == XG27 )
    {
    	XGINew_GetXG27Sense(HwDeviceExtension, pVBInfo) ;
    }
printk("7");

    /* 2.Reset Extended register */

    for( i = 0x06 ; i < 0x20 ; i++ )
        XGINew_SetReg1( pVBInfo->P3c4 , i , 0 ) ;

    for( i = 0x21 ; i <= 0x27 ; i++ )
        XGINew_SetReg1( pVBInfo->P3c4 , i , 0 ) ;

    /* for( i = 0x06 ; i <= 0x27 ; i++ ) */
    /* XGINew_SetReg1( pVBInfo->P3c4 , i , 0 ) ; */

printk("8");

    if(( HwDeviceExtension->jChipType >= XG20 ) || ( HwDeviceExtension->jChipType >= XG40))
    {
        for( i = 0x31 ; i <= 0x3B ; i++ )
            XGINew_SetReg1( pVBInfo->P3c4 , i , 0 ) ;
    }
    else
    {
        for( i = 0x31 ; i <= 0x3D ; i++ )
            XGINew_SetReg1( pVBInfo->P3c4 , i , 0 ) ;
    }
printk("9");

    if ( HwDeviceExtension->jChipType == XG42 )			/* [Hsuan] 2004/08/20 Auto over driver for XG42 */
      XGINew_SetReg1( pVBInfo->P3c4 , 0x3B , 0xC0 ) ;

    /* for( i = 0x30 ; i <= 0x3F ; i++ ) */
    /* XGINew_SetReg1( pVBInfo->P3d4 , i , 0 ) ; */

    for( i = 0x79 ; i <= 0x7C ; i++ )
        XGINew_SetReg1( pVBInfo->P3d4 , i , 0 ) ;		/* shampoo 0208 */

printk("10");

    if ( HwDeviceExtension->jChipType >= XG20 )
        XGINew_SetReg1( pVBInfo->P3d4 , 0x97 , *pVBInfo->pXGINew_CR97 ) ;

    /* 3.SetMemoryClock

    if ( HwDeviceExtension->jChipType >= XG40 )
        XGINew_RAMType = ( int )XGINew_GetXG20DRAMType( HwDeviceExtension , pVBInfo) ;

    if ( HwDeviceExtension->jChipType < XG40 )
        XGINew_SetMemoryClock( HwDeviceExtension , pVBInfo ) ;  */

printk("11");

    /* 4.SetDefExt1Regs begin */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x07 , *pVBInfo->pSR07 ) ;
    if ( HwDeviceExtension->jChipType == XG27 )
    {
        XGINew_SetReg1( pVBInfo->P3c4 , 0x40 , *pVBInfo->pSR40 ) ;
        XGINew_SetReg1( pVBInfo->P3c4 , 0x41 , *pVBInfo->pSR41 ) ;
    }
    XGINew_SetReg1( pVBInfo->P3c4 , 0x11 , 0x0F ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x1F , *pVBInfo->pSR1F ) ;
    /* XGINew_SetReg1( pVBInfo->P3c4 , 0x20 , 0x20 ) ; */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x20 , 0xA0 ) ;	/* alan, 2001/6/26 Frame buffer can read/write SR20 */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x36 , 0x70 ) ;	/* Hsuan, 2006/01/01 H/W request for slow corner chip */
    if ( HwDeviceExtension->jChipType == XG27 )         /* Alan 12/07/2006 */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x36 , *pVBInfo->pSR36 ) ;

    /* SR11 = 0x0F ; */
    /* XGINew_SetReg1( pVBInfo->P3c4 , 0x11 , SR11 ) ; */

printk("12");

   if ( HwDeviceExtension->jChipType < XG20 )		/* kuku 2004/06/25 */
    {
//    /* Set AGP Rate */
//    temp1 = XGINew_GetReg1( pVBInfo->P3c4 , 0x3B ) ;
//    temp1 &= 0x02 ;
//    if ( temp1 == 0x02 )
//    {
//        XGINew_SetReg4( 0xcf8 , 0x80000000 ) ;
//       ChipsetID = XGINew_GetReg3( 0x0cfc ) ;
//        XGINew_SetReg4( 0xcf8 , 0x8000002C ) ;
//        VendorID = XGINew_GetReg3( 0x0cfc ) ;
//        VendorID &= 0x0000FFFF ;
//        XGINew_SetReg4( 0xcf8 , 0x8001002C ) ;
//        GraphicVendorID = XGINew_GetReg3( 0x0cfc ) ;
//        GraphicVendorID &= 0x0000FFFF;
//
//        if ( ChipsetID == 0x7301039 )
///            XGINew_SetReg1( pVBInfo->P3d4 , 0x5F , 0x09 ) ;
//
//        ChipsetID &= 0x0000FFFF ;
///
//        if ( ( ChipsetID == 0x700E ) || ( ChipsetID == 0x1022 ) || ( ChipsetID == 0x1106 ) || ( ChipsetID == 0x10DE ) )
//        {
//            if ( ChipsetID == 0x1106 )
//            {
//                if ( ( VendorID == 0x1019 ) && ( GraphicVendorID == 0x1019 ) )
//                    XGINew_SetReg1( pVBInfo->P3d4 , 0x5F , 0x0D ) ;
//                else
//                    XGINew_SetReg1( pVBInfo->P3d4 , 0x5F , 0x0B ) ;
//            }
//            else
//                XGINew_SetReg1( pVBInfo->P3d4 , 0x5F , 0x0B ) ;
//        }
//    }

printk("13");

    if ( HwDeviceExtension->jChipType >= XG40 )
    {
        /* Set AGP customize registers (in SetDefAGPRegs) Start */
        for( i = 0x47 ; i <= 0x4C ; i++ )
            XGINew_SetReg1( pVBInfo->P3d4 , i , pVBInfo->AGPReg[ i - 0x47 ] ) ;

        for( i = 0x70 ; i <= 0x71 ; i++ )
            XGINew_SetReg1( pVBInfo->P3d4 , i , pVBInfo->AGPReg[ 6 + i - 0x70 ] ) ;

        for( i = 0x74 ; i <= 0x77 ; i++ )
            XGINew_SetReg1( pVBInfo->P3d4 , i , pVBInfo->AGPReg[ 8 + i - 0x74 ] ) ;
        /* Set AGP customize registers (in SetDefAGPRegs) End */
        /*[Hsuan]2004/12/14 AGP Input Delay Adjustment on 850 */
//        XGINew_SetReg4( 0xcf8 , 0x80000000 ) ;
//        ChipsetID = XGINew_GetReg3( 0x0cfc ) ;
//        if ( ChipsetID == 0x25308086 )
//            XGINew_SetReg1( pVBInfo->P3d4 , 0x77 , 0xF0 ) ;

        HwDeviceExtension->pQueryVGAConfigSpace( HwDeviceExtension , 0x50 , 0 , &Temp ) ;	/* Get */
        Temp >>= 20 ;
        Temp &= 0xF ;

        if ( Temp == 1 )
            XGINew_SetReg1( pVBInfo->P3d4 , 0x48 , 0x20 ) ;	/* CR48 */
    }
printk("14");

    if ( HwDeviceExtension->jChipType < XG40 )
        XGINew_SetReg1( pVBInfo->P3d4 , 0x49 , pVBInfo->CR49[ 0 ] ) ;
    }	/* != XG20 */

    /* Set PCI */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x23 , *pVBInfo->pSR23 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x24 , *pVBInfo->pSR24 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x25 , pVBInfo->SR25[ 0 ] ) ;
printk("15");

    if ( HwDeviceExtension->jChipType < XG20 )		/* kuku 2004/06/25 */
    {
    /* Set VB */
    XGI_UnLockCRT2( HwDeviceExtension, pVBInfo) ;
    XGINew_SetRegANDOR( pVBInfo->Part0Port , 0x3F , 0xEF , 0x00 ) ;	/* alan, disable VideoCapture */
    XGINew_SetReg1( pVBInfo->Part1Port , 0x00 , 0x00 ) ;
    temp1 = ( UCHAR )XGINew_GetReg1( pVBInfo->P3d4 , 0x7B ) ;		/* chk if BCLK>=100MHz */
    temp = ( UCHAR )( ( temp1 >> 4 ) & 0x0F ) ;


        XGINew_SetReg1( pVBInfo->Part1Port , 0x02 , ( *pVBInfo->pCRT2Data_1_2 ) ) ;

printk("16");

    XGINew_SetReg1( pVBInfo->Part1Port , 0x2E , 0x08 ) ;	/* use VB */
    } /* != XG20 */


    XGINew_SetReg1( pVBInfo->P3c4 , 0x27 , 0x1F ) ;

    if ( ( HwDeviceExtension->jChipType == XG42 ) && XGINew_GetXG20DRAMType( HwDeviceExtension , pVBInfo) != 0 )	/* Not DDR */
    {
        XGINew_SetReg1( pVBInfo->P3c4 , 0x31 , ( *pVBInfo->pSR31 & 0x3F ) | 0x40 ) ;
        XGINew_SetReg1( pVBInfo->P3c4 , 0x32 , ( *pVBInfo->pSR32 & 0xFC ) | 0x01 ) ;
    }
    else
    {
        XGINew_SetReg1( pVBInfo->P3c4 , 0x31 , *pVBInfo->pSR31 ) ;
        XGINew_SetReg1( pVBInfo->P3c4 , 0x32 , *pVBInfo->pSR32 ) ;
    }
    XGINew_SetReg1( pVBInfo->P3c4 , 0x33 , *pVBInfo->pSR33 ) ;
printk("17");

/*
    if ( HwDeviceExtension->jChipType >= XG40 )
      SetPowerConsume ( HwDeviceExtension , pVBInfo->P3c4);	*/

    if ( HwDeviceExtension->jChipType < XG20 )		/* kuku 2004/06/25 */
    {
    if ( XGI_BridgeIsOn( pVBInfo ) == 1 )
    {
        if ( pVBInfo->IF_DEF_LVDS == 0 )
        {
            XGINew_SetReg1( pVBInfo->Part2Port , 0x00 , 0x1C ) ;
            XGINew_SetReg1( pVBInfo->Part4Port , 0x0D , *pVBInfo->pCRT2Data_4_D ) ;
            XGINew_SetReg1( pVBInfo->Part4Port , 0x0E , *pVBInfo->pCRT2Data_4_E ) ;
            XGINew_SetReg1( pVBInfo->Part4Port , 0x10 , *pVBInfo->pCRT2Data_4_10 ) ;
            XGINew_SetReg1( pVBInfo->Part4Port , 0x0F , 0x3F ) ;
        }

        XGI_LockCRT2( HwDeviceExtension, pVBInfo ) ;
    }
    }	/* != XG20 */
printk("18");

    if ( HwDeviceExtension->jChipType < XG40 )
        XGINew_SetReg1( pVBInfo->P3d4 , 0x83 , 0x00 ) ;
printk("181");

    if ( HwDeviceExtension->bSkipSense == FALSE )
    {
printk("182");

        XGI_SenseCRT1(pVBInfo) ;

printk("183");
        /* XGINew_DetectMonitor( HwDeviceExtension ) ; */
pVBInfo->IF_DEF_CH7007 = 0;
        if ( ( HwDeviceExtension->jChipType == XG21 ) && (pVBInfo->IF_DEF_CH7007) )
        {
printk("184");
           XGI_GetSenseStatus( HwDeviceExtension , pVBInfo ) ; 	/* sense CRT2 */
printk("185");

        }
        if ( HwDeviceExtension->jChipType == XG21 )
        {
printk("186");

          XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x32 , ~Monitor1Sense , Monitor1Sense ) ;	/* Z9 default has CRT */
       	  temp = GetXG21FPBits( pVBInfo ) ;
          XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x37 , ~0x01, temp ) ;
printk("187");

          }
        if ( HwDeviceExtension->jChipType == XG27 )
        {
          XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x32 , ~Monitor1Sense , Monitor1Sense ) ;	/* Z9 default has CRT */
       	  temp = GetXG27FPBits( pVBInfo ) ;
          XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x37 , ~0x03, temp ) ;
        }
    }
printk("19");

    if ( HwDeviceExtension->jChipType >= XG40 )
    {
        if ( HwDeviceExtension->jChipType >= XG40 )
        {
          XGINew_RAMType = ( int )XGINew_GetXG20DRAMType( HwDeviceExtension , pVBInfo ) ;
         }

        XGINew_SetDRAMDefaultRegister340( HwDeviceExtension ,  pVBInfo->P3d4,  pVBInfo ) ;

        if ( HwDeviceExtension->bSkipDramSizing == TRUE )
        {
            pSR = HwDeviceExtension->pSR ;
            if ( pSR!=NULL )
            {
                while( pSR->jIdx != 0xFF )
                {
                    XGINew_SetReg1( pVBInfo->P3c4 , pSR->jIdx , pSR->jVal ) ;
                    pSR++ ;
                }
            }
            /* XGINew_SetDRAMModeRegister340( pVBInfo ) ; */
        }   	/* SkipDramSizing */
        else
        {
#if 0
           if ( HwDeviceExtension->jChipType == XG20 )
            {
            	XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , pVBInfo->SR15[0][XGINew_RAMType] ) ;
                XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , pVBInfo->SR15[1][XGINew_RAMType] ) ;
                XGINew_SetReg1( pVBInfo->P3c4 , 0x20 , 0x20 ) ;
            }
            else
#endif
{
printk("20");

               XGINew_SetDRAMSize_340( HwDeviceExtension , pVBInfo) ;
}
printk("21");

        }
    }		/* XG40 */

printk("22");


    /* SetDefExt2Regs begin */
/*
    AGP = 1 ;
    temp =( UCHAR )XGINew_GetReg1( pVBInfo->P3c4 , 0x3A ) ;
    temp &= 0x30 ;
    if ( temp == 0x30 )
        AGP = 0 ;

    if ( AGP == 0 )
        *pVBInfo->pSR21 &= 0xEF ;

    XGINew_SetReg1( pVBInfo->P3c4 , 0x21 , *pVBInfo->pSR21 ) ;
    if ( AGP == 1 )
        *pVBInfo->pSR22 &= 0x20 ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x22 , *pVBInfo->pSR22 ) ;
*/

//    base = 0x80000000 ;
//    OutPortLong( 0xcf8 , base ) ;
//    Temp = ( InPortLong( 0xcfc ) & 0xFFFF ) ;
//    if ( Temp == 0x1039 )
//    {
        XGINew_SetReg1( pVBInfo->P3c4 , 0x22 , ( UCHAR )( ( *pVBInfo->pSR22 ) & 0xFE ) ) ;
//    }
//    else
//    {
//        XGINew_SetReg1( pVBInfo->P3c4 , 0x22 , *pVBInfo->pSR22 ) ;
//    }

    XGINew_SetReg1( pVBInfo->P3c4 , 0x21 , *pVBInfo->pSR21 ) ;

printk("23");


    XGINew_ChkSenseStatus ( HwDeviceExtension , pVBInfo ) ;
    XGINew_SetModeScratch ( HwDeviceExtension , pVBInfo ) ;

printk("24");


XGINew_SetReg1( pVBInfo->P3d4 , 0x8c , 0x87);
XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x31);
printk("25");

    return( TRUE ) ;
} /* end of init */





/* ============== alan ====================== */

/* --------------------------------------------------------------------- */
/* Function : XGINew_GetXG20DRAMType */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
UCHAR XGINew_GetXG20DRAMType( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo)
{
    UCHAR data, temp ;

    if ( HwDeviceExtension->jChipType < XG20 )
    {
        if ( *pVBInfo->pSoftSetting & SoftDRAMType )
        {
            data = *pVBInfo->pSoftSetting & 0x07 ;
            return( data ) ;
        }
        else
        {
            data = XGINew_GetReg1( pVBInfo->P3c4 , 0x39 ) & 0x02 ;

            if ( data == 0 )
                data = ( XGINew_GetReg1( pVBInfo->P3c4 , 0x3A ) & 0x02 ) >> 1 ;

            return( data ) ;
        }
    }
    else if ( HwDeviceExtension->jChipType == XG27 )
    {
        if ( *pVBInfo->pSoftSetting & SoftDRAMType )
        {
            data = *pVBInfo->pSoftSetting & 0x07 ;
            return( data ) ;
        }
        temp = XGINew_GetReg1( pVBInfo->P3c4 , 0x3B ) ;

     	if (( temp & 0x88 )==0x80)		/* SR3B[7][3]MAA15 MAA11 (Power on Trapping) */
       	  data = 0 ;					/*DDR*/
        else
       	  data = 1 ; 					/*DDRII*/
       	return( data ) ;
    }
    else if ( HwDeviceExtension->jChipType == XG21 )
    {
        XGINew_SetRegAND( pVBInfo->P3d4 , 0xB4 , ~0x02 ) ;     		/* Independent GPIO control */
     	DelayUS(800);
        XGINew_SetRegOR( pVBInfo->P3d4 , 0x4A , 0x80 ) ;		/* Enable GPIOH read */
        temp = XGINew_GetReg1( pVBInfo->P3d4 , 0x48 ) ;       		/* GPIOF 0:DVI 1:DVO */
// HOTPLUG_SUPPORT
// for current XG20 & XG21, GPIOH is floating, driver will fix DDR temporarily
     	if ( temp & 0x01 )						/* DVI read GPIOH */
       	  data = 1 ;							/*DDRII*/
        else
       	  data = 0 ; 							/*DDR*/
//~HOTPLUG_SUPPORT
       	XGINew_SetRegOR( pVBInfo->P3d4 , 0xB4 , 0x02 ) ;
       	return( data ) ;
    }
    else
    {
    	data = XGINew_GetReg1( pVBInfo->P3d4 , 0x97 ) & 0x01 ;

    	if ( data == 1 )
            data ++ ;

    	return( data );
    }
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_Get310DRAMType */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
UCHAR XGINew_Get310DRAMType(PVB_DEVICE_INFO pVBInfo)
{
    UCHAR data ;

  /* index = XGINew_GetReg1( pVBInfo->P3c4 , 0x1A ) ; */
  /* index &= 07 ; */

    if ( *pVBInfo->pSoftSetting & SoftDRAMType )
        data = *pVBInfo->pSoftSetting & 0x03 ;
    else
        data = XGINew_GetReg1( pVBInfo->P3c4 , 0x3a ) & 0x03 ;

    return( data ) ;
}



/* --------------------------------------------------------------------- */
/* Function : XGINew_Delay15us */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
/*
void XGINew_Delay15us(ULONG ulMicrsoSec)
{
}
*/


/* --------------------------------------------------------------------- */
/* Function : XGINew_SDR_MRS */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SDR_MRS(  PVB_DEVICE_INFO pVBInfo )
{
    USHORT data ;

    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x16 ) ;
    data &= 0x3F ;          /* SR16 D7=0,D6=0 */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;   /* enable mode register set(MRS) low */
    /* XGINew_Delay15us( 0x100 ) ; */
    data |= 0x80 ;          /* SR16 D7=1,D6=0 */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;   /* enable mode register set(MRS) high */
    /* XGINew_Delay15us( 0x100 ) ; */
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR1x_MRS_340 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR1x_MRS_340( ULONG P3c4 , PVB_DEVICE_INFO pVBInfo)
{
    XGINew_SetReg1( P3c4 , 0x18 , 0x01 ) ;
    XGINew_SetReg1( P3c4 , 0x19 , 0x20 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;

    if ( *pVBInfo->pXGINew_DRAMTypeDefinition != 0x0C )	/* Samsung F Die */
    {
        DelayUS( 3000 ) ;	/* Delay 67 x 3 Delay15us */
        XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;
        XGINew_SetReg1( P3c4 , 0x19 , 0x20 ) ;
        XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
        XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;
    }

    DelayUS( 60 ) ;
    XGINew_SetReg1( P3c4 , 0x18 , pVBInfo->SR15[ 2 ][ XGINew_RAMType ] ) ;	/* SR18 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x01 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , pVBInfo->SR16[ 0 ] ) ;
    XGINew_SetReg1( P3c4 , 0x16 , pVBInfo->SR16[ 1 ] ) ;
    DelayUS( 1000 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x03 ) ;
    DelayUS( 500 ) ;
    XGINew_SetReg1( P3c4 , 0x18 , pVBInfo->SR15[ 2 ][ XGINew_RAMType ] ) ;	/* SR18 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , pVBInfo->SR16[ 2 ] ) ;
    XGINew_SetReg1( P3c4 , 0x16 , pVBInfo->SR16[ 3 ] ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x00 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR2x_MRS_340 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR2x_MRS_340( ULONG P3c4 , PVB_DEVICE_INFO pVBInfo)
{
    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x19 , 0x20 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;
    DelayUS( 60 ) ;
    XGINew_SetReg1( P3c4 , 0x18 , pVBInfo->SR15[ 2 ][ XGINew_RAMType ] ) ;	/* SR18 */
    /* XGINew_SetReg1( P3c4 , 0x18 , 0x31 ) ; */
    XGINew_SetReg1( P3c4 , 0x19 , 0x01 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x05 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x85 ) ;
    DelayUS( 1000 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x03 ) ;
    DelayUS( 500 ) ;
    /* XGINew_SetReg1( P3c4 , 0x18 , 0x31 ) ; */
    XGINew_SetReg1( P3c4 , 0x18 , pVBInfo->SR15[ 2 ][ XGINew_RAMType ] ) ;	/* SR18 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x05 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x85 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x00 ) ;
}

/* --------------------------------------------------------------------- */
/* Function : XGINew_DDRII_Bootup_XG27 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDRII_Bootup_XG27( PXGI_HW_DEVICE_INFO HwDeviceExtension ,  ULONG P3c4 , PVB_DEVICE_INFO pVBInfo)
{
    ULONG P3d4 = P3c4 + 0x10 ;
    XGINew_RAMType = ( int )XGINew_GetXG20DRAMType( HwDeviceExtension , pVBInfo ) ;
    XGINew_SetMemoryClock( HwDeviceExtension , pVBInfo ) ;

   /* Set Double Frequency */
    /* XGINew_SetReg1( P3d4 , 0x97 , 0x11 ) ; */		/* CR97 */
    XGINew_SetReg1( P3d4 , 0x97 , *pVBInfo->pXGINew_CR97 ) ;    /* CR97 */

    DelayUS( 200 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;   /* Set SR18 */ //EMRS2
    XGINew_SetReg1( P3c4 , 0x19 , 0x80 ) ;   /* Set SR19 */
    XGINew_SetReg1( P3c4 , 0x16 , 0x20 ) ;   /* Set SR16 */
    DelayUS( 15 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0xA0 ) ;   /* Set SR16 */
    DelayUS( 15 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;   /* Set SR18 */ //EMRS3
    XGINew_SetReg1( P3c4 , 0x19 , 0xC0 ) ;   /* Set SR19 */
    XGINew_SetReg1( P3c4 , 0x16 , 0x20 ) ;   /* Set SR16 */
    DelayUS( 15 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0xA0 ) ;   /* Set SR16 */
    DelayUS( 15) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;   /* Set SR18 */ //EMRS1
    XGINew_SetReg1( P3c4 , 0x19 , 0x40 ) ;   /* Set SR19 */
    XGINew_SetReg1( P3c4 , 0x16 , 0x20 ) ;   /* Set SR16 */
    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0xA0 ) ;   /* Set SR16 */
    DelayUS( 15 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x42 ) ;   /* Set SR18 */ //MRS, DLL Enable
    XGINew_SetReg1( P3c4 , 0x19 , 0x0A ) ;   /* Set SR19 */
    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;   /* Set SR16 */
    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;   /* Set SR16 */
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;   /* Set SR16 */
    /* DelayUS( 15 ) ; */

    XGINew_SetReg1( P3c4 , 0x1B , 0x04 ) ;   /* Set SR1B */
    DelayUS( 60 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x00 ) ;   /* Set SR1B */

    XGINew_SetReg1( P3c4 , 0x18 , 0x42 ) ;   /* Set SR18 */ //MRS, DLL Reset
    XGINew_SetReg1( P3c4 , 0x19 , 0x08 ) ;   /* Set SR19 */
    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;   /* Set SR16 */

    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x83 ) ;   /* Set SR16 */
    DelayUS( 15 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x80 ) ;   /* Set SR18 */ //MRS, ODT
    XGINew_SetReg1( P3c4 , 0x19 , 0x46 ) ;   /* Set SR19 */
    XGINew_SetReg1( P3c4 , 0x16 , 0x20 ) ;   /* Set SR16 */
    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0xA0 ) ;   /* Set SR16 */
    DelayUS( 15 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;   /* Set SR18 */ //EMRS
    XGINew_SetReg1( P3c4 , 0x19 , 0x40 ) ;   /* Set SR19 */
    XGINew_SetReg1( P3c4 , 0x16 , 0x20 ) ;   /* Set SR16 */
    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0xA0 ) ;   /* Set SR16 */
    DelayUS( 15 ) ;

    XGINew_SetReg1( P3c4 , 0x1B , 0x04 ) ;   /* Set SR1B refresh control 000:close; 010:open */
    DelayUS( 200 ) ;


}
/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR2_MRS_XG20 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR2_MRS_XG20( PXGI_HW_DEVICE_INFO HwDeviceExtension ,  ULONG P3c4 , PVB_DEVICE_INFO pVBInfo)
{
    ULONG P3d4 = P3c4 + 0x10 ;

    XGINew_RAMType = ( int )XGINew_GetXG20DRAMType( HwDeviceExtension , pVBInfo ) ;
    XGINew_SetMemoryClock( HwDeviceExtension , pVBInfo ) ;

    XGINew_SetReg1( P3d4 , 0x97 , 0x11 ) ;			/* CR97 */

    DelayUS( 200 ) ;
    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;			/* EMRS2 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x80 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x05 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x85 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;			/* EMRS3 */
    XGINew_SetReg1( P3c4 , 0x19 , 0xC0 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x05 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x85 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;			/* EMRS1 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x40 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x05 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x85 ) ;

   // XGINew_SetReg1( P3c4 , 0x18 , 0x52 ) ;			/* MRS1 */
    XGINew_SetReg1( P3c4 , 0x18 , 0x42 ) ;			/* MRS1 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x02 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x05 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x85 ) ;

    DelayUS( 15 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x04 ) ;			/* SR1B */
    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x00 ) ;			/* SR1B */
    DelayUS( 100 ) ;

    //XGINew_SetReg1( P3c4 , 0x18 , 0x52 ) ;			/* MRS2 */
    XGINew_SetReg1( P3c4 , 0x18 , 0x42 ) ;			/* MRS1 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x05 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x85 ) ;

    DelayUS( 200 ) ;
}

/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR2_MRS_XG20 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR2_MRS_XG27( PXGI_HW_DEVICE_INFO HwDeviceExtension ,  ULONG P3c4 , PVB_DEVICE_INFO pVBInfo)
{
    ULONG P3d4 = P3c4 + 0x10 ;

     XGINew_RAMType = ( int )XGINew_GetXG20DRAMType( HwDeviceExtension , pVBInfo ) ;
     XGINew_SetMemoryClock( HwDeviceExtension , pVBInfo ) ;

    XGINew_SetReg1( P3d4 , 0x97 , 0x11 ) ;			/* CR97 */
    DelayUS( 200 ) ;
    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;			/* EMRS2 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x80 ) ;

    XGINew_SetReg1( P3c4 , 0x16 , 0x10 ) ;
    DelayUS( 15 ) ;                          ////06/11/23 XG27 A0 for CKE enable
    XGINew_SetReg1( P3c4 , 0x16 , 0x90 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;			/* EMRS3 */
    XGINew_SetReg1( P3c4 , 0x19 , 0xC0 ) ;

    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    DelayUS( 15 ) ;                          ////06/11/22 XG27 A0
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;


    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;			/* EMRS1 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x40 ) ;

    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    DelayUS( 15 ) ;                          ////06/11/22 XG27 A0
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x42 ) ;			/* MRS1 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x06 ) ;   ////[Billy]06/11/22 DLL Reset for XG27 Hynix DRAM

    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    DelayUS( 15 ) ;                          ////06/11/23 XG27 A0
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;

    DelayUS( 30 ) ;                          ////06/11/23 XG27 A0 Start Auto-PreCharge
    XGINew_SetReg1( P3c4 , 0x1B , 0x04 ) ;			/* SR1B */
    DelayUS( 60 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x00 ) ;			/* SR1B */


    XGINew_SetReg1( P3c4 , 0x18 , 0x42 ) ;			/* MRS1 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x04 ) ;   //// DLL without Reset for XG27 Hynix DRAM

    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x80 );     ////XG27 OCD ON
    XGINew_SetReg1( P3c4 , 0x19 , 0x46 );

    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 );
    XGINew_SetReg1( P3c4 , 0x19 , 0x40 );

    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    DelayUS( 30 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;

    DelayUS( 15 ) ;                         ////Start Auto-PreCharge
    XGINew_SetReg1( P3c4 , 0x1B , 0x04 ) ;			/* SR1B */
    DelayUS( 200 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x03 ) ;			/* SR1B */

}

/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR1x_DefaultRegister */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR1x_DefaultRegister( PXGI_HW_DEVICE_INFO HwDeviceExtension ,  ULONG Port , PVB_DEVICE_INFO pVBInfo)
{
    ULONG P3d4 = Port ,
           P3c4 = Port - 0x10 ;

    if ( HwDeviceExtension->jChipType >= XG20 )
    {
        XGINew_SetMemoryClock( HwDeviceExtension , pVBInfo ) ;
        XGINew_SetReg1( P3d4 , 0x82 , pVBInfo->CR40[ 11 ][ XGINew_RAMType ] ) ;	/* CR82 */
        XGINew_SetReg1( P3d4 , 0x85 , pVBInfo->CR40[ 12 ][ XGINew_RAMType ] ) ;	/* CR85 */
        XGINew_SetReg1( P3d4 , 0x86 , pVBInfo->CR40[ 13 ][ XGINew_RAMType ] ) ;	/* CR86 */

        XGINew_SetReg1( P3d4 , 0x98 , 0x01 ) ;
        XGINew_SetReg1( P3d4 , 0x9A , 0x02 ) ;

        XGINew_DDR1x_MRS_XG20( P3c4 , pVBInfo) ;
    }
    else
    {
        XGINew_SetMemoryClock( HwDeviceExtension , pVBInfo ) ;

        switch( HwDeviceExtension->jChipType )
        {
            case XG41:
            case XG42:
                XGINew_SetReg1( P3d4 , 0x82 , pVBInfo->CR40[ 11 ][ XGINew_RAMType ] ) ;	/* CR82 */
                XGINew_SetReg1( P3d4 , 0x85 , pVBInfo->CR40[ 12 ][ XGINew_RAMType ] ) ;	/* CR85 */
                XGINew_SetReg1( P3d4 , 0x86 , pVBInfo->CR40[ 13 ][ XGINew_RAMType ] ) ;	/* CR86 */
                break ;
            default:
                XGINew_SetReg1( P3d4 , 0x82 , 0x88 ) ;
                XGINew_SetReg1( P3d4 , 0x86 , 0x00 ) ;
                XGINew_GetReg1( P3d4 , 0x86 ) ;				/* Insert read command for delay */
                XGINew_SetReg1( P3d4 , 0x86 , 0x88 ) ;
                XGINew_GetReg1( P3d4 , 0x86 ) ;
                XGINew_SetReg1( P3d4 , 0x86 , pVBInfo->CR40[ 13 ][ XGINew_RAMType ] ) ;
                XGINew_SetReg1( P3d4 , 0x82 , 0x77 ) ;
                XGINew_SetReg1( P3d4 , 0x85 , 0x00 ) ;
                XGINew_GetReg1( P3d4 , 0x85 ) ;				/* Insert read command for delay */
                XGINew_SetReg1( P3d4 , 0x85 , 0x88 ) ;
                XGINew_GetReg1( P3d4 , 0x85 ) ;				/* Insert read command for delay */
                XGINew_SetReg1( P3d4 , 0x85 , pVBInfo->CR40[ 12 ][ XGINew_RAMType ] ) ;	/* CR85 */
                XGINew_SetReg1( P3d4 , 0x82 , pVBInfo->CR40[ 11 ][ XGINew_RAMType ] ) ;	/* CR82 */
                break ;
        }

        XGINew_SetReg1( P3d4 , 0x97 , 0x00 ) ;
        XGINew_SetReg1( P3d4 , 0x98 , 0x01 ) ;
        XGINew_SetReg1( P3d4 , 0x9A , 0x02 ) ;
        XGINew_DDR1x_MRS_340( P3c4 , pVBInfo ) ;
    }
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR2x_DefaultRegister */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR2x_DefaultRegister( PXGI_HW_DEVICE_INFO HwDeviceExtension ,  ULONG Port ,PVB_DEVICE_INFO pVBInfo)
{
    ULONG P3d4 = Port ,
           P3c4 = Port - 0x10 ;

    XGINew_SetMemoryClock( HwDeviceExtension , pVBInfo ) ;

    /* 20040906 Hsuan modify CR82, CR85, CR86 for XG42 */
    switch( HwDeviceExtension->jChipType )
    {
       case XG41:
       case XG42:
            XGINew_SetReg1( P3d4 , 0x82 , pVBInfo->CR40[ 11 ][ XGINew_RAMType ] ) ;	/* CR82 */
            XGINew_SetReg1( P3d4 , 0x85 , pVBInfo->CR40[ 12 ][ XGINew_RAMType ] ) ;	/* CR85 */
            XGINew_SetReg1( P3d4 , 0x86 , pVBInfo->CR40[ 13 ][ XGINew_RAMType ] ) ;	/* CR86 */
            break ;
       default:
         /* keep following setting sequence, each setting in the same reg insert idle */
         XGINew_SetReg1( P3d4 , 0x82 , 0x88 ) ;
    	 XGINew_SetReg1( P3d4 , 0x86 , 0x00 ) ;
    	 XGINew_GetReg1( P3d4 , 0x86 ) ;				/* Insert read command for delay */
    	 XGINew_SetReg1( P3d4 , 0x86 , 0x88 ) ;
    	 XGINew_SetReg1( P3d4 , 0x82 , 0x77 ) ;
    	 XGINew_SetReg1( P3d4 , 0x85 , 0x00 ) ;
    	 XGINew_GetReg1( P3d4 , 0x85 ) ;				/* Insert read command for delay */
    	 XGINew_SetReg1( P3d4 , 0x85 , 0x88 ) ;
    	 XGINew_GetReg1( P3d4 , 0x85 ) ;				/* Insert read command for delay */
    	 XGINew_SetReg1( P3d4 , 0x85 , pVBInfo->CR40[ 12 ][ XGINew_RAMType ] ) ;	/* CR85 */
    	 XGINew_SetReg1( P3d4 , 0x82 , pVBInfo->CR40[ 11 ][ XGINew_RAMType ] ) ;	/* CR82 */
    }
    XGINew_SetReg1( P3d4 , 0x97 , 0x11 ) ;
    if ( HwDeviceExtension->jChipType == XG42 )
    {
      XGINew_SetReg1( P3d4 , 0x98 , 0x01 ) ;
    }
    else
    {
      XGINew_SetReg1( P3d4 , 0x98 , 0x03 ) ;
    }
    XGINew_SetReg1( P3d4 , 0x9A , 0x02 ) ;

    XGINew_DDR2x_MRS_340( P3c4 , pVBInfo ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR2_DefaultRegister */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR2_DefaultRegister( PXGI_HW_DEVICE_INFO HwDeviceExtension, ULONG Port , PVB_DEVICE_INFO pVBInfo)
{
    ULONG P3d4 = Port ,
           P3c4 = Port - 0x10 ;

    /* keep following setting sequence, each setting in the same reg insert idle */
    XGINew_SetReg1( P3d4 , 0x82 , 0x77 ) ;
    XGINew_SetReg1( P3d4 , 0x86 , 0x00 ) ;
    XGINew_GetReg1( P3d4 , 0x86 ) ;				/* Insert read command for delay */
    XGINew_SetReg1( P3d4 , 0x86 , 0x88 ) ;
    XGINew_GetReg1( P3d4 , 0x86 ) ;				/* Insert read command for delay */
    XGINew_SetReg1( P3d4 , 0x86 , pVBInfo->CR40[ 13 ][ XGINew_RAMType ] ) ;	/* CR86 */
    XGINew_SetReg1( P3d4 , 0x82 , 0x77 ) ;
    XGINew_SetReg1( P3d4 , 0x85 , 0x00 ) ;
    XGINew_GetReg1( P3d4 , 0x85 ) ;				/* Insert read command for delay */
    XGINew_SetReg1( P3d4 , 0x85 , 0x88 ) ;
    XGINew_GetReg1( P3d4 , 0x85 ) ;				/* Insert read command for delay */
    XGINew_SetReg1( P3d4 , 0x85 , pVBInfo->CR40[ 12 ][ XGINew_RAMType ] ) ;	/* CR85 */
    if ( HwDeviceExtension->jChipType == XG27 )
      XGINew_SetReg1( P3d4 , 0x82 , pVBInfo->CR40[ 11 ][ XGINew_RAMType ] ) ;	/* CR82 */
    else
    XGINew_SetReg1( P3d4 , 0x82 , 0xA8 ) ;	/* CR82 */

    XGINew_SetReg1( P3d4 , 0x98 , 0x01 ) ;
    XGINew_SetReg1( P3d4 , 0x9A , 0x02 ) ;
    if ( HwDeviceExtension->jChipType == XG27 )
       XGINew_DDRII_Bootup_XG27( HwDeviceExtension ,  P3c4 , pVBInfo) ;
    else
    XGINew_DDR2_MRS_XG20( HwDeviceExtension , P3c4, pVBInfo ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDRAMDefaultRegister340 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetDRAMDefaultRegister340( PXGI_HW_DEVICE_INFO HwDeviceExtension ,  ULONG Port , PVB_DEVICE_INFO pVBInfo)
{
    UCHAR temp , temp1 , temp2 , temp3 ,
          i , j , k ;

    ULONG P3d4 = Port ,
           P3c4 = Port - 0x10 ;

    XGINew_SetReg1( P3d4 , 0x6D , pVBInfo->CR40[ 8 ][ XGINew_RAMType ] ) ;
    XGINew_SetReg1( P3d4 , 0x68 , pVBInfo->CR40[ 5 ][ XGINew_RAMType ] ) ;
    XGINew_SetReg1( P3d4 , 0x69 , pVBInfo->CR40[ 6 ][ XGINew_RAMType ] ) ;
    XGINew_SetReg1( P3d4 , 0x6A , pVBInfo->CR40[ 7 ][ XGINew_RAMType ] ) ;

    temp2 = 0 ;
    for( i = 0 ; i < 4 ; i++ )
    {
        temp = pVBInfo->CR6B[ XGINew_RAMType ][ i ] ;        		/* CR6B DQS fine tune delay */
        for( j = 0 ; j < 4 ; j++ )
        {
            temp1 = ( ( temp >> ( 2 * j ) ) & 0x03 ) << 2 ;
            temp2 |= temp1 ;
            XGINew_SetReg1( P3d4 , 0x6B , temp2 ) ;
            XGINew_GetReg1( P3d4 , 0x6B ) ;				/* Insert read command for delay */
            temp2 &= 0xF0 ;
            temp2 += 0x10 ;
        }
    }

    temp2 = 0 ;
    for( i = 0 ; i < 4 ; i++ )
    {
        temp = pVBInfo->CR6E[ XGINew_RAMType ][ i ] ;        		/* CR6E DQM fine tune delay */
        for( j = 0 ; j < 4 ; j++ )
        {
            temp1 = ( ( temp >> ( 2 * j ) ) & 0x03 ) << 2 ;
            temp2 |= temp1 ;
            XGINew_SetReg1( P3d4 , 0x6E , temp2 ) ;
            XGINew_GetReg1( P3d4 , 0x6E ) ;				/* Insert read command for delay */
            temp2 &= 0xF0 ;
            temp2 += 0x10 ;
        }
    }

    temp3 = 0 ;
    for( k = 0 ; k < 4 ; k++ )
    {
        XGINew_SetRegANDOR( P3d4 , 0x6E , 0xFC , temp3 ) ;		/* CR6E_D[1:0] select channel */
        temp2 = 0 ;
        for( i = 0 ; i < 8 ; i++ )
        {
            temp = pVBInfo->CR6F[ XGINew_RAMType ][ 8 * k + i ] ;   	/* CR6F DQ fine tune delay */
            for( j = 0 ; j < 4 ; j++ )
            {
                temp1 = ( temp >> ( 2 * j ) ) & 0x03 ;
                temp2 |= temp1 ;
                XGINew_SetReg1( P3d4 , 0x6F , temp2 ) ;
                XGINew_GetReg1( P3d4 , 0x6F ) ;				/* Insert read command for delay */
                temp2 &= 0xF8 ;
                temp2 += 0x08 ;
            }
        }
        temp3 += 0x01 ;
    }

    XGINew_SetReg1( P3d4 , 0x80 , pVBInfo->CR40[ 9 ][ XGINew_RAMType ] ) ;	/* CR80 */
    XGINew_SetReg1( P3d4 , 0x81 , pVBInfo->CR40[ 10 ][ XGINew_RAMType ] ) ;	/* CR81 */

    temp2 = 0x80 ;
    temp = pVBInfo->CR89[ XGINew_RAMType ][ 0 ] ;        		/* CR89 terminator type select */
    for( j = 0 ; j < 4 ; j++ )
    {
        temp1 = ( temp >> ( 2 * j ) ) & 0x03 ;
        temp2 |= temp1 ;
        XGINew_SetReg1( P3d4 , 0x89 , temp2 ) ;
        XGINew_GetReg1( P3d4 , 0x89 ) ;				/* Insert read command for delay */
        temp2 &= 0xF0 ;
        temp2 += 0x10 ;
    }

    temp = pVBInfo->CR89[ XGINew_RAMType ][ 1 ] ;
    temp1 = temp & 0x03 ;
    temp2 |= temp1 ;
    XGINew_SetReg1( P3d4 , 0x89 , temp2 ) ;

    temp = pVBInfo->CR40[ 3 ][ XGINew_RAMType ] ;
    temp1 = temp & 0x0F ;
    temp2 = ( temp >> 4 ) & 0x07 ;
    temp3 = temp & 0x80 ;
    XGINew_SetReg1( P3d4 , 0x45 , temp1 ) ;	/* CR45 */
    XGINew_SetReg1( P3d4 , 0x99 , temp2 ) ;	/* CR99 */
    XGINew_SetRegOR( P3d4 , 0x40 , temp3 ) ;	/* CR40_D[7] */
    XGINew_SetReg1( P3d4 , 0x41 , pVBInfo->CR40[ 0 ][ XGINew_RAMType ] ) ;	/* CR41 */

    if ( HwDeviceExtension->jChipType == XG27 )
      XGINew_SetReg1( P3d4 , 0x8F , *pVBInfo->pCR8F ) ;	/* CR8F */

    for( j = 0 ; j <= 6 ; j++ )
        XGINew_SetReg1( P3d4 , ( 0x90 + j ) , pVBInfo->CR40[ 14 + j ][ XGINew_RAMType ] ) ;	/* CR90 - CR96 */

    for( j = 0 ; j <= 2 ; j++ )
        XGINew_SetReg1( P3d4 , ( 0xC3 + j ) , pVBInfo->CR40[ 21 + j ][ XGINew_RAMType ] ) ;	/* CRC3 - CRC5 */

    for( j = 0 ; j < 2 ; j++ )
        XGINew_SetReg1( P3d4 , ( 0x8A + j ) , pVBInfo->CR40[ 1 + j ][ XGINew_RAMType ] ) ;	/* CR8A - CR8B */

    if ( ( HwDeviceExtension->jChipType == XG41 ) || ( HwDeviceExtension->jChipType == XG42 ) )
        XGINew_SetReg1( P3d4 , 0x8C , 0x87 ) ;

    XGINew_SetReg1( P3d4 , 0x59 , pVBInfo->CR40[ 4 ][ XGINew_RAMType ] ) ;	/* CR59 */

    XGINew_SetReg1( P3d4 , 0x83 , 0x09 ) ;	/* CR83 */
    XGINew_SetReg1( P3d4 , 0x87 , 0x00 ) ;	/* CR87 */
    XGINew_SetReg1( P3d4 , 0xCF , *pVBInfo->pCRCF ) ;	/* CRCF */
    if ( XGINew_RAMType )
    {
      //XGINew_SetReg1( P3c4 , 0x17 , 0xC0 ) ;		/* SR17 DDRII */
      XGINew_SetReg1( P3c4 , 0x17 , 0x80 ) ;		/* SR17 DDRII */
      if ( HwDeviceExtension->jChipType == XG27 )
        XGINew_SetReg1( P3c4 , 0x17 , 0x02 ) ;		/* SR17 DDRII */

    }
    else
      XGINew_SetReg1( P3c4 , 0x17 , 0x00 ) ;		/* SR17 DDR */
    XGINew_SetReg1( P3c4 , 0x1A , 0x87 ) ;		/* SR1A */

    temp = XGINew_GetXG20DRAMType( HwDeviceExtension, pVBInfo) ;
    if( temp == 0 )
      XGINew_DDR1x_DefaultRegister( HwDeviceExtension, P3d4, pVBInfo ) ;
    else
    {
      XGINew_SetReg1( P3d4 , 0xB0 , 0x80 ) ;		/* DDRII Dual frequency mode */
      XGINew_DDR2_DefaultRegister( HwDeviceExtension, P3d4, pVBInfo ) ;
    }
    XGINew_SetReg1( P3c4 , 0x1B , pVBInfo->SR15[ 3 ][ XGINew_RAMType ] ) ;	/* SR1B */
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR_MRS */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR_MRS(PVB_DEVICE_INFO pVBInfo)
{
    USHORT data ;

    PUCHAR volatile pVideoMemory = ( PUCHAR )pVBInfo->ROMAddr ;

    /* SR16 <- 1F,DF,2F,AF */
    /* yriver modified SR16 <- 0F,DF,0F,AF */
    /* enable DLL of DDR SD/SGRAM , SR16 D4=1 */
    data = pVideoMemory[ 0xFB ] ;
    /* data = XGINew_GetReg1( pVBInfo->P3c4 , 0x16 ) ; */

    data &= 0x0F ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;
    data |= 0xC0 ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;
    data &= 0x0F ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;
    data |= 0x80 ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;
    data &= 0x0F ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;
    data |= 0xD0 ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;
    data &= 0x0F ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;
    data |= 0xA0 ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , data ) ;
/*
   else {
     data &= 0x0F;
     data |= 0x10;
     XGINew_SetReg1(pVBInfo->P3c4,0x16,data);

     if (!(pVBInfo->SR15[1][XGINew_RAMType] & 0x10))
     {
       data &= 0x0F;
     }

     data |= 0xC0;
     XGINew_SetReg1(pVBInfo->P3c4,0x16,data);


     data &= 0x0F;
     data |= 0x20;
     XGINew_SetReg1(pVBInfo->P3c4,0x16,data);
     if (!(pVBInfo->SR15[1][XGINew_RAMType] & 0x10))
     {
       data &= 0x0F;
     }

     data |= 0x80;
     XGINew_SetReg1(pVBInfo->P3c4,0x16,data);
   }
*/
}


/* check if read cache pointer is correct */



/* --------------------------------------------------------------------- */
/* Function : XGINew_VerifyMclk */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_VerifyMclk( PXGI_HW_DEVICE_INFO  HwDeviceExtension , PVB_DEVICE_INFO pVBInfo)
{
    PUCHAR pVideoMemory = pVBInfo->FBAddr ;
    UCHAR i , j ;
    USHORT Temp , SR21 ;

    pVideoMemory[ 0 ] = 0xaa ; 		/* alan */
    pVideoMemory[ 16 ] = 0x55 ; 	/* note: PCI read cache is off */

    if ( ( pVideoMemory[ 0 ] != 0xaa ) || ( pVideoMemory[ 16 ] != 0x55 ) )
    {
        for( i = 0 , j = 16 ; i < 2 ; i++ , j += 16 )
        {
            SR21 = XGINew_GetReg1( pVBInfo->P3c4 , 0x21 ) ;
            Temp = SR21 & 0xFB ;	/* disable PCI post write buffer empty gating */
            XGINew_SetReg1( pVBInfo->P3c4 , 0x21 , Temp ) ;

            Temp = XGINew_GetReg1( pVBInfo->P3c4 , 0x3C ) ;
            Temp |= 0x01 ;		/* MCLK reset */


            Temp = XGINew_GetReg1( pVBInfo->P3c4 , 0x3C ) ;
            Temp &= 0xFE ;		/* MCLK normal operation */

            XGINew_SetReg1( pVBInfo->P3c4 , 0x21 , SR21 ) ;

            pVideoMemory[ 16 + j ] = j ;
            if ( pVideoMemory[ 16 + j ] == j )
            {
                pVideoMemory[ j ] = j ;
                break ;
            }
        }
    }
}





/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDRAMSize_340 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetDRAMSize_340( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo)
{
    USHORT  data ;

    pVBInfo->ROMAddr = HwDeviceExtension->pjVirtualRomBase ;
    pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress ;

    XGISetModeNew( HwDeviceExtension , 0x2e ) ;


    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x21 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x21 , ( USHORT )( data & 0xDF ) ) ;	/* disable read cache */
    XGI_DisplayOff( HwDeviceExtension, pVBInfo );

    /*data = XGINew_GetReg1( pVBInfo->P3c4 , 0x1 ) ;*/
    /*data |= 0x20 ;*/
    /*XGINew_SetReg1( pVBInfo->P3c4 , 0x01 , data ) ;*/			/* Turn OFF Display */
    XGINew_DDRSizing340( HwDeviceExtension, pVBInfo ) ;
    data=XGINew_GetReg1( pVBInfo->P3c4 , 0x21 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x21 , ( USHORT )( data | 0x20 ) ) ;	/* enable read cache */

}


/* --------------------------------------------------------------------- */
/* Function : */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetDRAMSize_310( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo)
{
    USHORT data ;
    pVBInfo->ROMAddr  = HwDeviceExtension->pjVirtualRomBase ,
    pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress ;
#ifdef XGI301
    /* XGINew_SetReg1( pVBInfo->P3d4 , 0x30 , 0x40 ) ; */
#endif

#ifdef XGI302	/* alan,should change value */
    XGINew_SetReg1( pVBInfo->P3d4 , 0x30 , 0x4D ) ;
    XGINew_SetReg1( pVBInfo->P3d4 , 0x31 , 0xc0 ) ;
    XGINew_SetReg1( pVBInfo->P3d4 , 0x34 , 0x3F ) ;
#endif

    XGISetModeNew( HwDeviceExtension , 0x2e ) ;

    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x21 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x21 , ( USHORT )( data & 0xDF ) ) ;	/* disable read cache */

    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x1 ) ;
    data |= 0x20 ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x01 , data ) ;		/* Turn OFF Display */

    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x16 ) ;


    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , ( USHORT )( data | 0x0F ) ) ;		/* assume lowest speed DRAM */

    XGINew_SetDRAMModeRegister( pVBInfo ) ;
    XGINew_DisableRefresh( HwDeviceExtension, pVBInfo ) ;
    XGINew_CheckBusWidth_310( pVBInfo) ;
    XGINew_VerifyMclk( HwDeviceExtension, pVBInfo ) ;	/* alan 2000/7/3 */



    if ( XGINew_Get310DRAMType( pVBInfo ) < 2 )
    {
        XGINew_SDRSizing( pVBInfo ) ;
    }
    else
    {
        XGINew_DDRSizing( pVBInfo) ;
    }




    XGINew_SetReg1( pVBInfo->P3c4 , 0x16 , pVBInfo->SR15[ 1 ][ XGINew_RAMType ] ) ;	/* restore SR16 */

    XGINew_EnableRefresh(  HwDeviceExtension, pVBInfo ) ;
    data=XGINew_GetReg1( pVBInfo->P3c4 ,0x21 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x21 , ( USHORT )( data | 0x20 ) ) ;	/* enable read cache */
}



/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDRAMModeRegister340 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */

void XGINew_SetDRAMModeRegister340( PXGI_HW_DEVICE_INFO HwDeviceExtension )
{
    UCHAR data ;
    VB_DEVICE_INFO VBINF;
    PVB_DEVICE_INFO pVBInfo = &VBINF;
    pVBInfo->ROMAddr = HwDeviceExtension->pjVirtualRomBase ;
    pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress ;
    pVBInfo->BaseAddr = (ULONG)HwDeviceExtension->pjIOAddress ;
    pVBInfo->ISXPDOS = 0 ;

    pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14 ;
    pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24 ;
    pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10 ;
    pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e ;
    pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12 ;
    pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a ;
    pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16 ;
    pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17 ;
    pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18 ;
    pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19 ;
    pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A ;
    pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00 ;
    pVBInfo->Part1Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_04 ;
    pVBInfo->Part2Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_10 ;
    pVBInfo->Part3Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_12 ;
    pVBInfo->Part4Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 ;
    pVBInfo->Part5Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 + 2 ;
    if ( HwDeviceExtension->jChipType < XG20 )                  /* kuku 2004/06/25 */
    XGI_GetVBType( pVBInfo ) ;         /* Run XGI_GetVBType before InitTo330Pointer */

    InitTo330Pointer(HwDeviceExtension->jChipType,pVBInfo);

    ReadVBIOSTablData( HwDeviceExtension->jChipType , pVBInfo) ;

    if ( XGINew_GetXG20DRAMType( HwDeviceExtension, pVBInfo) == 0 )
    {
        data = ( XGINew_GetReg1( pVBInfo->P3c4 , 0x39 ) & 0x02 ) >> 1 ;
        if ( data == 0x01 )
            XGINew_DDR2x_MRS_340( pVBInfo->P3c4, pVBInfo ) ;
        else
            XGINew_DDR1x_MRS_340( pVBInfo->P3c4, pVBInfo ) ;
    }
    else
        XGINew_DDR2_MRS_XG20( HwDeviceExtension, pVBInfo->P3c4, pVBInfo);

    XGINew_SetReg1( pVBInfo->P3c4 , 0x1B , 0x03 ) ;
}

/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDRAMModeRegister */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetDRAMModeRegister( PVB_DEVICE_INFO pVBInfo)
{
    if ( XGINew_Get310DRAMType( pVBInfo ) < 2 )
    {
      XGINew_SDR_MRS(pVBInfo ) ;
    }
    else
    {
      /* SR16 <- 0F,CF,0F,8F */
      XGINew_DDR_MRS( pVBInfo ) ;
    }
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DisableRefresh */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DisableRefresh( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo)
{
    USHORT  data ;


    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x1B ) ;
    data &= 0xF8 ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x1B , data ) ;

}


/* --------------------------------------------------------------------- */
/* Function : XGINew_EnableRefresh */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_EnableRefresh( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo)
{

    XGINew_SetReg1( pVBInfo->P3c4 , 0x1B , pVBInfo->SR15[ 3 ][ XGINew_RAMType ] ) ;	/* SR1B */


}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DisableChannelInterleaving */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DisableChannelInterleaving( int index , USHORT XGINew_DDRDRAM_TYPE[][ 5 ] , PVB_DEVICE_INFO pVBInfo)
{
    USHORT data ;

    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x15 ) ;
    data &= 0x1F ;

    switch( XGINew_DDRDRAM_TYPE[ index ][ 3 ] )
    {
        case 64:
            data |= 0 ;
            break ;
        case 32:
            data |= 0x20 ;
            break ;
        case 16:
            data |= 0x40 ;
            break ;
        case 4:
            data |= 0x60 ;
            break ;
        default:
            break ;
    }
    XGINew_SetReg1( pVBInfo->P3c4 , 0x15 , data ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDRAMSizingType */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetDRAMSizingType( int index , USHORT DRAMTYPE_TABLE[][ 5 ] ,PVB_DEVICE_INFO pVBInfo)
{
    USHORT data ;

    data = DRAMTYPE_TABLE[ index ][ 4 ] ;
    XGINew_SetRegANDOR( pVBInfo->P3c4 , 0x13 , 0x80 , data ) ;
    DelayUS( 15 ) ;
   /* should delay 50 ns */
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckBusWidth_310 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_CheckBusWidth_310(  PVB_DEVICE_INFO pVBInfo)
{
    USHORT data ;
    PULONG volatile pVideoMemory ;

    pVideoMemory = (PULONG) pVBInfo->FBAddr;

    if ( XGINew_Get310DRAMType( pVBInfo ) < 2 )
    {
        XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x00 ) ;
        XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x12 ) ;
        /* should delay */
        XGINew_SDR_MRS( pVBInfo ) ;

        XGINew_ChannelAB = 0 ;
        XGINew_DataBusWidth = 128 ;
        pVideoMemory[ 0 ] = 0x01234567L ;
        pVideoMemory[ 1 ] = 0x456789ABL ;
        pVideoMemory[ 2 ] = 0x89ABCDEFL ;
        pVideoMemory[ 3 ] = 0xCDEF0123L ;
        pVideoMemory[ 4 ] = 0x55555555L ;
        pVideoMemory[ 5 ] = 0x55555555L ;
        pVideoMemory[ 6 ] = 0xFFFFFFFFL ;
        pVideoMemory[ 7 ] = 0xFFFFFFFFL ;

        if ( ( pVideoMemory[ 3 ] != 0xCDEF0123L ) || ( pVideoMemory[ 2 ] != 0x89ABCDEFL ) )
        {
            /* ChannelA64Bit */
            XGINew_DataBusWidth = 64 ;
            XGINew_ChannelAB = 0 ;
            data=XGINew_GetReg1( pVBInfo->P3c4 , 0x14 ) ;
            XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , ( USHORT )( data & 0xFD ) ) ;
        }

        if ( ( pVideoMemory[ 1 ] != 0x456789ABL ) || ( pVideoMemory[ 0 ] != 0x01234567L ) )
        {
            /* ChannelB64Bit */
            XGINew_DataBusWidth = 64 ;
            XGINew_ChannelAB = 1 ;
            data=XGINew_GetReg1( pVBInfo->P3c4 , 0x14 ) ;
            XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , ( USHORT )( ( data & 0xFD ) | 0x01 ) ) ;
        }

        return ;
    }
    else
    {
        /* DDR Dual channel */
        XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x00 ) ;
        XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x02 ) ;	/* Channel A, 64bit */
        /* should delay */
        XGINew_DDR_MRS( pVBInfo ) ;

        XGINew_ChannelAB = 0 ;
        XGINew_DataBusWidth = 64 ;
        pVideoMemory[ 0 ] = 0x01234567L ;
        pVideoMemory[ 1 ] = 0x456789ABL ;
        pVideoMemory[ 2 ] = 0x89ABCDEFL ;
        pVideoMemory[ 3 ] = 0xCDEF0123L ;
        pVideoMemory[ 4 ] = 0x55555555L ;
        pVideoMemory[ 5 ] = 0x55555555L ;
        pVideoMemory[ 6 ] = 0xAAAAAAAAL ;
        pVideoMemory[ 7 ] = 0xAAAAAAAAL ;

        if ( pVideoMemory[ 1 ] == 0x456789ABL )
        {
            if ( pVideoMemory[ 0 ] == 0x01234567L )
            {
                /* Channel A 64bit */
                return ;
            }
        }
        else
        {
            if ( pVideoMemory[ 0 ] == 0x01234567L )
            {
                /* Channel A 32bit */
                XGINew_DataBusWidth = 32 ;
                XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x00 ) ;
                return ;
            }
        }

        XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x03 ) ;	/* Channel B, 64bit */
        XGINew_DDR_MRS( pVBInfo);

        XGINew_ChannelAB = 1 ;
        XGINew_DataBusWidth = 64 ;
        pVideoMemory[ 0 ] = 0x01234567L ;
        pVideoMemory[ 1 ] = 0x456789ABL ;
        pVideoMemory[ 2 ] = 0x89ABCDEFL ;
        pVideoMemory[ 3 ] = 0xCDEF0123L ;
        pVideoMemory[ 4 ] = 0x55555555L ;
        pVideoMemory[ 5 ] = 0x55555555L ;
        pVideoMemory[ 6 ] = 0xAAAAAAAAL ;
        pVideoMemory[ 7 ] = 0xAAAAAAAAL ;

        if ( pVideoMemory[ 1 ] == 0x456789ABL )
        {
            /* Channel B 64 */
            if ( pVideoMemory[ 0 ] == 0x01234567L )
            {
                /* Channel B 64bit */
                return ;
            }
            else
            {
                /* error */
            }
        }
        else
        {
            if ( pVideoMemory[ 0 ] == 0x01234567L )
            {
                /* Channel B 32 */
                XGINew_DataBusWidth = 32 ;
                XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x01 ) ;
            }
            else
            {
                /* error */
            }
        }
    }
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetRank */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_SetRank( int index , UCHAR RankNo , UCHAR XGINew_ChannelAB , USHORT DRAMTYPE_TABLE[][ 5 ] , PVB_DEVICE_INFO pVBInfo)
{
    USHORT data ;
    int RankSize ;

    if ( ( RankNo == 2 ) && ( DRAMTYPE_TABLE[ index ][ 0 ] == 2 ) )
        return 0 ;

    RankSize = DRAMTYPE_TABLE[ index ][ 3 ] / 2 * XGINew_DataBusWidth / 32 ;

    if ( ( RankNo * RankSize ) <= 128 )
    {
        data = 0 ;

        while( ( RankSize >>= 1 ) > 0 )
        {
            data += 0x10 ;
        }
        data |= ( RankNo - 1 ) << 2 ;
        data |= ( XGINew_DataBusWidth / 64 ) & 2 ;
        data |= XGINew_ChannelAB ;
        XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , data ) ;
        /* should delay */
        XGINew_SDR_MRS( pVBInfo ) ;
        return( 1 ) ;
    }
    else
        return( 0 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDDRChannel */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_SetDDRChannel( int index , UCHAR ChannelNo , UCHAR XGINew_ChannelAB , USHORT DRAMTYPE_TABLE[][ 5 ] , PVB_DEVICE_INFO pVBInfo)
{
    USHORT  data ;
    int RankSize ;

    RankSize = DRAMTYPE_TABLE[index][3]/2 * XGINew_DataBusWidth/32;
    /* RankSize = DRAMTYPE_TABLE[ index ][ 3 ] ; */
    if ( ChannelNo * RankSize <= 128 )
    {
        data = 0 ;
        while( ( RankSize >>= 1 ) > 0 )
        {
            data += 0x10 ;
        }

        if ( ChannelNo == 2 )
            data |= 0x0C ;

        data |= ( XGINew_DataBusWidth / 32 ) & 2 ;
        data |= XGINew_ChannelAB ;
        XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , data ) ;
        /* should delay */
        XGINew_DDR_MRS( pVBInfo ) ;
        return( 1 ) ;
    }
    else
        return( 0 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckColumn */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_CheckColumn( int index , USHORT DRAMTYPE_TABLE[][ 5 ], PVB_DEVICE_INFO pVBInfo)
{
    int i ;
    ULONG Increment , Position ;

    /* Increment = 1 << ( DRAMTYPE_TABLE[ index ][ 2 ] + XGINew_DataBusWidth / 64 + 1 ) ; */
    Increment = 1 << ( 10 + XGINew_DataBusWidth / 64 ) ;

    for( i = 0 , Position = 0 ; i < 2 ; i++ )
    {
        *( ( PULONG )( pVBInfo->FBAddr + Position ) ) = Position ;
        Position += Increment ;
    }

#ifdef WIN2000  /* chiawen for linux solution */
    DelayUS( 100 ) ;
#endif

    for( i = 0 , Position = 0 ; i < 2 ; i++ )
    {
        /* if ( pVBInfo->FBAddr[ Position ] != Position ) */
        if ( ( *( PULONG )( pVBInfo->FBAddr + Position ) ) != Position )
            return( 0 ) ;
        Position += Increment ;
    }
    return( 1 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckBanks */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_CheckBanks( int index , USHORT DRAMTYPE_TABLE[][ 5 ], PVB_DEVICE_INFO pVBInfo)
{
    int i ;
    ULONG Increment , Position ;

    Increment = 1 << ( DRAMTYPE_TABLE[ index ][ 2 ] + XGINew_DataBusWidth / 64 + 2 ) ;

    for( i = 0 , Position = 0 ; i < 4 ; i++ )
    {
        /* pVBInfo->FBAddr[ Position ] = Position ; */
        *( ( PULONG )( pVBInfo->FBAddr + Position ) ) = Position ;
        Position += Increment ;
    }

    for( i = 0 , Position = 0 ; i < 4 ; i++ )
    {
        /* if (pVBInfo->FBAddr[ Position ] != Position ) */
        if ( ( *( PULONG )( pVBInfo->FBAddr + Position ) ) != Position )
            return( 0 ) ;
        Position += Increment ;
    }
    return( 1 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckRank */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_CheckRank( int RankNo , int index , USHORT DRAMTYPE_TABLE[][ 5 ], PVB_DEVICE_INFO pVBInfo)
{
    int i ;
    ULONG Increment , Position ;

    Increment = 1 << ( DRAMTYPE_TABLE[ index ][ 2 ] + DRAMTYPE_TABLE[ index ][ 1 ] +
                  DRAMTYPE_TABLE[ index ][ 0 ] + XGINew_DataBusWidth / 64 + RankNo ) ;

    for( i = 0 , Position = 0 ; i < 2 ; i++ )
    {
        /* pVBInfo->FBAddr[ Position ] = Position ; */
        /* *( ( PULONG )( pVBInfo->FBAddr ) ) = Position ; */
        *( ( PULONG )( pVBInfo->FBAddr + Position ) ) = Position ;
        Position += Increment ;
    }

    for( i = 0 , Position = 0 ; i < 2 ; i++ )
    {
        /* if ( pVBInfo->FBAddr[ Position ] != Position ) */
        /* if ( ( *( PULONG )( pVBInfo->FBAddr ) ) != Position ) */
        if ( ( *( PULONG )( pVBInfo->FBAddr + Position ) ) != Position )
            return( 0 ) ;
        Position += Increment ;
    }
    return( 1 );
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckDDRRank */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_CheckDDRRank( int RankNo , int index , USHORT DRAMTYPE_TABLE[][ 5 ], PVB_DEVICE_INFO pVBInfo)
{
    ULONG Increment , Position ;
    USHORT data ;

    Increment = 1 << ( DRAMTYPE_TABLE[ index ][ 2 ] + DRAMTYPE_TABLE[ index ][ 1 ] +
                       DRAMTYPE_TABLE[ index ][ 0 ] + XGINew_DataBusWidth / 64 + RankNo ) ;

    Increment += Increment / 2 ;

    Position = 0;
    *( ( PULONG )( pVBInfo->FBAddr + Position + 0 ) ) = 0x01234567 ;
    *( ( PULONG )( pVBInfo->FBAddr + Position + 1 ) ) = 0x456789AB ;
    *( ( PULONG )( pVBInfo->FBAddr + Position + 2 ) ) = 0x55555555 ;
    *( ( PULONG )( pVBInfo->FBAddr + Position + 3 ) ) = 0x55555555 ;
    *( ( PULONG )( pVBInfo->FBAddr + Position + 4 ) ) = 0xAAAAAAAA ;
    *( ( PULONG )( pVBInfo->FBAddr + Position + 5 ) ) = 0xAAAAAAAA ;

    if ( ( *( PULONG )( pVBInfo->FBAddr + 1 ) ) == 0x456789AB )
        return( 1 ) ;

    if ( ( *( PULONG )( pVBInfo->FBAddr + 0 ) ) == 0x01234567 )
        return( 0 ) ;

    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x14 ) ;
    data &= 0xF3 ;
    data |= 0x0E ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , data ) ;
    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x15 ) ;
    data += 0x20 ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x15 , data ) ;

    return( 1 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckRanks */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_CheckRanks( int RankNo , int index , USHORT DRAMTYPE_TABLE[][ 5 ], PVB_DEVICE_INFO pVBInfo)
{
    int r ;

    for( r = RankNo ; r >= 1 ; r-- )
    {
        if ( !XGINew_CheckRank( r , index , DRAMTYPE_TABLE, pVBInfo ) )
            return( 0 ) ;
    }

    if ( !XGINew_CheckBanks( index , DRAMTYPE_TABLE, pVBInfo ) )
        return( 0 ) ;

    if ( !XGINew_CheckColumn( index , DRAMTYPE_TABLE, pVBInfo ) )
        return( 0 ) ;

    return( 1 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckDDRRanks */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_CheckDDRRanks( int RankNo , int index , USHORT DRAMTYPE_TABLE[][ 5 ], PVB_DEVICE_INFO pVBInfo)
{
    int r ;

    for( r = RankNo ; r >= 1 ; r-- )
    {
        if ( !XGINew_CheckDDRRank( r , index , DRAMTYPE_TABLE, pVBInfo ) )
            return( 0 ) ;
    }

    if ( !XGINew_CheckBanks( index , DRAMTYPE_TABLE, pVBInfo ) )
        return( 0 ) ;

    if ( !XGINew_CheckColumn( index , DRAMTYPE_TABLE, pVBInfo ) )
        return( 0 ) ;

    return( 1 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_SDRSizing(PVB_DEVICE_INFO pVBInfo)
{
    int    i ;
    UCHAR  j ;

    for( i = 0 ; i < 13 ; i++ )
    {
        XGINew_SetDRAMSizingType( i , XGINew_SDRDRAM_TYPE , pVBInfo) ;

        for( j = 2 ; j > 0 ; j-- )
        {
            if ( !XGINew_SetRank( i , ( UCHAR )j , XGINew_ChannelAB , XGINew_SDRDRAM_TYPE , pVBInfo) )
                continue ;
            else
            {
                if ( XGINew_CheckRanks( j , i , XGINew_SDRDRAM_TYPE, pVBInfo) )
                    return( 1 ) ;
            }
        }
    }
    return( 0 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDRAMSizeReg */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
USHORT XGINew_SetDRAMSizeReg( int index , USHORT DRAMTYPE_TABLE[][ 5 ], PVB_DEVICE_INFO pVBInfo)
{
    USHORT data = 0 , memsize = 0 ;
    int RankSize ;
    UCHAR ChannelNo ;

    RankSize = DRAMTYPE_TABLE[ index ][ 3 ] * XGINew_DataBusWidth / 32 ;
    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x13 ) ;
    data &= 0x80 ;

    if ( data == 0x80 )
        RankSize *= 2 ;

    data = 0 ;

    if( XGINew_ChannelAB == 3 )
        ChannelNo = 4 ;
    else
        ChannelNo = XGINew_ChannelAB ;

    if ( ChannelNo * RankSize <= 256 )
    {
        while( ( RankSize >>= 1 ) > 0 )
        {
            data += 0x10 ;
        }

        memsize = data >> 4 ;

        /* [2004/03/25] Vicent, Fix DRAM Sizing Error */
        XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , ( XGINew_GetReg1( pVBInfo->P3c4 , 0x14 ) & 0x0F ) | ( data & 0xF0 ) ) ;

       /* data |= XGINew_ChannelAB << 2 ; */
       /* data |= ( XGINew_DataBusWidth / 64 ) << 1 ; */
       /* XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , data ) ; */

        /* should delay */
        /* XGINew_SetDRAMModeRegister340( pVBInfo ) ; */
    }
    return( memsize ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDRAMSize20Reg */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
USHORT XGINew_SetDRAMSize20Reg( int index , USHORT DRAMTYPE_TABLE[][ 5 ], PVB_DEVICE_INFO pVBInfo)
{
    USHORT data = 0 , memsize = 0 ;
    int RankSize ;
    UCHAR ChannelNo ;

    RankSize = DRAMTYPE_TABLE[ index ][ 3 ] * XGINew_DataBusWidth / 8 ;
    data = XGINew_GetReg1( pVBInfo->P3c4 , 0x13 ) ;
    data &= 0x80 ;

    if ( data == 0x80 )
        RankSize *= 2 ;

    data = 0 ;

    if( XGINew_ChannelAB == 3 )
        ChannelNo = 4 ;
    else
        ChannelNo = XGINew_ChannelAB ;

    if ( ChannelNo * RankSize <= 256 )
    {
        while( ( RankSize >>= 1 ) > 0 )
        {
            data += 0x10 ;
        }

        memsize = data >> 4 ;

        /* [2004/03/25] Vicent, Fix DRAM Sizing Error */
        XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , ( XGINew_GetReg1( pVBInfo->P3c4 , 0x14 ) & 0x0F ) | ( data & 0xF0 ) ) ;
	DelayUS( 15 ) ;

       /* data |= XGINew_ChannelAB << 2 ; */
       /* data |= ( XGINew_DataBusWidth / 64 ) << 1 ; */
       /* XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , data ) ; */

        /* should delay */
        /* XGINew_SetDRAMModeRegister340( pVBInfo ) ; */
    }
    return( memsize ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_ReadWriteRest */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_ReadWriteRest( USHORT StopAddr , USHORT StartAddr, PVB_DEVICE_INFO pVBInfo)
{
    int i ;
    ULONG Position = 0 ;

   *( ( PULONG )( pVBInfo->FBAddr + Position ) ) = Position ;

    for( i = StartAddr ; i <= StopAddr ; i++ )
    {
        Position = 1 << i ;
        *( ( PULONG )( pVBInfo->FBAddr + Position ) ) = Position ;
    }

    DelayUS( 500 ) ;	/* [Vicent] 2004/04/16. Fix #1759 Memory Size error in Multi-Adapter. */

    Position = 0 ;

   if ( ( *( PULONG )( pVBInfo->FBAddr + Position ) ) != Position )
        return( 0 ) ;

    for( i = StartAddr ; i <= StopAddr ; i++ )
    {
        Position = 1 << i ;
        if ( ( *( PULONG )( pVBInfo->FBAddr + Position ) ) != Position )
            return( 0 ) ;
    }
    return( 1 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckFrequence */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
UCHAR XGINew_CheckFrequence( PVB_DEVICE_INFO pVBInfo )
{
    UCHAR data ;

    data = XGINew_GetReg1( pVBInfo->P3d4 , 0x97 ) ;

    if ( ( data & 0x10 ) == 0 )
    {
        data = XGINew_GetReg1( pVBInfo->P3c4 , 0x39 ) ;
        data = ( data & 0x02 ) >> 1 ;
        return( data ) ;
    }
    else
        return( data & 0x01 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_CheckChannel */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_CheckChannel( PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO pVBInfo)
{
    UCHAR data;

    switch( HwDeviceExtension->jChipType )
    {
      case XG20:
      case XG21:
          data = XGINew_GetReg1( pVBInfo->P3d4 , 0x97 ) ;
          data = data & 0x01;
          XGINew_ChannelAB = 1 ;		/* XG20 "JUST" one channel */

          if ( data == 0 )  /* Single_32_16 */
          {

	      if (( HwDeviceExtension->ulVideoMemorySize - 1 ) > 0x1000000)
	      {

                XGINew_DataBusWidth = 32 ;	/* 32 bits */
                XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xB1 ) ;  /* 22bit + 2 rank + 32bit */
                XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x52 ) ;
		DelayUS( 15 ) ;

                if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                    return ;

		if (( HwDeviceExtension->ulVideoMemorySize - 1 ) > 0x800000)
		{
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x31 ) ;  /* 22bit + 1 rank + 32bit */
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x42 ) ;
		  DelayUS( 15 ) ;

                  if ( XGINew_ReadWriteRest( 23 , 23 , pVBInfo ) == 1 )
                      return ;
                }
	      }

	      if (( HwDeviceExtension->ulVideoMemorySize - 1 ) > 0x800000)
	      {
	        XGINew_DataBusWidth = 16 ;	/* 16 bits */
                XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xB1 ) ;  /* 22bit + 2 rank + 16bit */
                XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x41 ) ;
		DelayUS( 15 ) ;

                if ( XGINew_ReadWriteRest( 23 , 22 , pVBInfo ) == 1 )
                    return ;
                else
                    XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x31 ) ;
                    DelayUS( 15 ) ;
              }

          }
          else  /* Dual_16_8 */
          {
              if (( HwDeviceExtension->ulVideoMemorySize - 1 ) > 0x800000)
              {

                XGINew_DataBusWidth = 16 ;	/* 16 bits */
                XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xB1 ) ;  /* (0x31:12x8x2) 22bit + 2 rank */
                XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x41 ) ;  /* 0x41:16Mx16 bit*/
                DelayUS( 15 ) ;

                if ( XGINew_ReadWriteRest( 23 , 22 , pVBInfo ) == 1 )
                    return ;

		if (( HwDeviceExtension->ulVideoMemorySize - 1 ) > 0x400000)
		{
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x31 ) ;  /* (0x31:12x8x2) 22bit + 1 rank */
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x31 ) ;  /* 0x31:8Mx16 bit*/
                  DelayUS( 15 ) ;

                  if ( XGINew_ReadWriteRest( 22 , 22 , pVBInfo ) == 1 )
                      return ;
                }
	      }


	      if (( HwDeviceExtension->ulVideoMemorySize - 1 ) > 0x400000)
	      {
	        XGINew_DataBusWidth = 8 ;	/* 8 bits */
                XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xB1 ) ;  /* (0x31:12x8x2) 22bit + 2 rank */
                XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x30 ) ;  /* 0x30:8Mx8 bit*/
                DelayUS( 15 ) ;

                if ( XGINew_ReadWriteRest( 22 , 21 , pVBInfo ) == 1 )
                    return ;
                else
                    XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x31 ) ;  /* (0x31:12x8x2) 22bit + 1 rank */
                    DelayUS( 15 ) ;
              }
          }
          break ;

      case XG27:
          XGINew_DataBusWidth = 16 ;	/* 16 bits */
          XGINew_ChannelAB = 1 ;		/* Single channel */
          XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x51 ) ;  /* 32Mx16 bit*/
          break ;
      case XG41:
          if ( XGINew_CheckFrequence(pVBInfo) == 1 )
          {
              XGINew_DataBusWidth = 32 ;	/* 32 bits */
              XGINew_ChannelAB = 3 ;		/* Quad Channel */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xA1 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x4C ) ;

              if ( XGINew_ReadWriteRest( 25 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_ChannelAB = 2 ;		/* Dual channels */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x48 ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x49 ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_ChannelAB = 3 ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x21 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x3C ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x38 ) ;

              if ( XGINew_ReadWriteRest( 8 , 4 , pVBInfo ) == 1 )
                  return ;
              else
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x39 ) ;
          }
          else
          {					/* DDR */
              XGINew_DataBusWidth = 64 ;	/* 64 bits */
              XGINew_ChannelAB = 2 ;		/* Dual channels */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xA1 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x5A ) ;

              if ( XGINew_ReadWriteRest( 25 , 24 , pVBInfo ) == 1 )
                  return ;

              XGINew_ChannelAB = 1 ;		/* Single channels */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x52 ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x53 ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_ChannelAB = 2 ;		/* Dual channels */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x21 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x4A ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_ChannelAB = 1 ;		/* Single channels */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x42 ) ;

              if ( XGINew_ReadWriteRest( 8 , 4 , pVBInfo ) == 1 )
                  return ;
              else
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x43 ) ;
          }

          break ;

      case XG42:
/*
      	  XG42 SR14 D[3] Reserve
      	  	    D[2] = 1, Dual Channel
      	  	         = 0, Single Channel

      	  It's Different from Other XG40 Series.
*/
          if ( XGINew_CheckFrequence(pVBInfo) == 1 )	/* DDRII, DDR2x */
          {
              XGINew_DataBusWidth = 32 ;	/* 32 bits */
              XGINew_ChannelAB = 2 ;		/* 2 Channel */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xA1 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x44 ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x21 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x34 ) ;
              if ( XGINew_ReadWriteRest( 23 , 22 , pVBInfo ) == 1 )
                  return ;

              XGINew_ChannelAB = 1 ;		/* Single Channel */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xA1 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x40 ) ;

              if ( XGINew_ReadWriteRest( 23 , 22 , pVBInfo ) == 1 )
                  return ;
              else
              {
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x21 ) ;
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x30 ) ;
              }
          }
          else
          {					/* DDR */
              XGINew_DataBusWidth = 64 ;	/* 64 bits */
              XGINew_ChannelAB = 1 ;		/* 1 channels */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xA1 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x52 ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;
              else
              {
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x21 ) ;
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x42 ) ;
              }
          }

          break ;

      default:	/* XG40 */

          if ( XGINew_CheckFrequence(pVBInfo) == 1 )	/* DDRII */
          {
              XGINew_DataBusWidth = 32 ;	/* 32 bits */
              XGINew_ChannelAB = 3 ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xA1 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x4C ) ;

              if ( XGINew_ReadWriteRest( 25 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_ChannelAB = 2 ;		/* 2 channels */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x48 ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  return ;

              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x21 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x3C ) ;

              if ( XGINew_ReadWriteRest( 24 , 23 , pVBInfo ) == 1 )
                  XGINew_ChannelAB = 3 ;	/* 4 channels */
              else
              {
                  XGINew_ChannelAB = 2 ;	/* 2 channels */
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x38 ) ;
              }
          }
          else
          {					/* DDR */
              XGINew_DataBusWidth = 64 ;	/* 64 bits */
              XGINew_ChannelAB = 2 ;		/* 2 channels */
              XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0xA1 ) ;
              XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x5A ) ;

              if ( XGINew_ReadWriteRest( 25 , 24 , pVBInfo ) == 1 )
                  return ;
              else
              {
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x13 , 0x21 ) ;
                  XGINew_SetReg1( pVBInfo->P3c4 , 0x14 , 0x4A ) ;
              }
          }
      	  break ;
    }
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DDRSizing340 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_DDRSizing340( PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO pVBInfo)
{
    int i ;
    USHORT memsize , addr ;

    XGINew_SetReg1( pVBInfo->P3c4 , 0x15 , 0x00 ) ;	/* noninterleaving */
    XGINew_SetReg1( pVBInfo->P3c4 , 0x1C , 0x00 ) ;	/* nontiling */
    XGINew_CheckChannel( HwDeviceExtension, pVBInfo ) ;


    if ( HwDeviceExtension->jChipType >= XG20 )
    {
      for( i = 0 ; i < 12 ; i++ )
      {
        XGINew_SetDRAMSizingType( i , XGINew_DDRDRAM_TYPE20, pVBInfo ) ;
        memsize = XGINew_SetDRAMSize20Reg( i , XGINew_DDRDRAM_TYPE20, pVBInfo ) ;
        if ( memsize == 0 )
            continue ;

        addr = memsize + ( XGINew_ChannelAB - 2 ) + 20 ;
        if ( ( HwDeviceExtension->ulVideoMemorySize - 1 ) < ( ULONG )( 1 << addr ) )
            continue ;

        if ( XGINew_ReadWriteRest( addr , 5, pVBInfo ) == 1 )
            return( 1 ) ;
      }
    }
    else
    {
      for( i = 0 ; i < 4 ; i++ )
      {
    	XGINew_SetDRAMSizingType( i , XGINew_DDRDRAM_TYPE340, pVBInfo ) ;
        memsize = XGINew_SetDRAMSizeReg( i , XGINew_DDRDRAM_TYPE340, pVBInfo ) ;

        if ( memsize == 0 )
            continue ;

        addr = memsize + ( XGINew_ChannelAB - 2 ) + 20 ;
        if ( ( HwDeviceExtension->ulVideoMemorySize - 1 ) < ( ULONG )( 1 << addr ) )
            continue ;

        if ( XGINew_ReadWriteRest( addr , 9, pVBInfo ) == 1 )
            return( 1 ) ;
      }
    }
    return( 0 ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_DDRSizing */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
int XGINew_DDRSizing(PVB_DEVICE_INFO pVBInfo)
{
    int    i ;
    UCHAR  j ;

    for( i = 0 ; i < 4 ; i++ )
    {
        XGINew_SetDRAMSizingType( i , XGINew_DDRDRAM_TYPE, pVBInfo ) ;
        XGINew_DisableChannelInterleaving( i , XGINew_DDRDRAM_TYPE , pVBInfo) ;
        for( j = 2 ; j > 0 ; j-- )
        {
            XGINew_SetDDRChannel( i , j , XGINew_ChannelAB , XGINew_DDRDRAM_TYPE , pVBInfo ) ;
            if ( !XGINew_SetRank( i , ( UCHAR )j , XGINew_ChannelAB , XGINew_DDRDRAM_TYPE, pVBInfo ) )
                continue ;
            else
            {
                if ( XGINew_CheckDDRRanks( j , i , XGINew_DDRDRAM_TYPE,  pVBInfo ) )
                return( 1 ) ;
            }
        }
    }
    return( 0 ) ;
}

/* --------------------------------------------------------------------- */
/* Function : XGINew_SetMemoryClock */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetMemoryClock( PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO pVBInfo)
{


    XGINew_SetReg1( pVBInfo->P3c4 , 0x28 , pVBInfo->MCLKData[ XGINew_RAMType ].SR28 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x29 , pVBInfo->MCLKData[ XGINew_RAMType ].SR29 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x2A , pVBInfo->MCLKData[ XGINew_RAMType ].SR2A ) ;



    XGINew_SetReg1( pVBInfo->P3c4 , 0x2E , pVBInfo->ECLKData[ XGINew_RAMType ].SR2E ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x2F , pVBInfo->ECLKData[ XGINew_RAMType ].SR2F ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x30 , pVBInfo->ECLKData[ XGINew_RAMType ].SR30 ) ;

    /* [Vicent] 2004/07/07, When XG42 ECLK = MCLK = 207MHz, Set SR32 D[1:0] = 10b */
    /* [Hsuan] 2004/08/20, Modify SR32 value, when MCLK=207MHZ, ELCK=250MHz, Set SR32 D[1:0] = 10b */
    if ( HwDeviceExtension->jChipType == XG42 )
    {
      if ( ( pVBInfo->MCLKData[ XGINew_RAMType ].SR28 == 0x1C ) && ( pVBInfo->MCLKData[ XGINew_RAMType ].SR29 == 0x01 )
        && ( ( ( pVBInfo->ECLKData[ XGINew_RAMType ].SR2E == 0x1C ) && ( pVBInfo->ECLKData[ XGINew_RAMType ].SR2F == 0x01 ) )
        || ( ( pVBInfo->ECLKData[ XGINew_RAMType ].SR2E == 0x22 ) && ( pVBInfo->ECLKData[ XGINew_RAMType ].SR2F == 0x01 ) ) ) )
      {
      	XGINew_SetReg1( pVBInfo->P3c4 , 0x32 , ( ( UCHAR )XGINew_GetReg1( pVBInfo->P3c4 , 0x32 ) & 0xFC ) | 0x02 ) ;
      }
    }
}


/* --------------------------------------------------------------------- */
/* Function : ChkLFB */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
BOOLEAN ChkLFB( PVB_DEVICE_INFO pVBInfo )
{
    if ( LFBDRAMTrap & XGINew_GetReg1( pVBInfo->P3d4 , 0x78 ) )
        return( TRUE ) ;
    else
        return( FALSE );
}


/* --------------------------------------------------------------------- */
/* input : dx ,valid value : CR or second chip's CR */
/*  */
/* SetPowerConsume : */
/* Description: reduce 40/43 power consumption in first chip or */
/* in second chip, assume CR A1 D[6]="1" in this case */
/* output : none */
/* --------------------------------------------------------------------- */
void SetPowerConsume ( PXGI_HW_DEVICE_INFO HwDeviceExtension , ULONG XGI_P3d4Port )
{
    ULONG   lTemp ;
    UCHAR   bTemp;

    HwDeviceExtension->pQueryVGAConfigSpace( HwDeviceExtension , 0x08 , 0 , &lTemp ) ; /* Get */
    if ((lTemp&0xFF)==0)
    {
        /* set CR58 D[5]=0 D[3]=0 */
        XGINew_SetRegAND( XGI_P3d4Port , 0x58 , 0xD7 ) ;
        bTemp = (UCHAR) XGINew_GetReg1( XGI_P3d4Port , 0xCB ) ;
    	if (bTemp&0x20)
    	{
            if (!(bTemp&0x10))
            {
            	XGINew_SetRegANDOR( XGI_P3d4Port , 0x58 , 0xD7 , 0x20 ) ; /* CR58 D[5]=1 D[3]=0 */
            }
            else
            {
            	XGINew_SetRegANDOR( XGI_P3d4Port , 0x58 , 0xD7 , 0x08 ) ; /* CR58 D[5]=0 D[3]=1 */
            }

    	}

    }
}



#if defined(LINUX_XF86)||defined(LINUX_KERNEL)
void XGINew_InitVBIOSData(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO pVBInfo)
{

	/* ULONG ROMAddr = (ULONG)HwDeviceExtension->pjVirtualRomBase; */
    pVBInfo->ROMAddr = HwDeviceExtension->pjVirtualRomBase ;
    pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress ;
    pVBInfo->BaseAddr = (ULONG)HwDeviceExtension->pjIOAddress ;
    pVBInfo->ISXPDOS = 0 ;

    pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14 ;
    pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24 ;
    pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10 ;
    pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e ;
    pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12 ;
    pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a ;
    pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16 ;
    pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17 ;
    pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18 ;
    pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19 ;
    pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A ;
    pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00 ;
    pVBInfo->Part1Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_04 ;
    pVBInfo->Part2Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_10 ;
    pVBInfo->Part3Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_12 ;
    pVBInfo->Part4Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 ;
    pVBInfo->Part5Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 + 2 ;
    if ( HwDeviceExtension->jChipType < XG20 )                  /* kuku 2004/06/25 */
    XGI_GetVBType( pVBInfo ) ;         /* Run XGI_GetVBType before InitTo330Pointer */

	switch(HwDeviceExtension->jChipType)
	{
	case XG40:
	case XG41:
	case XG42:
	case XG20:
	case XG21:
	default:
		InitTo330Pointer(HwDeviceExtension->jChipType,pVBInfo);
		return ;
	}

}
#endif /* For Linux */

/* --------------------------------------------------------------------- */
/* Function : ReadVBIOSTablData */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void ReadVBIOSTablData( UCHAR ChipType , PVB_DEVICE_INFO pVBInfo)
{
    PUCHAR  volatile pVideoMemory = ( PUCHAR )pVBInfo->ROMAddr ;
    ULONG   i ;
    UCHAR   j , k ;
#if 0
    ULONG   ii , jj ;
    i = pVideoMemory[ 0x1CF ] | ( pVideoMemory[ 0x1D0 ] << 8 ) ;		/* UniROM */
    if ( i != 0 )
        UNIROM = 1 ;

    ii = 0x90 ;
    for( jj = 0x00 ; jj < 0x08 ; jj++ )
    {
        pVBInfo->MCLKData[ jj ].SR28 = pVideoMemory[ ii ] ;
        pVBInfo->MCLKData[ jj ].SR29 = pVideoMemory[ ii + 1] ;
        pVBInfo->MCLKData[ jj ].SR2A = pVideoMemory[ ii + 2] ;
        pVBInfo->MCLKData[ jj ].CLOCK = pVideoMemory[ ii + 3 ] | ( pVideoMemory[ ii + 4 ] << 8 ) ;
        ii += 0x05 ;
    }

    ii = 0xB8 ;
    for( jj = 0x00 ; jj < 0x08 ; jj++ )
    {
        pVBInfo->ECLKData[ jj ].SR2E = pVideoMemory[ ii ] ;
        pVBInfo->ECLKData[ jj ].SR2F=pVideoMemory[ ii + 1 ] ;
        pVBInfo->ECLKData[ jj ].SR30= pVideoMemory[ ii + 2 ] ;
        pVBInfo->ECLKData[ jj ].CLOCK= pVideoMemory[ ii + 3 ] | ( pVideoMemory[ ii + 4 ] << 8 ) ;
        ii += 0x05 ;
    }

    /* Volari customize data area start */
    /* if ( ChipType == XG40 ) */
    if ( ChipType >= XG40 )
    {
        ii = 0xE0 ;
        for( jj = 0x00 ; jj < 0x03 ; jj++ )
        {
            pVBInfo->SR15[ jj ][ 0 ] = pVideoMemory[ ii ] ;		/* SR13, SR14, and SR18 */
            pVBInfo->SR15[ jj ][ 1 ] = pVideoMemory[ ii + 1 ] ;
            pVBInfo->SR15[ jj ][ 2 ] = pVideoMemory[ ii + 2 ] ;
            pVBInfo->SR15[ jj ][ 3 ] = pVideoMemory[ ii + 3 ] ;
            pVBInfo->SR15[ jj ][ 4 ] = pVideoMemory[ ii + 4 ] ;
            pVBInfo->SR15[ jj ][ 5 ] = pVideoMemory[ ii + 5 ] ;
            pVBInfo->SR15[ jj ][ 6 ] = pVideoMemory[ ii + 6 ] ;
            pVBInfo->SR15[ jj ][ 7 ] = pVideoMemory[ ii + 7 ] ;
            ii += 0x08 ;
        }
        ii = 0x110 ;
        jj = 0x03 ;
        pVBInfo->SR15[ jj ][ 0 ] = pVideoMemory[ ii ] ;		/* SR1B */
        pVBInfo->SR15[ jj ][ 1 ] = pVideoMemory[ ii + 1 ] ;
        pVBInfo->SR15[ jj ][ 2 ] = pVideoMemory[ ii + 2 ] ;
        pVBInfo->SR15[ jj ][ 3 ] = pVideoMemory[ ii + 3 ] ;
        pVBInfo->SR15[ jj ][ 4 ] = pVideoMemory[ ii + 4 ] ;
        pVBInfo->SR15[ jj ][ 5 ] = pVideoMemory[ ii + 5 ] ;
        pVBInfo->SR15[ jj ][ 6 ] = pVideoMemory[ ii + 6 ] ;
        pVBInfo->SR15[ jj ][ 7 ] = pVideoMemory[ ii + 7 ] ;

        *pVBInfo->pSR07 = pVideoMemory[ 0x74 ] ;
        *pVBInfo->pSR1F = pVideoMemory[ 0x75 ] ;
        *pVBInfo->pSR21 = pVideoMemory[ 0x76 ] ;
        *pVBInfo->pSR22 = pVideoMemory[ 0x77 ] ;
        *pVBInfo->pSR23 = pVideoMemory[ 0x78 ] ;
        *pVBInfo->pSR24 = pVideoMemory[ 0x79 ] ;
        pVBInfo->SR25[ 0 ] = pVideoMemory[ 0x7A ] ;
        *pVBInfo->pSR31 = pVideoMemory[ 0x7B ] ;
        *pVBInfo->pSR32 = pVideoMemory[ 0x7C ] ;
        *pVBInfo->pSR33 = pVideoMemory[ 0x7D ] ;
        ii = 0xF8 ;

        for( jj = 0 ; jj < 3 ; jj++ )
        {
            pVBInfo->CR40[ jj ][ 0 ] = pVideoMemory[ ii ] ;
            pVBInfo->CR40[ jj ][ 1 ] = pVideoMemory[ ii + 1 ] ;
            pVBInfo->CR40[ jj ][ 2 ] = pVideoMemory[ ii + 2 ] ;
            pVBInfo->CR40[ jj ][ 3 ] = pVideoMemory[ ii + 3 ] ;
            pVBInfo->CR40[ jj ][ 4 ] = pVideoMemory[ ii + 4 ] ;
            pVBInfo->CR40[ jj ][ 5 ] = pVideoMemory[ ii + 5 ] ;
            pVBInfo->CR40[ jj ][ 6 ] = pVideoMemory[ ii + 6 ] ;
            pVBInfo->CR40[ jj ][ 7 ] = pVideoMemory[ ii + 7 ] ;
            ii += 0x08 ;
        }

        ii = 0x118 ;
        for( j = 3 ; j < 24 ; j++ )
        {
            pVBInfo->CR40[ j ][ 0 ] = pVideoMemory[ ii ] ;
            pVBInfo->CR40[ j ][ 1 ] = pVideoMemory[ ii + 1 ] ;
            pVBInfo->CR40[ j ][ 2 ] = pVideoMemory[ ii + 2 ] ;
            pVBInfo->CR40[ j ][ 3 ] = pVideoMemory[ ii + 3 ] ;
            pVBInfo->CR40[ j ][ 4 ] = pVideoMemory[ ii + 4 ] ;
            pVBInfo->CR40[ j ][ 5 ] = pVideoMemory[ ii + 5 ] ;
            pVBInfo->CR40[ j ][ 6 ] = pVideoMemory[ ii + 6 ] ;
            pVBInfo->CR40[ j ][ 7 ] = pVideoMemory[ ii + 7 ] ;
            ii += 0x08 ;
        }

        i = pVideoMemory[ 0x1C0 ] | ( pVideoMemory[ 0x1C1 ] << 8 ) ;

        for( j = 0 ; j < 8 ; j++ )
        {
            for( k = 0 ; k < 4 ; k++ )
                pVBInfo->CR6B[ j ][ k ] = pVideoMemory[ i + 4 * j + k ] ;
        }

        i = pVideoMemory[ 0x1C2 ] | ( pVideoMemory[ 0x1C3 ] << 8 ) ;

        for( j = 0 ; j < 8 ; j++ )
        {
            for( k = 0 ; k < 4 ; k++ )
                pVBInfo->CR6E[ j ][ k ] = pVideoMemory[ i + 4 * j + k ] ;
        }

        i = pVideoMemory[ 0x1C4 ] | ( pVideoMemory[ 0x1C5 ] << 8 ) ;
        for( j = 0 ; j < 8 ; j++ )
        {
            for( k = 0 ; k < 32 ; k++ )
                pVBInfo->CR6F[ j ][ k ] = pVideoMemory[ i + 32 * j + k ] ;
        }

        i = pVideoMemory[ 0x1C6 ] | ( pVideoMemory[ 0x1C7 ] << 8 ) ;

        for( j = 0 ; j < 8 ; j++ )
        {
            for( k = 0 ; k < 2 ; k++ )
                pVBInfo->CR89[ j ][ k ] = pVideoMemory[ i + 2 * j + k ] ;
        }

        i = pVideoMemory[ 0x1C8 ] | ( pVideoMemory[ 0x1C9 ] << 8 ) ;
        for( j = 0 ; j < 12 ; j++ )
            pVBInfo->AGPReg[ j ] = pVideoMemory[ i + j ] ;

        i = pVideoMemory[ 0x1CF ] | ( pVideoMemory[ 0x1D0 ] << 8 ) ;
        for( j = 0 ; j < 4 ; j++ )
            pVBInfo->SR16[ j ] = pVideoMemory[ i + j ] ;

        if ( ChipType == XG21 )
        {
            if (pVideoMemory[ 0x67 ] & 0x80)
            {
                *pVBInfo->pDVOSetting = pVideoMemory[ 0x67 ];
            }
            if ( (pVideoMemory[ 0x67 ] & 0xC0) == 0xC0 )
            {
                *pVBInfo->pCR2E = pVideoMemory[ i + 4 ] ;
                *pVBInfo->pCR2F = pVideoMemory[ i + 5 ] ;
                *pVBInfo->pCR46 = pVideoMemory[ i + 6 ] ;
                *pVBInfo->pCR47 = pVideoMemory[ i + 7 ] ;
            }
        }

        if ( ChipType == XG27 )
        {
            jj = i+j;
            for( i = 0 ; i <= 0xB ; i++,jj++ )
              pVBInfo->pCRD0[i] = pVideoMemory[ jj ] ;
            for( i = 0x0 ; i <= 0x1 ; i++,jj++ )
              pVBInfo->pCRDE[i] = pVideoMemory[ jj ] ;

            *pVBInfo->pSR40 = pVideoMemory[ jj ] ;
            jj++;
            *pVBInfo->pSR41 = pVideoMemory[ jj ] ;

            if (pVideoMemory[ 0x67 ] & 0x80)
            {
                *pVBInfo->pDVOSetting = pVideoMemory[ 0x67 ];
            }
            if ( (pVideoMemory[ 0x67 ] & 0xC0) == 0xC0 )
            {
                jj++;
                *pVBInfo->pCR2E = pVideoMemory[ jj ] ;
                *pVBInfo->pCR2F = pVideoMemory[ jj + 1 ] ;
                *pVBInfo->pCR46 = pVideoMemory[ jj + 2 ] ;
                *pVBInfo->pCR47 = pVideoMemory[ jj + 3 ] ;
            }

        }

        *pVBInfo->pCRCF = pVideoMemory[ 0x1CA ] ;
        *pVBInfo->pXGINew_DRAMTypeDefinition = pVideoMemory[ 0x1CB ] ;
        *pVBInfo->pXGINew_I2CDefinition = pVideoMemory[ 0x1D1 ] ;
        if ( ChipType >= XG20 )
        {
           *pVBInfo->pXGINew_CR97 = pVideoMemory[ 0x1D2 ] ;
           if ( ChipType == XG27 )
           {
             *pVBInfo->pSR36 = pVideoMemory[ 0x1D3 ] ;
             *pVBInfo->pCR8F = pVideoMemory[ 0x1D5 ] ;
           }
        }

    }
#endif
    /* Volari customize data area end */

    if ( ChipType == XG21 )
    {
        pVBInfo->IF_DEF_LVDS = 0 ;
        if (pVideoMemory[ 0x65 ] & 0x1)
        {
            pVBInfo->IF_DEF_LVDS = 1 ;
            i = pVideoMemory[ 0x316 ] | ( pVideoMemory[ 0x317 ] << 8 );
            j = pVideoMemory[ i-1 ] ;
            if ( j != 0xff )
            {
              k = 0;
              do
              {
                pVBInfo->XG21_LVDSCapList[k].LVDS_Capability = pVideoMemory[ i ] | ( pVideoMemory[ i + 1 ] << 8 );
                pVBInfo->XG21_LVDSCapList[k].LVDSHT = pVideoMemory[ i + 2 ] | ( pVideoMemory[ i + 3 ] << 8 ) ;
                pVBInfo->XG21_LVDSCapList[k].LVDSVT = pVideoMemory[ i + 4 ] | ( pVideoMemory[ i + 5 ] << 8 );
                pVBInfo->XG21_LVDSCapList[k].LVDSHDE = pVideoMemory[ i + 6 ] | ( pVideoMemory[ i + 7 ] << 8 );
                pVBInfo->XG21_LVDSCapList[k].LVDSVDE = pVideoMemory[ i + 8 ] | ( pVideoMemory[ i + 9 ] << 8 );
                pVBInfo->XG21_LVDSCapList[k].LVDSHFP = pVideoMemory[ i + 10 ] | ( pVideoMemory[ i + 11 ] << 8 );
                pVBInfo->XG21_LVDSCapList[k].LVDSVFP = pVideoMemory[ i + 12 ] | ( pVideoMemory[ i + 13 ] << 8 );
                pVBInfo->XG21_LVDSCapList[k].LVDSHSYNC = pVideoMemory[ i + 14 ] | ( pVideoMemory[ i + 15 ] << 8 );
                pVBInfo->XG21_LVDSCapList[k].LVDSVSYNC = pVideoMemory[ i + 16 ] | ( pVideoMemory[ i + 17 ] << 8 );
                pVBInfo->XG21_LVDSCapList[k].VCLKData1 = pVideoMemory[ i + 18 ] ;
                pVBInfo->XG21_LVDSCapList[k].VCLKData2 = pVideoMemory[ i + 19 ] ;
                pVBInfo->XG21_LVDSCapList[k].PSC_S1 = pVideoMemory[ i + 20 ] ;
                pVBInfo->XG21_LVDSCapList[k].PSC_S2 = pVideoMemory[ i + 21 ] ;
                pVBInfo->XG21_LVDSCapList[k].PSC_S3 = pVideoMemory[ i + 22 ] ;
                pVBInfo->XG21_LVDSCapList[k].PSC_S4 = pVideoMemory[ i + 23 ] ;
                pVBInfo->XG21_LVDSCapList[k].PSC_S5 = pVideoMemory[ i + 24 ] ;
                i += 25;
                j--;
                k++;
              } while ( (j>0) && ( k < (sizeof(XGI21_LCDCapList)/sizeof(XGI21_LVDSCapStruct)) ) );
            }
            else
            {
            pVBInfo->XG21_LVDSCapList[0].LVDS_Capability = pVideoMemory[ i ] | ( pVideoMemory[ i + 1 ] << 8 );
            pVBInfo->XG21_LVDSCapList[0].LVDSHT = pVideoMemory[ i + 2 ] | ( pVideoMemory[ i + 3 ] << 8 ) ;
            pVBInfo->XG21_LVDSCapList[0].LVDSVT = pVideoMemory[ i + 4 ] | ( pVideoMemory[ i + 5 ] << 8 );
            pVBInfo->XG21_LVDSCapList[0].LVDSHDE = pVideoMemory[ i + 6 ] | ( pVideoMemory[ i + 7 ] << 8 );
            pVBInfo->XG21_LVDSCapList[0].LVDSVDE = pVideoMemory[ i + 8 ] | ( pVideoMemory[ i + 9 ] << 8 );
            pVBInfo->XG21_LVDSCapList[0].LVDSHFP = pVideoMemory[ i + 10 ] | ( pVideoMemory[ i + 11 ] << 8 );
            pVBInfo->XG21_LVDSCapList[0].LVDSVFP = pVideoMemory[ i + 12 ] | ( pVideoMemory[ i + 13 ] << 8 );
            pVBInfo->XG21_LVDSCapList[0].LVDSHSYNC = pVideoMemory[ i + 14 ] | ( pVideoMemory[ i + 15 ] << 8 );
            pVBInfo->XG21_LVDSCapList[0].LVDSVSYNC = pVideoMemory[ i + 16 ] | ( pVideoMemory[ i + 17 ] << 8 );
            pVBInfo->XG21_LVDSCapList[0].VCLKData1 = pVideoMemory[ i + 18 ] ;
            pVBInfo->XG21_LVDSCapList[0].VCLKData2 = pVideoMemory[ i + 19 ] ;
            pVBInfo->XG21_LVDSCapList[0].PSC_S1 = pVideoMemory[ i + 20 ] ;
            pVBInfo->XG21_LVDSCapList[0].PSC_S2 = pVideoMemory[ i + 21 ] ;
            pVBInfo->XG21_LVDSCapList[0].PSC_S3 = pVideoMemory[ i + 22 ] ;
            pVBInfo->XG21_LVDSCapList[0].PSC_S4 = pVideoMemory[ i + 23 ] ;
            pVBInfo->XG21_LVDSCapList[0].PSC_S5 = pVideoMemory[ i + 24 ] ;
        }
        }
    }
}

/* --------------------------------------------------------------------- */
/* Function : XGINew_DDR1x_MRS_XG20 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_DDR1x_MRS_XG20( ULONG P3c4 , PVB_DEVICE_INFO pVBInfo)
{

    XGINew_SetReg1( P3c4 , 0x18 , 0x01 ) ;
    XGINew_SetReg1( P3c4 , 0x19 , 0x40 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;
    DelayUS( 60 ) ;

    XGINew_SetReg1( P3c4 , 0x18 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x19 , 0x40 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x80 ) ;
    DelayUS( 60 ) ;
    XGINew_SetReg1( P3c4 , 0x18 , pVBInfo->SR15[ 2 ][ XGINew_RAMType ] ) ;	/* SR18 */
    /* XGINew_SetReg1( P3c4 , 0x18 , 0x31 ) ; */
    XGINew_SetReg1( P3c4 , 0x19 , 0x01 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x03 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x83 ) ;
    DelayUS( 1000 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x03 ) ;
    DelayUS( 500 ) ;
    /* XGINew_SetReg1( P3c4 , 0x18 , 0x31 ) ; */
    XGINew_SetReg1( P3c4 , 0x18 , pVBInfo->SR15[ 2 ][ XGINew_RAMType ] ) ;	/* SR18 */
    XGINew_SetReg1( P3c4 , 0x19 , 0x00 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x03 ) ;
    XGINew_SetReg1( P3c4 , 0x16 , 0x83 ) ;
    XGINew_SetReg1( P3c4 , 0x1B , 0x00 ) ;
}

/* --------------------------------------------------------------------- */
/* Function : XGINew_SetDRAMModeRegister_XG20 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetDRAMModeRegister_XG20( PXGI_HW_DEVICE_INFO HwDeviceExtension )
{
    VB_DEVICE_INFO VBINF;
    PVB_DEVICE_INFO pVBInfo = &VBINF;
    pVBInfo->ROMAddr = HwDeviceExtension->pjVirtualRomBase ;
    pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress ;
    pVBInfo->BaseAddr = (ULONG)HwDeviceExtension->pjIOAddress ;
    pVBInfo->ISXPDOS = 0 ;

    pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14 ;
    pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24 ;
    pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10 ;
    pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e ;
    pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12 ;
    pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a ;
    pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16 ;
    pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17 ;
    pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18 ;
    pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19 ;
    pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A ;
    pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00 ;
    pVBInfo->Part1Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_04 ;
    pVBInfo->Part2Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_10 ;
    pVBInfo->Part3Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_12 ;
    pVBInfo->Part4Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 ;
    pVBInfo->Part5Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 + 2 ;

    InitTo330Pointer(HwDeviceExtension->jChipType,pVBInfo);

    ReadVBIOSTablData( HwDeviceExtension->jChipType , pVBInfo) ;

    if ( XGINew_GetXG20DRAMType( HwDeviceExtension, pVBInfo) == 0 )
        XGINew_DDR1x_MRS_XG20( pVBInfo->P3c4, pVBInfo ) ;
    else
        XGINew_DDR2_MRS_XG20( HwDeviceExtension , pVBInfo->P3c4 , pVBInfo ) ;

    XGINew_SetReg1( pVBInfo->P3c4 , 0x1B , 0x03 ) ;
}

void XGINew_SetDRAMModeRegister_XG27( PXGI_HW_DEVICE_INFO HwDeviceExtension )
{
    VB_DEVICE_INFO VBINF;
    PVB_DEVICE_INFO pVBInfo = &VBINF;
    pVBInfo->ROMAddr = HwDeviceExtension->pjVirtualRomBase ;
    pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress ;
    pVBInfo->BaseAddr = (ULONG)HwDeviceExtension->pjIOAddress ;
    pVBInfo->ISXPDOS = 0 ;

    pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14 ;
    pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24 ;
    pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10 ;
    pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e ;
    pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12 ;
    pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a ;
    pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16 ;
    pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17 ;
    pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18 ;
    pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19 ;
    pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A ;
    pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00 ;
    pVBInfo->Part1Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_04 ;
    pVBInfo->Part2Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_10 ;
    pVBInfo->Part3Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_12 ;
    pVBInfo->Part4Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 ;
    pVBInfo->Part5Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 + 2 ;

    InitTo330Pointer(HwDeviceExtension->jChipType,pVBInfo);

    ReadVBIOSTablData( HwDeviceExtension->jChipType , pVBInfo) ;

    if ( XGINew_GetXG20DRAMType( HwDeviceExtension, pVBInfo) == 0 )
        XGINew_DDR1x_MRS_XG20( pVBInfo->P3c4, pVBInfo ) ;
    else
        //XGINew_DDR2_MRS_XG27( HwDeviceExtension , pVBInfo->P3c4 , pVBInfo ) ;
        XGINew_DDRII_Bootup_XG27( HwDeviceExtension , pVBInfo->P3c4 , pVBInfo) ;

    //XGINew_SetReg1( pVBInfo->P3c4 , 0x1B , 0x03 ) ;
    XGINew_SetReg1( pVBInfo->P3c4 , 0x1B , pVBInfo->SR15[ 3 ][ XGINew_RAMType ] ) ;	/* SR1B */

}
/*
void XGINew_SetDRAMModeRegister_XG27( PXGI_HW_DEVICE_INFO HwDeviceExtension )
{
#ifndef LINUX_XF86
    UCHAR data ;
#endif
    VB_DEVICE_INFO VBINF;
    PVB_DEVICE_INFO pVBInfo = &VBINF;
    pVBInfo->ROMAddr = HwDeviceExtension->pjVirtualRomBase ;
    pVBInfo->FBAddr = HwDeviceExtension->pjVideoMemoryAddress ;
    pVBInfo->BaseAddr = HwDeviceExtension->pjIOAddress ;
    pVBInfo->ISXPDOS = 0 ;

    pVBInfo->P3c4 = pVBInfo->BaseAddr + 0x14 ;
    pVBInfo->P3d4 = pVBInfo->BaseAddr + 0x24 ;
    pVBInfo->P3c0 = pVBInfo->BaseAddr + 0x10 ;
    pVBInfo->P3ce = pVBInfo->BaseAddr + 0x1e ;
    pVBInfo->P3c2 = pVBInfo->BaseAddr + 0x12 ;
    pVBInfo->P3ca = pVBInfo->BaseAddr + 0x1a ;
    pVBInfo->P3c6 = pVBInfo->BaseAddr + 0x16 ;
    pVBInfo->P3c7 = pVBInfo->BaseAddr + 0x17 ;
    pVBInfo->P3c8 = pVBInfo->BaseAddr + 0x18 ;
    pVBInfo->P3c9 = pVBInfo->BaseAddr + 0x19 ;
    pVBInfo->P3da = pVBInfo->BaseAddr + 0x2A ;
    pVBInfo->Part0Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_00 ;
    pVBInfo->Part1Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_04 ;
    pVBInfo->Part2Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_10 ;
    pVBInfo->Part3Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_12 ;
    pVBInfo->Part4Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 ;
    pVBInfo->Part5Port = pVBInfo->BaseAddr + XGI_CRT2_PORT_14 + 2 ;

    InitTo330Pointer(HwDeviceExtension->jChipType,pVBInfo);

    ReadVBIOSTablData( HwDeviceExtension->jChipType , pVBInfo) ;

    if ( XGINew_GetXG20DRAMType( HwDeviceExtension, pVBInfo) == 0 )
        XGINew_DDR1x_MRS_XG20( pVBInfo->P3c4, pVBInfo ) ;
    else
        XGINew_DDR2_MRS_XG27( HwDeviceExtension , pVBInfo->P3c4 , pVBInfo ) ;

    XGINew_SetReg1( pVBInfo->P3c4 , 0x1B , 0x03 ) ;
}
*/
/* -------------------------------------------------------- */
/* Function : XGINew_ChkSenseStatus */
/* Input : */
/* Output : */
/* Description : */
/* -------------------------------------------------------- */
void XGINew_ChkSenseStatus ( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo)
{
    USHORT tempbx=0 , temp , tempcx , CR3CData;

    temp = XGINew_GetReg1( pVBInfo->P3d4 , 0x32 ) ;

    if ( temp & Monitor1Sense )
    	tempbx |= ActiveCRT1 ;
    if ( temp & LCDSense )
    	tempbx |= ActiveLCD ;
    if ( temp & Monitor2Sense )
    	tempbx |= ActiveCRT2 ;
    if ( temp & TVSense )
    {
    	tempbx |= ActiveTV ;
    	if ( temp & AVIDEOSense )
    	    tempbx |= ( ActiveAVideo << 8 );
    	if ( temp & SVIDEOSense )
    	    tempbx |= ( ActiveSVideo << 8 );
    	if ( temp & SCARTSense )
    	    tempbx |= ( ActiveSCART << 8 );
    	if ( temp & HiTVSense )
    	    tempbx |= ( ActiveHiTV << 8 );
    	if ( temp & YPbPrSense )
    	    tempbx |= ( ActiveYPbPr << 8 );
    }

    tempcx = XGINew_GetReg1( pVBInfo->P3d4 , 0x3d ) ;
    tempcx |= ( XGINew_GetReg1( pVBInfo->P3d4 , 0x3e ) << 8 ) ;

    if ( tempbx & tempcx )
    {
    	CR3CData = XGINew_GetReg1( pVBInfo->P3d4 , 0x3c ) ;
    	if ( !( CR3CData & DisplayDeviceFromCMOS ) )
    	{
    	    tempcx = 0x1FF0 ;
    	    if ( *pVBInfo->pSoftSetting & ModeSoftSetting )
    	    {
    	    	tempbx = 0x1FF0 ;
    	    }
    	}
    }
    else
    {
    	tempcx = 0x1FF0 ;
    	if ( *pVBInfo->pSoftSetting & ModeSoftSetting )
    	{
    	    tempbx = 0x1FF0 ;
    	}
    }

    tempbx &= tempcx ;
    XGINew_SetReg1( pVBInfo->P3d4, 0x3d , ( tempbx & 0x00FF ) ) ;
    XGINew_SetReg1( pVBInfo->P3d4, 0x3e , ( ( tempbx & 0xFF00 ) >> 8 )) ;
}
/* -------------------------------------------------------- */
/* Function : XGINew_SetModeScratch */
/* Input : */
/* Output : */
/* Description : */
/* -------------------------------------------------------- */
void XGINew_SetModeScratch ( PXGI_HW_DEVICE_INFO HwDeviceExtension , PVB_DEVICE_INFO pVBInfo )
{
    USHORT temp , tempcl = 0 , tempch = 0 , CR31Data , CR38Data;

    temp = XGINew_GetReg1( pVBInfo->P3d4 , 0x3d ) ;
    temp |= XGINew_GetReg1( pVBInfo->P3d4 , 0x3e ) << 8 ;
    temp |= ( XGINew_GetReg1( pVBInfo->P3d4 , 0x31 ) & ( DriverMode >> 8) ) << 8 ;

    if ( pVBInfo->IF_DEF_CRT2Monitor == 1)
    {
    	if ( temp & ActiveCRT2 )
    	   tempcl = SetCRT2ToRAMDAC ;
    }

    if ( temp & ActiveLCD )
    {
    	tempcl |= SetCRT2ToLCD ;
    	if  ( temp & DriverMode )
    	{
    	    if ( temp & ActiveTV )
    	    {
    	    	tempch = SetToLCDA | EnableDualEdge ;
    	    	temp ^= SetCRT2ToLCD ;

    	    	if ( ( temp >> 8 ) & ActiveAVideo )
    	    	    tempcl |= SetCRT2ToAVIDEO ;
    	    	if ( ( temp >> 8 ) & ActiveSVideo )
    	    	    tempcl |= SetCRT2ToSVIDEO ;
    	    	if ( ( temp >> 8 ) & ActiveSCART )
    	    	    tempcl |= SetCRT2ToSCART ;

    	    	if ( pVBInfo->IF_DEF_HiVision == 1 )
    	    	{
    	    	    if ( ( temp >> 8 ) & ActiveHiTV )
    	    	    tempcl |= SetCRT2ToHiVisionTV ;
    	    	}

    	    	if ( pVBInfo->IF_DEF_YPbPr == 1 )
    	    	{
    	    	    if ( ( temp >> 8 ) & ActiveYPbPr )
    	    	    tempch |= SetYPbPr ;
    	    	}
    	    }
    	}
    }
    else
    {
    	if ( ( temp >> 8 ) & ActiveAVideo )
    	   tempcl |= SetCRT2ToAVIDEO ;
    	if ( ( temp >> 8 ) & ActiveSVideo )
  	   tempcl |= SetCRT2ToSVIDEO ;
    	if ( ( temp >> 8 ) & ActiveSCART )
   	   tempcl |= SetCRT2ToSCART ;

   	if ( pVBInfo->IF_DEF_HiVision == 1 )
    	{
    	   if ( ( temp >> 8 ) & ActiveHiTV )
    	   tempcl |= SetCRT2ToHiVisionTV ;
    	}

    	if ( pVBInfo->IF_DEF_YPbPr == 1 )
    	{
    	   if ( ( temp >> 8 ) & ActiveYPbPr )
    	   tempch |= SetYPbPr ;
    	}
    }


    tempcl |= SetSimuScanMode ;
    if ( (!( temp & ActiveCRT1 )) && ( ( temp & ActiveLCD ) || ( temp & ActiveTV ) || ( temp & ActiveCRT2 ) ) )
       tempcl ^= ( SetSimuScanMode | SwitchToCRT2 ) ;
    if ( ( temp & ActiveLCD ) && ( temp & ActiveTV ) )
       tempcl ^= ( SetSimuScanMode | SwitchToCRT2 ) ;
    XGINew_SetReg1( pVBInfo->P3d4, 0x30 , tempcl ) ;

    CR31Data = XGINew_GetReg1( pVBInfo->P3d4 , 0x31 ) ;
    CR31Data &= ~( SetNotSimuMode >> 8 ) ;
    if ( !( temp & ActiveCRT1 ) )
        CR31Data |= ( SetNotSimuMode >> 8 ) ;
    CR31Data &= ~( DisableCRT2Display >> 8 ) ;
    if  (!( ( temp & ActiveLCD ) || ( temp & ActiveTV ) || ( temp & ActiveCRT2 ) ) )
        CR31Data |= ( DisableCRT2Display >> 8 ) ;
    XGINew_SetReg1( pVBInfo->P3d4, 0x31 , CR31Data ) ;

    CR38Data = XGINew_GetReg1( pVBInfo->P3d4 , 0x38 ) ;
    CR38Data &= ~SetYPbPr ;
    CR38Data |= tempch ;
    XGINew_SetReg1( pVBInfo->P3d4, 0x38 , CR38Data ) ;

}

/* -------------------------------------------------------- */
/* Function : XGINew_GetXG21Sense */
/* Input : */
/* Output : */
/* Description : */
/* -------------------------------------------------------- */
void XGINew_GetXG21Sense(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO pVBInfo)
{
    UCHAR Temp;
    PUCHAR  volatile pVideoMemory = ( PUCHAR )pVBInfo->ROMAddr ;

    pVBInfo->IF_DEF_LVDS = 0 ;

#ifdef WIN2000
   pVBInfo->IF_DEF_CH7007 = 0 ;
    if ( ( pVideoMemory[ 0x65 ] & 0x02 ) )			/* For XG21 CH7007 */
    {
        /* VideoDebugPrint((0, "ReadVBIOSTablData: pVideoMemory[ 0x65 ] =%x\n",pVideoMemory[ 0x65 ])); */
        pVBInfo->IF_DEF_CH7007 = 1 ;                            /* [Billy] 07/05/03 */
        XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x38 , ~0xE0 , 0x60 ) ; /* CH7007 on chip */
    }
    else
#endif
#if 1
    if (( pVideoMemory[ 0x65 ] & 0x01 ) )			/* For XG21 LVDS */
    {
        pVBInfo->IF_DEF_LVDS = 1 ;
        XGINew_SetRegOR( pVBInfo->P3d4 , 0x32 , LCDSense ) ;
        XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x38 , ~0xE0 , 0xC0 ) ; /* LVDS on chip */
    }
    else
    {
#endif
        XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x4A , ~0x03 , 0x03 ) ; /* Enable GPIOA/B read  */
        Temp = XGINew_GetReg1( pVBInfo->P3d4 , 0x48 ) & 0xC0;
        if ( Temp == 0xC0 )
        {								/* DVI & DVO GPIOA/B pull high */
          XGINew_SenseLCD( HwDeviceExtension, pVBInfo ) ;
          XGINew_SetRegOR( pVBInfo->P3d4 , 0x32 , LCDSense ) ;
          XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x4A , ~0x20 , 0x20 ) ;   /* Enable read GPIOF */
          Temp = XGINew_GetReg1( pVBInfo->P3d4 , 0x48 ) & 0x04 ;
          if ( !Temp )
            XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x38 , ~0xE0 , 0x80 ) ; /* TMDS on chip */
          else
            XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x38 , ~0xE0 , 0xA0 ) ; /* Only DVO on chip */
          XGINew_SetRegAND( pVBInfo->P3d4 , 0x4A , ~0x20 ) ;	    /* Disable read GPIOF */
        }
#if 1
    }
#endif
}

/* -------------------------------------------------------- */
/* Function : XGINew_GetXG27Sense */
/* Input : */
/* Output : */
/* Description : */
/* -------------------------------------------------------- */
void XGINew_GetXG27Sense(PXGI_HW_DEVICE_INFO HwDeviceExtension, PVB_DEVICE_INFO pVBInfo)
{
    UCHAR Temp,bCR4A;

     pVBInfo->IF_DEF_LVDS = 0 ;
     bCR4A = XGINew_GetReg1( pVBInfo->P3d4 , 0x4A ) ;
     XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x4A , ~0x07 , 0x07 ) ; /* Enable GPIOA/B/C read  */
     Temp = XGINew_GetReg1( pVBInfo->P3d4 , 0x48 ) & 0x07;
     XGINew_SetReg1( pVBInfo->P3d4, 0x4A , bCR4A ) ;

     if ( Temp <= 0x02 )
     {
         pVBInfo->IF_DEF_LVDS = 1 ;
         XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x38 , ~0xE0 , 0xC0 ) ; /* LVDS setting */
         XGINew_SetReg1( pVBInfo->P3d4, 0x30 , 0x21 ) ;
     }
     else
     {
       XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x38 , ~0xE0 , 0xA0 ) ; /* TMDS/DVO setting */
     }
     XGINew_SetRegOR( pVBInfo->P3d4 , 0x32 , LCDSense ) ;

}

UCHAR GetXG21FPBits(PVB_DEVICE_INFO pVBInfo)
{
    UCHAR CR38,CR4A,temp;

    CR4A = XGINew_GetReg1( pVBInfo->P3d4 , 0x4A ) ;
    XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x4A , ~0x10 , 0x10 ) ; /* enable GPIOE read */
    CR38 = XGINew_GetReg1( pVBInfo->P3d4 , 0x38 ) ;
    temp =0;
    if ( ( CR38 & 0xE0 ) > 0x80 )
    {
        temp = XGINew_GetReg1( pVBInfo->P3d4 , 0x48 ) ;
        temp &= 0x08;
        temp >>= 3;
    }

    XGINew_SetReg1( pVBInfo->P3d4, 0x4A , CR4A ) ;

    return temp;
}

UCHAR GetXG27FPBits(PVB_DEVICE_INFO pVBInfo)
{
    UCHAR CR4A,temp;

    CR4A = XGINew_GetReg1( pVBInfo->P3d4 , 0x4A ) ;
    XGINew_SetRegANDOR( pVBInfo->P3d4 , 0x4A , ~0x03 , 0x03 ) ; /* enable GPIOA/B/C read */
    temp = XGINew_GetReg1( pVBInfo->P3d4 , 0x48 ) ;
    if ( temp <= 2 )
    {
    	temp &= 0x03;
    }
    else
    {
    	temp = ((temp&0x04)>>1) || ((~temp)&0x01);
    }
    XGINew_SetReg1( pVBInfo->P3d4, 0x4A , CR4A ) ;

    return temp;
}

