#include "vb_def.h"
#include "vgatypes.h"
#include "vb_struct.h"

#include "XGIfb.h"
#include <asm/io.h>
#include <linux/types.h>

#include "vb_util.h"

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
