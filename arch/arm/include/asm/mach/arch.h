/*
 *  arch/arm/include/asm/mach/arch.h
 *
 *  Copyright (C) 2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASSEMBLY__

struct tag;
struct meminfo;
struct sys_timer;

struct machine_desc {
	unsigned int		nr;		/* architecture number	*/
	const char		*name;		/* architecture name	*/
	unsigned long		boot_params;	/* tagged list		*/
	const char		**dt_compat;	/* array of device tree
						 * 'compatible' strings	*/

	unsigned int		nr_irqs;	/* number of IRQs */

#ifdef CONFIG_ZONE_DMA
	unsigned long		dma_zone_size;	/* size of DMA-able area */
#endif

	unsigned int		video_start;	/* start of video RAM	*/
	unsigned int		video_end;	/* end of video RAM	*/

	unsigned int		reserve_lp0 :1;	/* never has lp0	*/
	unsigned int		reserve_lp1 :1;	/* never has lp1	*/
	unsigned int		reserve_lp2 :1;	/* never has lp2	*/
	unsigned int		soft_reboot :1;	/* soft reboot		*/
	void			(*fixup)(struct machine_desc *,
					 struct tag *, char **,
					 struct meminfo *);
	void			(*reserve)(void);/* reserve mem blocks	*/
	void			(*map_io)(void);/* IO mapping function	*/
	void			(*init_early)(void);
	void			(*init_irq)(void);
	struct sys_timer	*timer;		/* system tick timer	*/
	void			(*init_machine)(void);
#ifdef CONFIG_MULTI_IRQ_HANDLER
	void			(*handle_irq)(struct pt_regs *);
#endif
};

/*
 * Current machine - only accessible during boot.
 */
extern struct machine_desc *machine_desc;

/*
 * Machine type table - also only accessible during boot
 */
extern struct machine_desc __arch_info_begin[], __arch_info_end[];
#define for_each_machine_desc(p)			\
	for (p = __arch_info_begin; p < __arch_info_end; p++)

/*
 * Set of macros to define architecture features.  This is built into
 * a table by the linker.
 */
#define MACHINE_START(_type,_name)			\
static const struct machine_desc __mach_desc_##_type	\
 __used							\
 __attribute__((__section__(".arch.info.init"))) = {	\
	.nr		= MACH_TYPE_##_type,		\
	.name		= _name,

#define MACHINE_END				\
};

#define DT_MACHINE_START(_name, _namestr)		\
static const struct machine_desc __mach_desc_##_name	\
 __used							\
 __attribute__((__section__(".arch.info.init"))) = {	\
	.nr		= ~0,				\
	.name		= _namestr,

#endif
