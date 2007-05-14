/*
 * arch/sh/kernel/machvec.c
 *
 * The SuperH machine vector setup handlers, yanked from setup.c
 *
 *  Copyright (C) 1999  Niibe Yutaka
 *  Copyright (C) 2002 - 2007 Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/string.h>
#include <asm/machvec.h>
#include <asm/sections.h>
#include <asm/io.h>
#include <asm/irq.h>

#define MV_NAME_SIZE 32

#define for_each_mv(mv) \
	for ((mv) = (struct sh_machine_vector *)&__machvec_start; \
	     (mv) && (unsigned long)(mv) < (unsigned long)&__machvec_end; \
	     (mv)++)

static struct sh_machine_vector * __init get_mv_byname(const char *name)
{
	struct sh_machine_vector *mv;

	for_each_mv(mv)
		if (strcasecmp(name, get_system_type()) == 0)
			return mv;

	return NULL;
}

static int __init early_parse_mv(char *from)
{
	char mv_name[MV_NAME_SIZE] = "";
	char *mv_end;
	char *mv_comma;
	int mv_len;
	struct sh_machine_vector *mvp;

	mv_end = strchr(from, ' ');
	if (mv_end == NULL)
		mv_end = from + strlen(from);

	mv_comma = strchr(from, ',');
	mv_len = mv_end - from;
	if (mv_len > (MV_NAME_SIZE-1))
		mv_len = MV_NAME_SIZE-1;
	memcpy(mv_name, from, mv_len);
	mv_name[mv_len] = '\0';
	from = mv_end;

	if (strcmp(sh_mv.mv_name, mv_name) != 0) {
		mvp = get_mv_byname(mv_name);
		if (unlikely(!mvp)) {
			printk("Available vectors:\n\n\t");
			for_each_mv(mvp)
				printk("'%s', ", mvp->mv_name);
			printk("\n\n");
			panic("Failed to select machvec '%s' -- halting.\n",
			      mv_name);
		} else
			sh_mv = *mvp;
	}

	printk(KERN_NOTICE "Booting machvec: %s\n", sh_mv.mv_name);
	return 0;
}
early_param("sh_mv", early_parse_mv);

void __init sh_mv_setup(void)
{
	/*
	 * Manually walk the vec, fill in anything that the board hasn't yet
	 * by hand, wrapping to the generic implementation.
	 */
#define mv_set(elem) do { \
	if (!sh_mv.mv_##elem) \
		sh_mv.mv_##elem = generic_##elem; \
} while (0)

	mv_set(inb);	mv_set(inw);	mv_set(inl);
	mv_set(outb);	mv_set(outw);	mv_set(outl);

	mv_set(inb_p);	mv_set(inw_p);	mv_set(inl_p);
	mv_set(outb_p);	mv_set(outw_p);	mv_set(outl_p);

	mv_set(insb);	mv_set(insw);	mv_set(insl);
	mv_set(outsb);	mv_set(outsw);	mv_set(outsl);

	mv_set(readb);	mv_set(readw);	mv_set(readl);
	mv_set(writeb);	mv_set(writew);	mv_set(writel);

	mv_set(ioport_map);
	mv_set(ioport_unmap);
	mv_set(irq_demux);

	if (!sh_mv.mv_nr_irqs)
		sh_mv.mv_nr_irqs = NR_IRQS;
}
