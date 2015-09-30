#ifndef LYNX_HELP_H__
#define LYNX_HELP_H__

/* Internal macros */
#define _F_START(f)             (0 ? f)
#define _F_END(f)               (1 ? f)
#define _F_SIZE(f)              (1 + _F_END(f) - _F_START(f))
#define _F_MASK(f)              (((1 << _F_SIZE(f)) - 1) << _F_START(f))
#define _F_NORMALIZE(v, f)      (((v) & _F_MASK(f)) >> _F_START(f))
#define _F_DENORMALIZE(v, f)    (((v) << _F_START(f)) & _F_MASK(f))

/* Global macros */
#define FIELD_GET(x, reg, field) \
( \
	_F_NORMALIZE((x), reg ## _ ## field) \
)

#define FIELD_SET(x, reg, field, value) \
( \
	(x & ~_F_MASK(reg ## _ ## field)) \
	| _F_DENORMALIZE(reg ## _ ## field ## _ ## value, reg ## _ ## field) \
)

#define FIELD_VALUE(x, reg, field, value) \
( \
	(x & ~_F_MASK(reg ## _ ## field)) \
	| _F_DENORMALIZE(value, reg ## _ ## field) \
)

#define FIELD_CLEAR(reg, field) \
( \
	~_F_MASK(reg ## _ ## field) \
)

/* Field Macros */
#define FIELD_START(field)              (0 ? field)
#define FIELD_END(field)                (1 ? field)
#define FIELD_SIZE(field)               (1 + FIELD_END(field) - FIELD_START(field))
#define FIELD_MASK(field)               (((1 << (FIELD_SIZE(field)-1)) | ((1 << (FIELD_SIZE(field)-1)) - 1)) << FIELD_START(field))

static inline unsigned int absDiff(unsigned int a, unsigned int b)
{
	if (a < b)
		return b-a;
	else
		return a-b;
}

/* n / d + 1 / 2 = (2n + d) / 2d */
#define roundedDiv(num, denom)	((2 * (num) + (denom)) / (2 * (denom)))
#define MHz(x) ((x) * 1000000)




#endif
