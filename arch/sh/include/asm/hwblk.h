#ifndef __ASM_SH_HWBLK_H
#define __ASM_SH_HWBLK_H

#include <asm/clock.h>
#include <asm/io.h>

#define HWBLK_CNT_USAGE 0
#define HWBLK_CNT_IDLE 1
#define HWBLK_CNT_DEVICES 2
#define HWBLK_CNT_NR 3

#define HWBLK_AREA_FLAG_PARENT (1 << 0) /* valid parent */

#define HWBLK_AREA(_flags, _parent)		\
{						\
	.flags = _flags,			\
	.parent = _parent,			\
}

struct hwblk_area {
	int cnt[HWBLK_CNT_NR];
	unsigned char parent;
	unsigned char flags;
};

#define HWBLK(_mstp, _bit, _area)		\
{						\
	.mstp = (void __iomem *)_mstp,		\
	.bit = _bit,				\
	.area = _area,				\
}

struct hwblk {
	void __iomem *mstp;
	unsigned char bit;
	unsigned char area;
	int cnt[HWBLK_CNT_NR];
};

struct hwblk_info {
	struct hwblk_area *areas;
	int nr_areas;
	struct hwblk *hwblks;
	int nr_hwblks;
};

/* Should be defined by processor-specific code */
int arch_hwblk_init(void);
int arch_hwblk_sleep_mode(void);

int hwblk_register(struct hwblk_info *info);
int hwblk_init(void);

void hwblk_enable(struct hwblk_info *info, int hwblk);
void hwblk_disable(struct hwblk_info *info, int hwblk);

void hwblk_cnt_inc(struct hwblk_info *info, int hwblk, int cnt);
void hwblk_cnt_dec(struct hwblk_info *info, int hwblk, int cnt);

/* allow clocks to enable and disable hardware blocks */
#define SH_HWBLK_CLK(_hwblk, _parent, _flags)	\
[_hwblk] = {					\
	.parent		= _parent,		\
	.arch_flags	= _hwblk,		\
	.flags		= _flags,		\
}

int sh_hwblk_clk_register(struct clk *clks, int nr);

#endif /* __ASM_SH_HWBLK_H */
