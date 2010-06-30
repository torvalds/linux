#include "osdef.h"
#include "vb_def.h"
#include "vgatypes.h"
#include "vb_struct.h"

#ifdef LINUX_KERNEL
#include "XGIfb.h"
#include <asm/io.h>
#include <linux/types.h>
#endif

#ifdef TC
#include <stdio.h>
#include <string.h>
#include <conio.h>
#include <dos.h>
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

#ifdef LINUX_XF86
#include "xf86.h"
#include "xf86PciInfo.h"
#include "xgi.h"
#include "xgi_regs.h"
#endif




void XGINew_SetReg1( ULONG , USHORT , USHORT ) ;
void XGINew_SetReg2( ULONG , USHORT , USHORT ) ;
void XGINew_SetReg3( ULONG , USHORT ) ;
void XGINew_SetReg4( ULONG , ULONG ) ;
UCHAR XGINew_GetReg1( ULONG , USHORT) ;
UCHAR XGINew_GetReg2( ULONG ) ;
ULONG XGINew_GetReg3( ULONG ) ;
void XGINew_ClearDAC( PUCHAR ) ;
void     XGINew_SetRegANDOR(ULONG Port,USHORT Index,USHORT DataAND,USHORT DataOR);
void     XGINew_SetRegOR(ULONG Port,USHORT Index,USHORT DataOR);
void     XGINew_SetRegAND(ULONG Port,USHORT Index,USHORT DataAND);


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetReg1 */
/* Input : */
/* Output : */
/* Description : SR CRTC GR */
/* --------------------------------------------------------------------- */
void XGINew_SetReg1( ULONG port , USHORT index , USHORT data )
{
#ifdef LINUX_XF86
    OutPortByte( ( PUCHAR )(ULONG)port , index ) ;
    OutPortByte( ( PUCHAR )(ULONG)port + 1 , data ) ;
#else
    OutPortByte( port , index ) ;
    OutPortByte( port + 1 , data ) ;
#endif
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetReg2 */
/* Input : */
/* Output : */
/* Description : AR( 3C0 ) */
/* --------------------------------------------------------------------- */
/*void XGINew_SetReg2( ULONG port , USHORT index , USHORT data )
{
    InPortByte( ( PUCHAR )port + 0x3da - 0x3c0 ) ;
    OutPortByte( XGINew_P3c0 , index ) ;
    OutPortByte( XGINew_P3c0 , data ) ;
    OutPortByte( XGINew_P3c0 , 0x20 ) ;
}*/


/* --------------------------------------------------------------------- */
/* Function : */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetReg3( ULONG port , USHORT data )
{
    OutPortByte( port , data ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetReg4 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetReg4( ULONG port , ULONG data )
{
    OutPortLong( port , data ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_GetReg1 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
UCHAR XGINew_GetReg1( ULONG port , USHORT index )
{
    UCHAR data ;

#ifdef LINUX_XF86
    OutPortByte( ( PUCHAR )(ULONG)port , index ) ;
    data = InPortByte( ( PUCHAR )(ULONG)port + 1 ) ;
#else
    OutPortByte( port , index ) ;
    data = InPortByte( port + 1 ) ;
#endif

    return( data ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_GetReg2 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
UCHAR XGINew_GetReg2( ULONG port )
{
    UCHAR data ;

    data = InPortByte( port ) ;

    return( data ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_GetReg3 */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
ULONG XGINew_GetReg3( ULONG port )
{
    ULONG data ;

    data = InPortLong( port ) ;

    return( data ) ;
}



/* --------------------------------------------------------------------- */
/* Function : XGINew_SetRegANDOR */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetRegANDOR( ULONG Port , USHORT Index , USHORT DataAND , USHORT DataOR )
{
    USHORT temp ;

    temp = XGINew_GetReg1( Port , Index ) ;		/* XGINew_Part1Port index 02 */
    temp = ( temp & ( DataAND ) ) | DataOR ;
    XGINew_SetReg1( Port , Index , temp ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetRegAND */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetRegAND(ULONG Port,USHORT Index,USHORT DataAND)
{
    USHORT temp ;

    temp = XGINew_GetReg1( Port , Index ) ;	/* XGINew_Part1Port index 02 */
    temp &= DataAND ;
    XGINew_SetReg1( Port , Index , temp ) ;
}


/* --------------------------------------------------------------------- */
/* Function : XGINew_SetRegOR */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void XGINew_SetRegOR( ULONG Port , USHORT Index , USHORT DataOR )
{
    USHORT temp ;

    temp = XGINew_GetReg1( Port , Index ) ;	/* XGINew_Part1Port index 02 */
    temp |= DataOR ;
    XGINew_SetReg1( Port , Index , temp ) ;
}


/* --------------------------------------------------------------------- */
/* Function : NewDelaySecond */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void NewDelaySeconds( int seconds )
{
#ifdef WIN2000
    int j ;
#endif
    int i ;


    for( i = 0 ; i < seconds ; i++ )
    {
#ifdef TC
        delay( 1000 ) ;
#endif

#ifdef WIN2000

        for ( j = 0 ; j < 20000 ; j++ )
            VideoPortStallExecution( 50 ) ;
#endif

#ifdef WINCE_HEADER
#endif

#ifdef LINUX_KERNEL
#endif
    }
}


/* --------------------------------------------------------------------- */
/* Function : Newdebugcode */
/* Input : */
/* Output : */
/* Description : */
/* --------------------------------------------------------------------- */
void Newdebugcode( UCHAR code )
{
//    OutPortByte ( 0x80 , code ) ;
    /* OutPortByte ( 0x300 , code ) ; */
    /* NewDelaySeconds( 0x3 ) ; */
}



