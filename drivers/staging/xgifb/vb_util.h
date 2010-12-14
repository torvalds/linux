#ifndef _VBUTIL_
#define _VBUTIL_
extern   void     NewDelaySeconds( int );
extern   void     Newdebugcode(unsigned char);
extern   void     XGINew_SetReg1(unsigned long, unsigned short, unsigned short);
extern   void     XGINew_SetReg3(unsigned long, unsigned short);
extern    unsigned char     XGINew_GetReg1(unsigned long, unsigned short);
extern    unsigned char     XGINew_GetReg2(unsigned long);
extern   void     XGINew_SetReg4(unsigned long, unsigned long);
extern   unsigned long    XGINew_GetReg3(unsigned long);
extern   void     XGINew_SetRegOR(unsigned long Port,unsigned short Index,unsigned short DataOR);
extern   void     XGINew_SetRegAND(unsigned long Port,unsigned short Index,unsigned short DataAND);
extern   void     XGINew_SetRegANDOR(unsigned long Port,unsigned short Index,unsigned short DataAND,unsigned short DataOR);
#endif

