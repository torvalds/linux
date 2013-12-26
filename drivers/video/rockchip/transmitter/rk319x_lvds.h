#include<linux/rk_screen.h>

#define ENABLE 16

#define GRF_SOC_CON4   		0x0070
#define LVDS_SEL_LCDC(x)	((((x)&1)<<2)|(1<<(2+ENABLE)))
#define LVDS_MODE(x)		((((x)&1)<<3)|(1<<(3+ENABLE)))
#define LVDS_OUTPUT_FORMAT(x)   ((((x)&3)<<4)|(3<<(4+ENABLE)))
#define LVDS_MSBSEL(x)		((((x)&1)<<6)|(1<<(6+ENABLE)))

#define GRF_SOC_CON8		0x013c
#define LVDS_ENABLE(x) 		((((x)&1)<<7)|(1<<(7+ENABLE)))

#define MIPIPHY_REG0		0x0000
#define MIPIPHY_REG3		0x000c
#define MIPIPHY_REG4		0x0010
#define MIPIPHY_REGE0		0x0380
#define MIPIPHY_REGEA		0x03A8
#define MIPIPHY_REGE2		0x0388
#define MIPIPHY_REGE7		0x039C



enum{
	OUT_DISABLE=0,
	OUT_ENABLE,
};

