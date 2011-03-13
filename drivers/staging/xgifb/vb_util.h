#ifndef _VBUTIL_
#define _VBUTIL_
extern void xgifb_reg_set(unsigned long, unsigned short, unsigned short);
extern unsigned char xgifb_reg_get(unsigned long, unsigned short);
extern void xgifb_reg_or(unsigned long, unsigned short, unsigned short);
extern void xgifb_reg_and(unsigned long, unsigned short, unsigned short);
extern void xgifb_reg_and_or(unsigned long, unsigned short, unsigned short, unsigned short);
#endif

