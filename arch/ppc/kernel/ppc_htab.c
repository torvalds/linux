/*
 * PowerPC hash table management proc entry.  Will show information
 * about the current hash table and will allow changes to it.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/proc_fs.h>
#include <linux/stat.h>
#include <linux/sysctl.h>
#include <linux/capability.h>
#include <linux/ctype.h>
#include <linux/threads.h>
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/uaccess.h>
#include <asm/mmu.h>
#include <asm/residual.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/cputable.h>
#include <asm/system.h>
#include <asm/reg.h>

static int ppc_htab_show(struct seq_file *m, void *v);
static ssize_t ppc_htab_write(struct file * file, const char __user * buffer,
			      size_t count, loff_t *ppos);
extern PTE *Hash, *Hash_end;
extern unsigned long Hash_size, Hash_mask;
extern unsigned long _SDR1;
extern unsigned long htab_reloads;
extern unsigned long htab_preloads;
extern unsigned long htab_evicts;
extern unsigned long pte_misses;
extern unsigned long pte_errors;
extern unsigned int primary_pteg_full;
extern unsigned int htab_hash_searches;

static int ppc_htab_open(struct inode *inode, struct file *file)
{
	return single_open(file, ppc_htab_show, NULL);
}

const struct file_operations ppc_htab_operations = {
	.open		= ppc_htab_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.write		= ppc_htab_write,
	.release	= single_release,
};

static char *pmc1_lookup(unsigned long mmcr0)
{
	switch ( mmcr0 & (0x7f<<7) )
	{
	case 0x0:
		return "none";
	case MMCR0_PMC1_CYCLES:
		return "cycles";
	case MMCR0_PMC1_ICACHEMISS:
		return "ic miss";
	case MMCR0_PMC1_DTLB:
		return "dtlb miss";
	default:
		return "unknown";
	}
}

static char *pmc2_lookup(unsigned long mmcr0)
{
	switch ( mmcr0 & 0x3f )
	{
	case 0x0:
		return "none";
	case MMCR0_PMC2_CYCLES:
		return "cycles";
	case MMCR0_PMC2_DCACHEMISS:
		return "dc miss";
	case MMCR0_PMC2_ITLB:
		return "itlb miss";
	case MMCR0_PMC2_LOADMISSTIME:
		return "load miss time";
	default:
		return "unknown";
	}
}

/*
 * print some useful info about the hash table.  This function
 * is _REALLY_ slow (see the nested for loops below) but nothing
 * in here should be really timing critical. -- Cort
 */
