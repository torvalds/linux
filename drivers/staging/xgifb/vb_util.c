#include "vb_def.h"
#include "vgatypes.h"
#include "vb_struct.h"

#include "XGIfb.h"
#include <asm/io.h>
#include <linux/types.h>

void XGINew_SetReg1(unsigned long, unsigned short, unsigned short);
void XGINew_SetReg2(unsigned long, unsigned short, unsigned short);
void XGINew_SetReg3(unsigned long, unsigned short);
void XGINew_SetReg4(unsigned long, unsigned long);
unsigned char XGINew_GetReg1(unsigned long, unsigned short);
unsigned char XGINew_GetReg2(unsigned long);
unsigned long XGINew_GetReg3(unsigned long);
void XGINew_ClearDAC(unsigned char *);
void XGINew_SetRegANDOR(unsigned long Port, unsigned short Index,
		unsigned short DataAND, unsigned short DataOR);
void XGINew_SetRegOR(unsigned long Port, unsigned short Index,
		unsigned short DataOR);
void XGINew_SetRegAND(unsigned long Port, unsigned short Index,
		unsigned short DataAND);

/* --------------------------------------------------------------------- */
/* Function : XGINew_SetReg1 */
/* Input : */
/* Output : */
/* Description : SR CRTC GR */
/* --------------------------------------------------------------------- */
void XGINew_SetReg1(unsigned long port, unsigned short index,
		unsigned short data)
{
	outb(index, port);
	outb(data, port + 1);
}

/* --------------------------------------------------------------------- */
/* Function : XGINew_SetReg2 */
/* Input : */
/* Output : */
/* Description : AR( 3C0 ) */
/* --------------------------------------------------------------------- */
/*
void XGINew_SetReg2(unsigned long port, unsigned short index, unsigned short data)
{
	InPortByte((P unsigned char)port + 0x3da - 0x3c0) ;
	OutPortByte(XGINew_P3c0, index);
	OutPortByte(XGINew_P3c0, data);
	OutPortByte(XGINew_P3c0, 0x20);
}
*/

void XGINew_SetReg3(unsigned long port, unsigned short data)
{
	outb(data, port);
}

void XGINew_SetReg4(unsigned long port, unsigned long data)
{
	outl(data, port);
}

unsigned char XGINew_GetReg1(unsigned long port, unsigned short index)
{
	unsigned char data;

	outb(index, port);
	data = inb(port + 1);
	return data;
}

unsigned char XGINew_GetReg2(unsigned long port)
{
	unsigned char data;

	data = inb(port);

	return data;
}

unsigned long XGINew_GetReg3(unsigned long port)
{
	unsigned long data;

	data = inl(port);

	return data;
}

void XGINew_SetRegANDOR(unsigned long Port, unsigned short Index,
		unsigned short DataAND, unsigned short DataOR)
{
	unsigned short temp;

	temp = XGINew_GetReg1(Port, Index); /* XGINew_Part1Port index 02 */
	temp = (temp & (DataAND)) | DataOR;
	XGINew_SetReg1(Port, Index, temp);
}

void XGINew_SetRegAND(unsigned long Port, unsigned short Index,
		unsigned short DataAND)
{
	unsigned short temp;

	temp = XGINew_GetReg1(Port, Index); /* XGINew_Part1Port index 02 */
	temp &= DataAND;
	XGINew_SetReg1(Port, Index, temp);
}

void XGINew_SetRegOR(unsigned long Port, unsigned short Index,
		unsigned short DataOR)
{
	unsigned short temp;

	temp = XGINew_GetReg1(Port, Index); /* XGINew_Part1Port index 02 */
	temp |= DataOR;
	XGINew_SetReg1(Port, Index, temp);
}

#if 0
void NewDelaySeconds(int seconds)
{
	int i;

	for (i = 0; i < seconds; i++) {

	}
}

void Newdebugcode(unsigned char code)
{
	/* OutPortByte(0x80, code); */
	/* OutPortByte(0x300, code); */
	/* NewDelaySeconds(0x3); */
}
#endif
