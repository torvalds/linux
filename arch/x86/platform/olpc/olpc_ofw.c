// SPDX-License-Identifier: GPL-2.0-only
#include <linux/kernel.h>
#include <linux/export.h>
#include <linux/spinlock_types.h>
#include <linux/init.h>
#include <asm/page.h>
#include <asm/setup.h>
#include <asm/io.h>
#include <asm/cpufeature.h>
#include <asm/special_insns.h>
#include <asm/pgtable.h>
#include <asm/olpc_ofw.h>

/* address of OFW callback interface; will be NULL if OFW isn't found */
static int (*olpc_ofw_cif)(int *);

/* page dir entry containing OFW's pgdir table; filled in by head_32.S */
u32 olpc_ofw_pgd __initdata;

static DEFINE_SPINLOCK(ofw_lock);

#define MAXARGS 10

void __init setup_olpc_ofw_pgd(void)
{
	pgd_t *base, *ofw_pde;

	if (!olpc_ofw_cif)
		return;

	/* fetch OFW's PDE */
	base = early_ioremap(olpc_ofw_pgd, sizeof(olpc_ofw_pgd) * PTRS_PER_PGD);
	if (!base) {
		printk(KERN_ERR "failed to remap OFW's pgd - disabling OFW!\n");
		olpc_ofw_cif = NULL;
		return;
	}
	ofw_pde = &base[OLPC_OFW_PDE_NR];

	/* install OFW's PDE permanently into the kernel's pgtable */
	set_pgd(&swapper_pg_dir[OLPC_OFW_PDE_NR], *ofw_pde);
	/* implicit optimization barrier here due to uninline function return */

	early_iounmap(base, sizeof(olpc_ofw_pgd) * PTRS_PER_PGD);
}

int __olpc_ofw(const char *name, int nr_args, const void **args, int nr_res,
		void **res)
{
	int ofw_args[MAXARGS + 3];
	unsigned long flags;
	int ret, i, *p;

	BUG_ON(nr_args + nr_res > MAXARGS);

	if (!olpc_ofw_cif)
		return -EIO;

	ofw_args[0] = (int)name;
	ofw_args[1] = nr_args;
	ofw_args[2] = nr_res;

	p = &ofw_args[3];
	for (i = 0; i < nr_args; i++, p++)
		*p = (int)args[i];

	/* call into ofw */
	spin_lock_irqsave(&ofw_lock, flags);
	ret = olpc_ofw_cif(ofw_args);
	spin_unlock_irqrestore(&ofw_lock, flags);

	if (!ret) {
		for (i = 0; i < nr_res; i++, p++)
			*((int *)res[i]) = *p;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(__olpc_ofw);

bool olpc_ofw_present(void)
{
	return olpc_ofw_cif != NULL;
}
EXPORT_SYMBOL_GPL(olpc_ofw_present);

/* OFW cif _should_ be above this address */
#define OFW_MIN 0xff000000

/* OFW starts on a 1MB boundary */
#define OFW_BOUND (1<<20)

void __init olpc_ofw_detect(void)
{
	struct olpc_ofw_header *hdr = &boot_params.olpc_ofw_header;
	unsigned long start;

	/* ensure OFW booted us by checking for "OFW " string */
	if (hdr->ofw_magic != OLPC_OFW_SIG)
		return;

	olpc_ofw_cif = (int (*)(int *))hdr->cif_handler;

	if ((unsigned long)olpc_ofw_cif < OFW_MIN) {
		printk(KERN_ERR "OFW detected, but cif has invalid address 0x%lx - disabling.\n",
				(unsigned long)olpc_ofw_cif);
		olpc_ofw_cif = NULL;
		return;
	}

	/* determine where OFW starts in memory */
	start = round_down((unsigned long)olpc_ofw_cif, OFW_BOUND);
	printk(KERN_INFO "OFW detected in memory, cif @ 0x%lx (reserving top %ldMB)\n",
			(unsigned long)olpc_ofw_cif, (-start) >> 20);
	reserve_top_address(-start);
}

bool __init olpc_ofw_is_installed(void)
{
	return olpc_ofw_cif != NULL;
}