static int ppc_htab_show(struct seq_file *m, void *v)
{
	unsigned long mmcr0 = 0, pmc1 = 0, pmc2 = 0;
#if defined(CONFIG_PPC_STD_MMU)
	unsigned int kptes = 0, uptes = 0;
	PTE *ptr;
#endif /* CONFIG_PPC_STD_MMU */

	if (cpu_has_feature(CPU_FTR_604_PERF_MON)) {
		mmcr0 = mfspr(SPRN_MMCR0);
		pmc1 = mfspr(SPRN_PMC1);
		pmc2 = mfspr(SPRN_PMC2);
		seq_printf(m,
			      "604 Performance Monitoring\n"
			      "MMCR0\t\t: %08lx %s%s ",
			      mmcr0,
			      ( mmcr0>>28 & 0x2 ) ? "(user mode counted)" : "",
			      ( mmcr0>>28 & 0x4 ) ? "(kernel mode counted)" : "");
		seq_printf(m,
			      "\nPMC1\t\t: %08lx (%s)\n"
			      "PMC2\t\t: %08lx (%s)\n",
			      pmc1, pmc1_lookup(mmcr0),
			      pmc2, pmc2_lookup(mmcr0));
	}

#ifdef CONFIG_PPC_STD_MMU
	/* if we don't have a htab */
	if ( Hash_size == 0 ) {
		seq_printf(m, "No Hash Table used\n");
		return 0;
	}

	for (ptr = Hash; ptr < Hash_end; ptr++) {
		unsigned int mctx, vsid;

		if (!ptr->v)
			continue;
		/* undo the esid skew */
		vsid = ptr->vsid;
		mctx = ((vsid - (vsid & 0xf) * 0x111) >> 4) & 0xfffff;
		if (mctx == 0)
			kptes++;
		else
			uptes++;
	}

	seq_printf(m,
		      "PTE Hash Table Information\n"
		      "Size\t\t: %luKb\n"
		      "Buckets\t\t: %lu\n"
 		      "Address\t\t: %08lx\n"
		      "Entries\t\t: %lu\n"
		      "User ptes\t: %u\n"
		      "Kernel ptes\t: %u\n"
		      "Percent full\t: %lu%%\n"
                      , (unsigned long)(Hash_size>>10),
		      (Hash_size/(sizeof(PTE)*8)),
		      (unsigned long)Hash,
		      Hash_size/sizeof(PTE)
                      , uptes,
		      kptes,
		      ((kptes+uptes)*100) / (Hash_size/sizeof(PTE))
		);

	seq_printf(m,
		      "Reloads\t\t: %lu\n"
		      "Preloads\t: %lu\n"
		      "Searches\t: %u\n"
		      "Overflows\t: %u\n"
		      "Evicts\t\t: %lu\n",
		      htab_reloads, htab_preloads, htab_hash_searches,
		      primary_pteg_full, htab_evicts);
#endif /* CONFIG_PPC_STD_MMU */

	seq_printf(m,
		      "Non-error misses: %lu\n"
		      "Error misses\t: %lu\n",
		      pte_misses, pte_errors);
	return 0;
}

/*
 * Allow user to define performance counters and resize the hash table
 */
