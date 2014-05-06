/************************************************************************
File  : Clock H/w specific Information

Author: Pankaj Dev <pankaj.dev@st.com>

Copyright (C) 2014 STMicroelectronics
************************************************************************/

#ifndef __CLKGEN_INFO_H
#define __CLKGEN_INFO_H

struct clkgen_field {
	unsigned int offset;
	unsigned int mask;
	unsigned int shift;
};

static inline unsigned long clkgen_read(void __iomem	*base,
					  struct clkgen_field *field)
{
	return (readl(base + field->offset) >> field->shift) & field->mask;
}


static inline void clkgen_write(void __iomem *base, struct clkgen_field *field,
				  unsigned long val)
{
	writel((readl(base + field->offset) &
	       ~(field->mask << field->shift)) | (val << field->shift),
	       base + field->offset);

	return;
}

#define CLKGEN_FIELD(_offset, _mask, _shift) {		\
				.offset	= _offset,	\
				.mask	= _mask,	\
				.shift	= _shift,	\
				}

#define CLKGEN_READ(pll, field) clkgen_read(pll->regs_base, \
		&pll->data->field)

#define CLKGEN_WRITE(pll, field, val) clkgen_write(pll->regs_base, \
		&pll->data->field, val)

#endif /*__CLKGEN_INFO_H*/

