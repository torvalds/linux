#include "vb_def.h"
#include "vgatypes.h"
#include "vb_struct.h"

#include "XGIfb.h"
#include <asm/io.h>
#include <linux/types.h>

#include "vb_util.h"

void xgifb_reg_set(unsigned long port, u8 index, u8 data)
{
	outb(index, port);
	outb(data, port + 1);
}

u8 xgifb_reg_get(unsigned long port, u8 index)
{
	u8 data;

	outb(index, port);
	data = inb(port + 1);
	return data;
}

void xgifb_reg_and_or(unsigned long Port, u8 Index,
		unsigned DataAND, unsigned DataOR)
{
	u8 temp;

	temp = xgifb_reg_get(Port, Index); /* XGINew_Part1Port index 02 */
	temp = (temp & (DataAND)) | DataOR;
	xgifb_reg_set(Port, Index, temp);
}

void xgifb_reg_and(unsigned long Port, u8 Index, unsigned DataAND)
{
	u8 temp;

	temp = xgifb_reg_get(Port, Index); /* XGINew_Part1Port index 02 */
	temp &= DataAND;
	xgifb_reg_set(Port, Index, temp);
}

void xgifb_reg_or(unsigned long Port, u8 Index, unsigned DataOR)
{
	u8 temp;

	temp = xgifb_reg_get(Port, Index); /* XGINew_Part1Port index 02 */
	temp |= DataOR;
	xgifb_reg_set(Port, Index, temp);
}
