#ifndef _VBUTIL_
#define _VBUTIL_
extern void xgifb_reg_set(unsigned long, unsigned short, unsigned short);
extern unsigned char xgifb_reg_get(unsigned long, unsigned short);
extern   void     XGINew_SetRegOR(unsigned long Port,unsigned short Index,unsigned short DataOR);
extern   void     XGINew_SetRegAND(unsigned long Port,unsigned short Index,unsigned short DataAND);
extern   void     XGINew_SetRegANDOR(unsigned long Port,unsigned short Index,unsigned short DataAND,unsigned short DataOR);
#endif

