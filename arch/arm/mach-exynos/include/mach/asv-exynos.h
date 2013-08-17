/* linux/arch/arm/mach-exynos/include/mach/asv.h
 *
 * copyright (c) 2012 samsung electronics co., ltd.
 *		http://www.samsung.com/
 *
 * EXYNOS5 - Adaptive Support Voltage Source File
 *
 * this program is free software; you can redistribute it and/or modify
 * it under the terms of the gnu general public license version 2 as
 * published by the free software foundation.
*/

#ifndef __ASM_ARCH_NEW_ASV_H
#define __ASM_ARCH_NEW_ASV_H __FILE__

#define ASV_GRP_NR(_id)		_id##_ASV_GRP_NR
#define DVFS_LEVEL_NR(_id)	_id##_DVFS_LEVEL_NR
#define MAX_VOLT(_id)		_id##_MAX_VOLT
#define MAX_VOLT_VER(_id, _ver)	_id##_MAX_VOLT_##_ver

#define ABB_X060		0
#define ABB_X065		1
#define ABB_X070		2
#define ABB_X075		3
#define ABB_X080		4
#define ABB_X085		5
#define ABB_X090		6
#define ABB_X095		7
#define ABB_X100		8
#define ABB_X105		9
#define ABB_X110		10
#define ABB_X115		11
#define ABB_X120		12
#define ABB_X125		13
#define ABB_X130		14
#define ABB_X135		15
#define ABB_X140		16
#define ABB_X145		17
#define ABB_X150		18
#define ABB_X155		19
#define ABB_X160		20
#define ABB_BYPASS		255

#define ABB_INIT		(0x80000080)
#define ABB_INIT_BYPASS		(0x80000000)

static inline void set_abb(void __iomem *target_reg, unsigned int target_value)
{
	unsigned int tmp;

	if (target_value == ABB_BYPASS)
		tmp = ABB_INIT_BYPASS;
	else
		tmp = (ABB_INIT | target_value);

	__raw_writel(tmp , target_reg);
}

enum asv_type_id {
	ID_ARM,
	ID_KFC,
	ID_INT,
	ID_MIF,
	ID_G3D,
	ID_INT_MIF_L0,
	ID_INT_MIF_L1,
	ID_INT_MIF_L2,
	ID_INT_MIF_L3,
};

/* define Struct for ASV common */
struct asv_common {
	char		lot_name[5];
	unsigned int	ids_value;
	unsigned int	hpm_value;
	unsigned int	(*init)(void);
	unsigned int	(*regist_asv_member)(void);
};

struct asv_freq_table {
	unsigned int	asv_freq;
	unsigned int	asv_value;
};

/* define struct for information of each ASV type */
struct asv_info {
	struct list_head	node;
	enum asv_type_id	asv_type;
	const char		*name;
	struct asv_ops		*ops;
	unsigned int		asv_group_nr;
	unsigned int		dvfs_level_nr;
	unsigned int		result_asv_grp;
	unsigned int		max_volt_value;
	struct asv_freq_table	*asv_volt;
	struct asv_freq_table	*asv_abb;
	struct abb_common	*abb_info;
};

/* Struct for ABB function */
struct abb_common {
	unsigned int	target_abb;
	void		(*set_target_abb)(struct asv_info *asv_inform);
};

/* Operation for ASV*/
struct asv_ops {
	unsigned int	(*get_asv_group)(struct asv_common *asv_comm);
	void		(*set_asv_info)(struct asv_info *asv_inform, bool show_value);
};

/* define function for common asv */
extern void add_asv_member(struct asv_info *exynos_asv_info);
extern struct asv_info *asv_get(enum asv_type_id exynos_asv_type_id);
extern unsigned int get_match_volt(enum asv_type_id target_type, unsigned int target_freq);
extern unsigned int get_match_abb(enum asv_type_id target_type, unsigned int target_freq);
extern unsigned int set_match_abb(enum asv_type_id target_type, unsigned int target_abb);
/* define function for initialize of SoC */
extern int exynos5410_init_asv(struct asv_common *asv_info);
#endif /* __ASM_ARCH_NEW_ASV_H */
