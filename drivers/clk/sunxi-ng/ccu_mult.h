#ifndef _CCU_MULT_H_
#define _CCU_MULT_H_

struct _ccu_mult {
	u8	shift;
	u8	width;
};

#define _SUNXI_CCU_MULT(_shift, _width)		\
	{					\
		.shift	= _shift,		\
		.width	= _width,		\
	}

#endif /* _CCU_MULT_H_ */
