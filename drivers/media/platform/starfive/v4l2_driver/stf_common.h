/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2021 StarFive Technology Co., Ltd.
 */
#ifndef STF_COMMON_H
#define STF_COMMON_H

#include <linux/kern_levels.h>

// #define STF_DEBUG

// #define USE_CSIDPHY_ONE_CLK_MODE 1

enum {
	ST_DVP = 0x0001,
	ST_CSIPHY = 0x0002,
	ST_CSI = 0x0004,
	ST_ISP = 0x0008,
	ST_VIN = 0x0010,
	ST_VIDEO = 0x0020,
	ST_CAMSS = 0x0040,
	ST_SENSOR = 0x0080,
};

enum {
	ST_NONE = 0x00,
	ST_ERR = 0x01,
	ST_WARN = 0x02,
	ST_INFO = 0x03,
	ST_DEBUG = 0x04,
};

extern unsigned int stdbg_level;
extern unsigned int stdbg_mask;

#define ST_MODULE2STRING(__module) ({ \
	char *__str; \
	\
	switch (__module) { \
	case ST_DVP: \
		__str = "st_dvp"; \
		break; \
	case ST_CSIPHY: \
		__str = "st_csiphy"; \
		break; \
	case ST_CSI: \
		__str = "st_csi"; \
		break; \
	case ST_ISP: \
		__str = "st_isp"; \
		break; \
	case ST_VIN: \
		__str = "st_vin"; \
		break; \
	case ST_VIDEO: \
		__str = "st_video"; \
		break; \
	case ST_CAMSS: \
		__str = "st_camss"; \
		break; \
	case ST_SENSOR: \
		__str = "st_sensor"; \
		break; \
	default: \
		__str = "unknow"; \
		break; \
	} \
	\
	__str; \
	})

#define st_debug(module, __fmt, arg...)	\
	do { \
		if (stdbg_level > ST_INFO) { \
			if (stdbg_mask & module)  \
				pr_err("[%s] debug: " __fmt, \
						ST_MODULE2STRING(module), \
						## arg); \
		} \
	} while (0)

#define st_info(module, __fmt, arg...)	\
	do { \
		if (stdbg_level > ST_WARN) { \
			if (stdbg_mask & module)  \
				pr_err("[%s] info: " __fmt, \
						ST_MODULE2STRING(module), \
						## arg); \
		} \
	} while (0)

#define st_warn(module, __fmt, arg...)	\
	do { \
		if (stdbg_level > ST_ERR) { \
			if (stdbg_mask & module)  \
				pr_err("[%s] warn: " __fmt, \
						ST_MODULE2STRING(module), \
						## arg); \
		} \
	} while (0)

#define st_err(module, __fmt, arg...)	\
	do { \
		if (stdbg_level > ST_NONE) { \
			if (stdbg_mask & module) \
				pr_err("[%s] error: " __fmt, \
						ST_MODULE2STRING(module), \
						## arg); \
		} \
	} while (0)

#define st_err_ratelimited(module, fmt, ...)                 \
	do {                                                                    \
		static DEFINE_RATELIMIT_STATE(_rs,                              \
						DEFAULT_RATELIMIT_INTERVAL,     \
						DEFAULT_RATELIMIT_BURST);       \
		if (__ratelimit(&_rs) && (stdbg_level > ST_NONE)) {             \
			if (stdbg_mask & module)                                \
				pr_err("[%s] error: " fmt,                      \
						ST_MODULE2STRING(module),       \
						##__VA_ARGS__);                 \
		} \
	} while (0)

#define set_bits(p, v, b, m)	(((p) & ~(m)) | ((v) << (b)))

static inline u32 reg_read(void __iomem *base, u32 reg)
{
	return ioread32(base + reg);
}

static inline void reg_write(void __iomem *base, u32 reg, u32 val)
{
	iowrite32(val, base + reg);
}

static inline void reg_set_bit(void __iomem *base, u32 reg, u32 mask, u32 val)
{
	u32 value;

	value = ioread32(base + reg) & ~mask;
	val &= mask;
	val |= value;
	iowrite32(val, base + reg);
}

static inline void reg_set(void __iomem *base, u32 reg, u32 mask)
{
	iowrite32(ioread32(base + reg) | mask, base + reg);
}

static inline void reg_clear(void __iomem *base, u32 reg, u32 mask)
{
	iowrite32(ioread32(base + reg) & ~mask, base + reg);
}

static inline void reg_set_highest_bit(void __iomem *base, u32 reg)
{
	u32 val;

	val = ioread32(base + reg);
	val &= ~(0x1 << 31);
	val |= (0x1 & 0x1) << 31;
	iowrite32(val, base + reg);
}

static inline void reg_clr_highest_bit(void __iomem *base, u32 reg)
{
	u32 val;

	val = ioread32(base + reg);
	val &= ~(0x1 << 31);
	val |= (0x0 & 0x1) << 31;
	iowrite32(val, base + reg);
}

static inline void print_reg(unsigned int module, void __iomem *base, u32 reg)
{
	//st_debug(module, "REG 0x%x = 0x%x\n",
	//		base + reg, ioread32(base + reg));
}

#endif /* STF_COMMON_H */
