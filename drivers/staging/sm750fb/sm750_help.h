#ifndef LYNX_HELP_H__
#define LYNX_HELP_H__
/*****************************************************************************\
 *                                FIELD MACROS                               *
\*****************************************************************************/

#define _LSB(f)             (0 ? f)
#define _MSB(f)             (1 ? f)
#define _COUNT(f)           (_MSB(f) - _LSB(f) + 1)

#define RAW_MASK(f)         (0xFFFFFFFF >> (32 - _COUNT(f)))
#define GET_MASK(f)         (RAW_MASK(f) << _LSB(f))
#define GET_FIELD(d,f)      (((d) >> _LSB(f)) & RAW_MASK(f))
#define TEST_FIELD(d,f,v)   (GET_FIELD(d,f) == f ## _ ## v)
#define SET_FIELD(d,f,v)    (((d) & ~GET_MASK(f)) | \
                            (((f ## _ ## v) & RAW_MASK(f)) << _LSB(f)))
#define SET_FIELDV(d,f,v)   (((d) & ~GET_MASK(f)) | \
                            (((v) & RAW_MASK(f)) << _LSB(f)))


////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Internal macros                                                            //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#define _F_START(f)             (0 ? f)
#define _F_END(f)               (1 ? f)
#define _F_SIZE(f)              (1 + _F_END(f) - _F_START(f))
#define _F_MASK(f)              (((1 << _F_SIZE(f)) - 1) << _F_START(f))
#define _F_NORMALIZE(v, f)      (((v) & _F_MASK(f)) >> _F_START(f))
#define _F_DENORMALIZE(v, f)    (((v) << _F_START(f)) & _F_MASK(f))


////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Global macros                                                              //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

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
    ~ _F_MASK(reg ## _ ## field) \
)


////////////////////////////////////////////////////////////////////////////////
//                                                                            //
// Field Macros                                                               //
//                                                                            //
////////////////////////////////////////////////////////////////////////////////

#define FIELD_START(field)              (0 ? field)
#define FIELD_END(field)                (1 ? field)
#define FIELD_SIZE(field)               (1 + FIELD_END(field) - FIELD_START(field))
#define FIELD_MASK(field)               (((1 << (FIELD_SIZE(field)-1)) | ((1 << (FIELD_SIZE(field)-1)) - 1)) << FIELD_START(field))
#define FIELD_NORMALIZE(reg, field)     (((reg) & FIELD_MASK(field)) >> FIELD_START(field))
#define FIELD_DENORMALIZE(field, value) (((value) << FIELD_START(field)) & FIELD_MASK(field))

#define FIELD_INIT(reg, field, value)   FIELD_DENORMALIZE(reg ## _ ## field, \
                                                          reg ## _ ## field ## _ ## value)
#define FIELD_INIT_VAL(reg, field, value) \
                                        (FIELD_DENORMALIZE(reg ## _ ## field, value))
#define FIELD_VAL_SET(x, r, f, v)       x = x & ~FIELD_MASK(r ## _ ## f) \
                                              | FIELD_DENORMALIZE(r ## _ ## f, r ## _ ## f ## _ ## v)

#define RGB(r, g, b) \
( \
    (unsigned long) (((r) << 16) | ((g) << 8) | (b)) \
)

#define RGB16(r, g, b) \
( \
    (unsigned short) ((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | (((b) & 0xF8) >> 3)) \
)

static inline unsigned int absDiff(unsigned int a,unsigned int b)
{
	if(a<b)
		return b-a;
	else
		return a-b;
}

/* n / d + 1 / 2 = (2n + d) / 2d */
#define roundedDiv(num,denom)	((2 * (num) + (denom)) / (2 * (denom)))
#define MB(x) ((x)<<20)
#define KB(x) ((x)<<10)
#define MHz(x) ((x) * 1000000)




#endif
