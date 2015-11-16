#ifndef _TSC200X_CORE_H
#define _TSC200X_CORE_H

/* control byte 1 */
#define TSC200X_CMD			0x80
#define TSC200X_CMD_NORMAL		0x00
#define TSC200X_CMD_STOP		0x01
#define TSC200X_CMD_12BIT		0x04

/* control byte 0 */
#define TSC200X_REG_READ		0x01 /* R/W access */
#define TSC200X_REG_PND0		0x02 /* Power Not Down Control */
#define TSC200X_REG_X			(0x0 << 3)
#define TSC200X_REG_Y			(0x1 << 3)
#define TSC200X_REG_Z1			(0x2 << 3)
#define TSC200X_REG_Z2			(0x3 << 3)
#define TSC200X_REG_AUX			(0x4 << 3)
#define TSC200X_REG_TEMP1		(0x5 << 3)
#define TSC200X_REG_TEMP2		(0x6 << 3)
#define TSC200X_REG_STATUS		(0x7 << 3)
#define TSC200X_REG_AUX_HIGH		(0x8 << 3)
#define TSC200X_REG_AUX_LOW		(0x9 << 3)
#define TSC200X_REG_TEMP_HIGH		(0xA << 3)
#define TSC200X_REG_TEMP_LOW		(0xB << 3)
#define TSC200X_REG_CFR0		(0xC << 3)
#define TSC200X_REG_CFR1		(0xD << 3)
#define TSC200X_REG_CFR2		(0xE << 3)
#define TSC200X_REG_CONV_FUNC		(0xF << 3)

/* configuration register 0 */
#define TSC200X_CFR0_PRECHARGE_276US	0x0040
#define TSC200X_CFR0_STABTIME_1MS	0x0300
#define TSC200X_CFR0_CLOCK_1MHZ		0x1000
#define TSC200X_CFR0_RESOLUTION12	0x2000
#define TSC200X_CFR0_PENMODE		0x8000
#define TSC200X_CFR0_INITVALUE		(TSC200X_CFR0_STABTIME_1MS    | \
					 TSC200X_CFR0_CLOCK_1MHZ      | \
					 TSC200X_CFR0_RESOLUTION12    | \
					 TSC200X_CFR0_PRECHARGE_276US | \
					 TSC200X_CFR0_PENMODE)

/* bits common to both read and write of configuration register 0 */
#define	TSC200X_CFR0_RW_MASK		0x3fff

/* configuration register 1 */
#define TSC200X_CFR1_BATCHDELAY_4MS	0x0003
#define TSC200X_CFR1_INITVALUE		TSC200X_CFR1_BATCHDELAY_4MS

/* configuration register 2 */
#define TSC200X_CFR2_MAVE_Z		0x0004
#define TSC200X_CFR2_MAVE_Y		0x0008
#define TSC200X_CFR2_MAVE_X		0x0010
#define TSC200X_CFR2_AVG_7		0x0800
#define TSC200X_CFR2_MEDIUM_15		0x3000
#define TSC200X_CFR2_INITVALUE		(TSC200X_CFR2_MAVE_X	| \
					 TSC200X_CFR2_MAVE_Y	| \
					 TSC200X_CFR2_MAVE_Z	| \
					 TSC200X_CFR2_MEDIUM_15	| \
					 TSC200X_CFR2_AVG_7)

#define MAX_12BIT			0xfff
#define TSC200X_DEF_X_FUZZ		4
#define TSC200X_DEF_Y_FUZZ		8
#define TSC200X_DEF_P_FUZZ		2
#define TSC200X_DEF_RESISTOR		280

#define TSC2005_SPI_MAX_SPEED_HZ	10000000
#define TSC200X_PENUP_TIME_MS		40

extern const struct regmap_config tsc200x_regmap_config;
extern const struct dev_pm_ops tsc200x_pm_ops;

int tsc200x_probe(struct device *dev, int irq, __u16 bustype,
		  struct regmap *regmap,
		  int (*tsc200x_cmd)(struct device *dev, u8 cmd));
int tsc200x_remove(struct device *dev);

#endif
