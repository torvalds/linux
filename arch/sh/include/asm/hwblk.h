#ifndef __ASM_SH_HWBLK_H
#define __ASM_SH_HWBLK_H

#include <asm/clock.h>
#include <asm/io.h>

#define HWBLK_AREA_FLAG_PARENT (1 << 0) /* valid parent */

#define HWBLK_AREA(_flags, _parent)		\
{						\
	.flags = _flags,			\
	.parent = _parent,			\
}

struct hwblk_area {
	unsigned long cnt;
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
	unsigned long cnt;
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

/* allow clocks to enable and disable hardware blocks */
#define SH_HWBLK_CLK(_name, _id, _parent, _hwblk, _flags)	\
{							\
	.name		= _name,			\
	.id		= _id,				\
	.parent		= _parent,			\
	.arch_flags	= _hwblk,			\
	.flags		= _flags,			\
}

int sh_hwblk_clk_register(struct clk *clks, int nr);

#endif /* __ASM_SH_HWBLK_H */