static ssize_t ppc_htab_write(struct file * file, const char __user * ubuffer,
			      size_t count, loff_t *ppos)
{
#ifdef CONFIG_PPC_STD_MMU
	unsigned long tmp;
	char buffer[16];

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;
	if (strncpy_from_user(buffer, ubuffer, 15))
		return -EFAULT;
	buffer[15] = 0;

	/* don't set the htab size for now */
	if ( !strncmp( buffer, "size ", 5) )
		return -EBUSY;

	if ( !strncmp( buffer, "reset", 5) )
	{
		if (cpu_has_feature(CPU_FTR_604_PERF_MON)) {
			/* reset PMC1 and PMC2 */
			mtspr(SPRN_PMC1, 0);
			mtspr(SPRN_PMC2, 0);
		}
		htab_reloads = 0;
		htab_evicts = 0;
		pte_misses = 0;
		pte_errors = 0;
	}

	/* Everything below here requires the performance monitor feature. */
	if (!cpu_has_feature(CPU_FTR_604_PERF_MON))
		return count;

	/* turn off performance monitoring */
	if ( !strncmp( buffer, "off", 3) )
	{
		mtspr(SPRN_MMCR0, 0);
		mtspr(SPRN_PMC1, 0);
		mtspr(SPRN_PMC2, 0);
	}

	if ( !strncmp( buffer, "user", 4) )
	{
		/* setup mmcr0 and clear the correct pmc */
		tmp = (mfspr(SPRN_MMCR0) & ~(0x60000000)) | 0x20000000;
		mtspr(SPRN_MMCR0, tmp);
		mtspr(SPRN_PMC1, 0);
		mtspr(SPRN_PMC2, 0);
	}

	if ( !strncmp( buffer, "kernel", 6) )
	{
		/* setup mmcr0 and clear the correct pmc */
		tmp = (mfspr(SPRN_MMCR0) & ~(0x60000000)) | 0x40000000;
		mtspr(SPRN_MMCR0, tmp);
		mtspr(SPRN_PMC1, 0);
		mtspr(SPRN_PMC2, 0);
	}

	/* PMC1 values */
	if ( !strncmp( buffer, "dtlb", 4) )
	{
		/* setup mmcr0 and clear the correct pmc */
		tmp = (mfspr(SPRN_MMCR0) & ~(0x7F << 7)) | MMCR0_PMC1_DTLB;
		mtspr(SPRN_MMCR0, tmp);
		mtspr(SPRN_PMC1, 0);
	}

	if ( !strncmp( buffer, "ic miss", 7) )
	{
		/* setup mmcr0 and clear the correct pmc */
		tmp = (mfspr(SPRN_MMCR0) & ~(0x7F<<7)) | MMCR0_PMC1_ICACHEMISS;
		mtspr(SPRN_MMCR0, tmp);
		mtspr(SPRN_PMC1, 0);
	}

	/* PMC2 values */
	if ( !strncmp( buffer, "load miss time", 14) )
	{
		/* setup mmcr0 and clear the correct pmc */
	       asm volatile(
		       "mfspr %0,%1\n\t"     /* get current mccr0 */
		       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
		       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
		       "mtspr %1,%0 \n\t"    /* set new mccr0 */
		       "mtspr %3,%4 \n\t"    /* reset the pmc */
		       : "=r" (tmp)
		       : "i" (SPRN_MMCR0),
		       "i" (MMCR0_PMC2_LOADMISSTIME),
		       "i" (SPRN_PMC2),  "r" (0) );
	}

	if ( !strncmp( buffer, "itlb", 4) )
	{
		/* setup mmcr0 and clear the correct pmc */
	       asm volatile(
		       "mfspr %0,%1\n\t"     /* get current mccr0 */
		       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
		       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
		       "mtspr %1,%0 \n\t"    /* set new mccr0 */
		       "mtspr %3,%4 \n\t"    /* reset the pmc */
		       : "=r" (tmp)
		       : "i" (SPRN_MMCR0), "i" (MMCR0_PMC2_ITLB),
		       "i" (SPRN_PMC2),  "r" (0) );
	}

	if ( !strncmp( buffer, "dc miss", 7) )
	{
		/* setup mmcr0 and clear the correct pmc */
	       asm volatile(
		       "mfspr %0,%1\n\t"     /* get current mccr0 */
		       "rlwinm %0,%0,0,0,31-6\n\t"  /* clear bits [26-31] */
		       "ori   %0,%0,%2 \n\t" /* or in mmcr0 settings */
		       "mtspr %1,%0 \n\t"    /* set new mccr0 */
		       "mtspr %3,%4 \n\t"    /* reset the pmc */
		       : "=r" (tmp)
		       : "i" (SPRN_MMCR0), "i" (MMCR0_PMC2_DCACHEMISS),
		       "i" (SPRN_PMC2),  "r" (0) );
	}

	return count;
#else /* CONFIG_PPC_STD_MMU */
	return 0;
#endif /* CONFIG_PPC_STD_MMU */
}

