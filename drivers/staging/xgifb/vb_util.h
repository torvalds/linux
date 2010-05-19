#ifndef _VBUTIL_
#define _VBUTIL_
extern   void     NewDelaySeconds( int );
extern   void     Newdebugcode( UCHAR );
extern   void     XGINew_SetReg1(ULONG, USHORT, USHORT);
extern   void     XGINew_SetReg3(ULONG, USHORT);
extern   UCHAR    XGINew_GetReg1(ULONG, USHORT);
extern   UCHAR    XGINew_GetReg2(ULONG);
extern   void     XGINew_SetReg4(ULONG, ULONG);
extern   ULONG    XGINew_GetReg3(ULONG);
extern   void     XGINew_SetRegOR(ULONG Port,USHORT Index,USHORT DataOR);
extern   void     XGINew_SetRegAND(ULONG Port,USHORT Index,USHORT DataAND);
extern   void     XGINew_SetRegANDOR(ULONG Port,USHORT Index,USHORT DataAND,USHORT DataOR);
#endif

