#ifndef _VBUTIL_
#define _VBUTIL_
static inline void xgifb_reg_set(unsigned long port, u8 index, u8 data)
{
	outb(index, port);
	outb(data, port + 1);
}

static inline u8 xgifb_reg_get(unsigned long port, u8 index)
{
	outb(index, port);
	return inb(port + 1);
}

static inline void xgifb_reg_and_or(unsigned long port, u8 index,
				    unsigned data_and, unsigned data_or)
{
	u8 temp;

	temp = xgifb_reg_get(port, index);
	temp = (u8) ((temp & data_and) | data_or);
	xgifb_reg_set(port, index, temp);
}

static inline void xgifb_reg_and(unsigned long port, u8 index, unsigned data_and)
{
	u8 temp;

	temp = xgifb_reg_get(port, index);
	temp = (u8) (temp & data_and);
	xgifb_reg_set(port, index, temp);
}

static inline void xgifb_reg_or(unsigned long port, u8 index, unsigned data_or)
{
	u8 temp;

	temp = xgifb_reg_get(port, index);
	temp |= data_or;
	xgifb_reg_set(port, index, temp);
}
#endif