int proc_dol2crvec(ctl_table *table, int write, struct file *filp,
		  void __user *buffer_arg, size_t *lenp, loff_t *ppos)
{
	int vleft, first=1, len, left, val;
	char __user *buffer = (char __user *) buffer_arg;
	#define TMPBUFLEN 256
	char buf[TMPBUFLEN], *p;
	static const char *sizestrings[4] = {
		"2MB", "256KB", "512KB", "1MB"
	};
	static const char *clockstrings[8] = {
		"clock disabled", "+1 clock", "+1.5 clock", "reserved(3)",
		"+2 clock", "+2.5 clock", "+3 clock", "reserved(7)"
	};
	static const char *typestrings[4] = {
		"flow-through burst SRAM", "reserved SRAM",
		"pipelined burst SRAM", "pipelined late-write SRAM"
	};
	static const char *holdstrings[4] = {
		"0.5", "1.0", "(reserved2)", "(reserved3)"
	};

	if (!cpu_has_feature(CPU_FTR_L2CR))
		return -EFAULT;

	if ( /*!table->maxlen ||*/ (*ppos && !write)) {
		*lenp = 0;
		return 0;
	}

	vleft = table->maxlen / sizeof(int);
	left = *lenp;

	for (; left /*&& vleft--*/; first=0) {
		if (write) {
			while (left) {
				char c;
				if(get_user(c, buffer))
					return -EFAULT;
				if (!isspace(c))
					break;
				left--;
				buffer++;
			}
			if (!left)
				break;
			len = left;
			if (len > TMPBUFLEN-1)
				len = TMPBUFLEN-1;
			if(copy_from_user(buf, buffer, len))
				return -EFAULT;
			buf[len] = 0;
			p = buf;
			if (*p < '0' || *p > '9')
				break;
			val = simple_strtoul(p, &p, 0);
			len = p-buf;
			if ((len < left) && *p && !isspace(*p))
				break;
			buffer += len;
			left -= len;
			_set_L2CR(val);
		} else {
			p = buf;
			if (!first)
				*p++ = '\t';
			val = _get_L2CR();
			p += sprintf(p, "0x%08x: ", val);
			p += sprintf(p, " %s", (val >> 31) & 1 ? "enabled" :
				     	"disabled");
			p += sprintf(p, ", %sparity", (val>>30)&1 ? "" : "no ");
			p += sprintf(p, ", %s", sizestrings[(val >> 28) & 3]);
			p += sprintf(p, ", %s", clockstrings[(val >> 25) & 7]);
			p += sprintf(p, ", %s", typestrings[(val >> 23) & 2]);
			p += sprintf(p, "%s", (val>>22)&1 ? ", data only" : "");
			p += sprintf(p, "%s", (val>>20)&1 ? ", ZZ enabled": "");
			p += sprintf(p, ", %s", (val>>19)&1 ? "write-through" :
					"copy-back");
			p += sprintf(p, "%s", (val>>18)&1 ? ", testing" : "");
			p += sprintf(p, ", %sns hold",holdstrings[(val>>16)&3]);
			p += sprintf(p, "%s", (val>>15)&1 ? ", DLL slow" : "");
			p += sprintf(p, "%s", (val>>14)&1 ? ", diff clock" :"");
			p += sprintf(p, "%s", (val>>13)&1 ? ", DLL bypass" :"");

			p += sprintf(p,"\n");

			len = strlen(buf);
			if (len > left)
				len = left;
			if (copy_to_user(buffer, buf, len))
				return -EFAULT;
			left -= len;
			buffer += len;
			break;
		}
	}

	if (!write && !first && left) {
		if(put_user('\n', (char __user *) buffer))
			return -EFAULT;
		left--, buffer++;
	}
	if (write) {
		char __user *s = (char __user *) buffer;
		while (left) {
			char c;
			if(get_user(c, s++))
				return -EFAULT;
			if (!isspace(c))
				break;
			left--;
		}
	}
	if (write && first)
		return -EINVAL;
	*lenp -= left;
	*ppos += *lenp;
	return 0;
}

#ifdef CONFIG_SYSCTL
/*
 * Register our sysctl.
 */
static ctl_table htab_ctl_table[]={
	{
		.ctl_name	= KERN_PPC_L2CR,
		.procname	= "l2cr",
		.mode		= 0644,
		.proc_handler	= &proc_dol2crvec,
	},
	{}
};
static ctl_table htab_sysctl_root[] = {
	{
		.ctl_name	= CTL_KERN,
		.procname	= "kernel",
		.mode		= 0555,
		.child		= htab_ctl_table,
	},
	{}
};

static int __init
register_ppc_htab_sysctl(void)
{
	register_sysctl_table(htab_sysctl_root);

	return 0;
}

__initcall(register_ppc_htab_sysctl);
#endif
