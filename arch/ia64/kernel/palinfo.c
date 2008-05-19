/*
 * palinfo.c
 *
 * Prints processor specific information reported by PAL.
 * This code is based on specification of PAL as of the
 * Intel IA-64 Architecture Software Developer's Manual v1.0.
 *
 *
 * Copyright (C) 2000-2001, 2003 Hewlett-Packard Co
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2004 Intel Corporation
 *  Ashok Raj <ashok.raj@intel.com>
 *
 * 05/26/2000	S.Eranian	initial release
 * 08/21/2000	S.Eranian	updated to July 2000 PAL specs
 * 02/05/2001   S.Eranian	fixed module support
 * 10/23/2001	S.Eranian	updated pal_perf_mon_info bug fixes
 * 03/24/2004	Ashok Raj	updated to work with CPU Hotplug
 * 10/26/2006   Russ Anderson	updated processor features to rev 2.2 spec
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/efi.h>
#include <linux/notifier.h>
#include <linux/cpu.h>
#include <linux/cpumask.h>

#include <asm/pal.h>
#include <asm/sal.h>
#include <asm/page.h>
#include <asm/processor.h>
#include <linux/smp.h>

MODULE_AUTHOR("Stephane Eranian <eranian@hpl.hp.com>");
MODULE_DESCRIPTION("/proc interface to IA-64 PAL");
MODULE_LICENSE("GPL");

#define PALINFO_VERSION "0.5"

typedef int (*palinfo_func_t)(char*);

typedef struct {
	const char		*name;		/* name of the proc entry */
	palinfo_func_t		proc_read;	/* function to call for reading */
	struct proc_dir_entry	*entry;		/* registered entry (removal) */
} palinfo_entry_t;


/*
 *  A bunch of string array to get pretty printing
 */

static char *cache_types[] = {
	"",			/* not used */
	"Instruction",
	"Data",
	"Data/Instruction"	/* unified */
};

static const char *cache_mattrib[]={
	"WriteThrough",
	"WriteBack",
	"",		/* reserved */
	""		/* reserved */
};

static const char *cache_st_hints[]={
	"Temporal, level 1",
	"Reserved",
	"Reserved",
	"Non-temporal, all levels",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved"
};

static const char *cache_ld_hints[]={
	"Temporal, level 1",
	"Non-temporal, level 1",
	"Reserved",
	"Non-temporal, all levels",
	"Reserved",
	"Reserved",
	"Reserved",
	"Reserved"
};

static const char *rse_hints[]={
	"enforced lazy",
	"eager stores",
	"eager loads",
	"eager loads and stores"
};

#define RSE_HINTS_COUNT ARRAY_SIZE(rse_hints)

static const char *mem_attrib[]={
	"WB",		/* 000 */
	"SW",		/* 001 */
	"010",		/* 010 */
	"011",		/* 011 */
	"UC",		/* 100 */
	"UCE",		/* 101 */
	"WC",		/* 110 */
	"NaTPage"	/* 111 */
};

/*
 * Take a 64bit vector and produces a string such that
 * if bit n is set then 2^n in clear text is generated. The adjustment
 * to the right unit is also done.
 *
 * Input:
 *	- a pointer to a buffer to hold the string
 *	- a 64-bit vector
 * Ouput:
 *	- a pointer to the end of the buffer
 *
 */
static char *
bitvector_process(char *p, u64 vector)
{
	int i,j;
	const char *units[]={ "", "K", "M", "G", "T" };

	for (i=0, j=0; i < 64; i++ , j=i/10) {
		if (vector & 0x1) {
			p += sprintf(p, "%d%s ", 1 << (i-j*10), units[j]);
		}
		vector >>= 1;
	}
	return p;
}

/*
 * Take a 64bit vector and produces a string such that
 * if bit n is set then register n is present. The function
 * takes into account consecutive registers and prints out ranges.
 *
 * Input:
 *	- a pointer to a buffer to hold the string
 *	- a 64-bit vector
 * Ouput:
 *	- a pointer to the end of the buffer
 *
 */
static char *
bitregister_process(char *p, u64 *reg_info, int max)
{
	int i, begin, skip = 0;
	u64 value = reg_info[0];

	value >>= i = begin = ffs(value) - 1;

	for(; i < max; i++ ) {

		if (i != 0 && (i%64) == 0) value = *++reg_info;

		if ((value & 0x1) == 0 && skip == 0) {
			if (begin  <= i - 2)
				p += sprintf(p, "%d-%d ", begin, i-1);
			else
				p += sprintf(p, "%d ", i-1);
			skip  = 1;
			begin = -1;
		} else if ((value & 0x1) && skip == 1) {
			skip = 0;
			begin = i;
		}
		value >>=1;
	}
	if (begin > -1) {
		if (begin < 127)
			p += sprintf(p, "%d-127", begin);
		else
			p += sprintf(p, "127");
	}

	return p;
}

static int
power_info(char *page)
{
	s64 status;
	char *p = page;
	u64 halt_info_buffer[8];
	pal_power_mgmt_info_u_t *halt_info =(pal_power_mgmt_info_u_t *)halt_info_buffer;
	int i;

	status = ia64_pal_halt_info(halt_info);
	if (status != 0) return 0;

	for (i=0; i < 8 ; i++ ) {
		if (halt_info[i].pal_power_mgmt_info_s.im == 1) {
			p += sprintf(p,	"Power level %d:\n"
				     "\tentry_latency       : %d cycles\n"
				     "\texit_latency        : %d cycles\n"
				     "\tpower consumption   : %d mW\n"
				     "\tCache+TLB coherency : %s\n", i,
				     halt_info[i].pal_power_mgmt_info_s.entry_latency,
				     halt_info[i].pal_power_mgmt_info_s.exit_latency,
				     halt_info[i].pal_power_mgmt_info_s.power_consumption,
				     halt_info[i].pal_power_mgmt_info_s.co ? "Yes" : "No");
		} else {
			p += sprintf(p,"Power level %d: not implemented\n",i);
		}
	}
	return p - page;
}

static int
cache_info(char *page)
{
	char *p = page;
	u64 i, levels, unique_caches;
	pal_cache_config_info_t cci;
	int j, k;
	s64 status;

	if ((status = ia64_pal_cache_summary(&levels, &unique_caches)) != 0) {
		printk(KERN_ERR "ia64_pal_cache_summary=%ld\n", status);
		return 0;
	}

	p += sprintf(p, "Cache levels  : %ld\nUnique caches : %ld\n\n", levels, unique_caches);

	for (i=0; i < levels; i++) {

		for (j=2; j >0 ; j--) {

			/* even without unification some level may not be present */
			if ((status=ia64_pal_cache_config_info(i,j, &cci)) != 0) {
				continue;
			}
			p += sprintf(p,
				     "%s Cache level %lu:\n"
				     "\tSize           : %u bytes\n"
				     "\tAttributes     : ",
				     cache_types[j+cci.pcci_unified], i+1,
				     cci.pcci_cache_size);

			if (cci.pcci_unified) p += sprintf(p, "Unified ");

			p += sprintf(p, "%s\n", cache_mattrib[cci.pcci_cache_attr]);

			p += sprintf(p,
				     "\tAssociativity  : %d\n"
				     "\tLine size      : %d bytes\n"
				     "\tStride         : %d bytes\n",
				     cci.pcci_assoc, 1<<cci.pcci_line_size, 1<<cci.pcci_stride);
			if (j == 1)
				p += sprintf(p, "\tStore latency  : N/A\n");
			else
				p += sprintf(p, "\tStore latency  : %d cycle(s)\n",
						cci.pcci_st_latency);

			p += sprintf(p,
				     "\tLoad latency   : %d cycle(s)\n"
				     "\tStore hints    : ", cci.pcci_ld_latency);

			for(k=0; k < 8; k++ ) {
				if ( cci.pcci_st_hints & 0x1)
					p += sprintf(p, "[%s]", cache_st_hints[k]);
				cci.pcci_st_hints >>=1;
			}
			p += sprintf(p, "\n\tLoad hints     : ");

			for(k=0; k < 8; k++ ) {
				if (cci.pcci_ld_hints & 0x1)
					p += sprintf(p, "[%s]", cache_ld_hints[k]);
				cci.pcci_ld_hints >>=1;
			}
			p += sprintf(p,
				     "\n\tAlias boundary : %d byte(s)\n"
				     "\tTag LSB        : %d\n"
				     "\tTag MSB        : %d\n",
				     1<<cci.pcci_alias_boundary, cci.pcci_tag_lsb,
				     cci.pcci_tag_msb);

			/* when unified, data(j=2) is enough */
			if (cci.pcci_unified) break;
		}
	}
	return p - page;
}


static int
vm_info(char *page)
{
	char *p = page;
	u64 tr_pages =0, vw_pages=0, tc_pages;
	u64 attrib;
	pal_vm_info_1_u_t vm_info_1;
	pal_vm_info_2_u_t vm_info_2;
	pal_tc_info_u_t	tc_info;
	ia64_ptce_info_t ptce;
	const char *sep;
	int i, j;
	s64 status;

	if ((status = ia64_pal_vm_summary(&vm_info_1, &vm_info_2)) !=0) {
		printk(KERN_ERR "ia64_pal_vm_summary=%ld\n", status);
	} else {

		p += sprintf(p,
		     "Physical Address Space         : %d bits\n"
		     "Virtual Address Space          : %d bits\n"
		     "Protection Key Registers(PKR)  : %d\n"
		     "Implemented bits in PKR.key    : %d\n"
		     "Hash Tag ID                    : 0x%x\n"
		     "Size of RR.rid                 : %d\n"
		     "Max Purges                     : ",
		     vm_info_1.pal_vm_info_1_s.phys_add_size,
		     vm_info_2.pal_vm_info_2_s.impl_va_msb+1,
		     vm_info_1.pal_vm_info_1_s.max_pkr+1,
		     vm_info_1.pal_vm_info_1_s.key_size,
		     vm_info_1.pal_vm_info_1_s.hash_tag_id,
		     vm_info_2.pal_vm_info_2_s.rid_size);
		if (vm_info_2.pal_vm_info_2_s.max_purges == PAL_MAX_PURGES)
			p += sprintf(p, "unlimited\n");
		else
			p += sprintf(p, "%d\n",
		     		vm_info_2.pal_vm_info_2_s.max_purges ?
				vm_info_2.pal_vm_info_2_s.max_purges : 1);
	}

	if (ia64_pal_mem_attrib(&attrib) == 0) {
		p += sprintf(p, "Supported memory attributes    : ");
		sep = "";
		for (i = 0; i < 8; i++) {
			if (attrib & (1 << i)) {
				p += sprintf(p, "%s%s", sep, mem_attrib[i]);
				sep = ", ";
			}
		}
		p += sprintf(p, "\n");
	}

	if ((status = ia64_pal_vm_page_size(&tr_pages, &vw_pages)) !=0) {
		printk(KERN_ERR "ia64_pal_vm_page_size=%ld\n", status);
	} else {

		p += sprintf(p,
			     "\nTLB walker                     : %simplemented\n"
			     "Number of DTR                  : %d\n"
			     "Number of ITR                  : %d\n"
			     "TLB insertable page sizes      : ",
			     vm_info_1.pal_vm_info_1_s.vw ? "" : "not ",
			     vm_info_1.pal_vm_info_1_s.max_dtr_entry+1,
			     vm_info_1.pal_vm_info_1_s.max_itr_entry+1);


		p = bitvector_process(p, tr_pages);

		p += sprintf(p, "\nTLB purgeable page sizes       : ");

		p = bitvector_process(p, vw_pages);
	}
	if ((status=ia64_get_ptce(&ptce)) != 0) {
		printk(KERN_ERR "ia64_get_ptce=%ld\n", status);
	} else {
		p += sprintf(p,
		     "\nPurge base address             : 0x%016lx\n"
		     "Purge outer loop count         : %d\n"
		     "Purge inner loop count         : %d\n"
		     "Purge outer loop stride        : %d\n"
		     "Purge inner loop stride        : %d\n",
		     ptce.base, ptce.count[0], ptce.count[1],
		     ptce.stride[0], ptce.stride[1]);

		p += sprintf(p,
		     "TC Levels                      : %d\n"
		     "Unique TC(s)                   : %d\n",
		     vm_info_1.pal_vm_info_1_s.num_tc_levels,
		     vm_info_1.pal_vm_info_1_s.max_unique_tcs);

		for(i=0; i < vm_info_1.pal_vm_info_1_s.num_tc_levels; i++) {
			for (j=2; j>0 ; j--) {
				tc_pages = 0; /* just in case */


				/* even without unification, some levels may not be present */
				if ((status=ia64_pal_vm_info(i,j, &tc_info, &tc_pages)) != 0) {
					continue;
				}

				p += sprintf(p,
				     "\n%s Translation Cache Level %d:\n"
				     "\tHash sets           : %d\n"
				     "\tAssociativity       : %d\n"
				     "\tNumber of entries   : %d\n"
				     "\tFlags               : ",
				     cache_types[j+tc_info.tc_unified], i+1,
				     tc_info.tc_num_sets,
				     tc_info.tc_associativity,
				     tc_info.tc_num_entries);

				if (tc_info.tc_pf)
					p += sprintf(p, "PreferredPageSizeOptimized ");
				if (tc_info.tc_unified)
					p += sprintf(p, "Unified ");
				if (tc_info.tc_reduce_tr)
					p += sprintf(p, "TCReduction");

				p += sprintf(p, "\n\tSupported page sizes: ");

				p = bitvector_process(p, tc_pages);

				/* when unified date (j=2) is enough */
				if (tc_info.tc_unified)
					break;
			}
		}
	}
	p += sprintf(p, "\n");

	return p - page;
}


static int
register_info(char *page)
{
	char *p = page;
	u64 reg_info[2];
	u64 info;
	u64 phys_stacked;
	pal_hints_u_t hints;
	u64 iregs, dregs;
	char *info_type[]={
		"Implemented AR(s)",
		"AR(s) with read side-effects",
		"Implemented CR(s)",
		"CR(s) with read side-effects",
	};

	for(info=0; info < 4; info++) {

		if (ia64_pal_register_info(info, &reg_info[0], &reg_info[1]) != 0) return 0;

		p += sprintf(p, "%-32s : ", info_type[info]);

		p = bitregister_process(p, reg_info, 128);

		p += sprintf(p, "\n");
	}

	if (ia64_pal_rse_info(&phys_stacked, &hints) == 0) {

	p += sprintf(p,
		     "RSE stacked physical registers   : %ld\n"
		     "RSE load/store hints             : %ld (%s)\n",
		     phys_stacked, hints.ph_data,
		     hints.ph_data < RSE_HINTS_COUNT ? rse_hints[hints.ph_data]: "(??)");
	}
	if (ia64_pal_debug_info(&iregs, &dregs))
		return 0;

	p += sprintf(p,
		     "Instruction debug register pairs : %ld\n"
		     "Data debug register pairs        : %ld\n", iregs, dregs);

	return p - page;
}

static char *proc_features_0[]={		/* Feature set 0 */
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL, NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL, NULL,NULL,NULL,NULL,
	"Unimplemented instruction address fault",
	"INIT, PMI, and LINT pins",
	"Simple unimplemented instr addresses",
	"Variable P-state performance",
	"Virtual machine features implemented",
	"XIP,XPSR,XFS implemented",
	"XR1-XR3 implemented",
	"Disable dynamic predicate prediction",
	"Disable processor physical number",
	"Disable dynamic data cache prefetch",
	"Disable dynamic inst cache prefetch",
	"Disable dynamic branch prediction",
	NULL, NULL, NULL, NULL,
	"Disable P-states",
	"Enable MCA on Data Poisoning",
	"Enable vmsw instruction",
	"Enable extern environmental notification",
	"Disable BINIT on processor time-out",
	"Disable dynamic power management (DPM)",
	"Disable coherency",
	"Disable cache",
	"Enable CMCI promotion",
	"Enable MCA to BINIT promotion",
	"Enable MCA promotion",
	"Enable BERR promotion"
};

static char *proc_features_16[]={		/* Feature set 16 */
	"Disable ETM",
	"Enable ETM",
	"Enable MCA on half-way timer",
	"Enable snoop WC",
	NULL,
	"Enable Fast Deferral",
	"Disable MCA on memory aliasing",
	"Enable RSB",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	"DP system processor",
	"Low Voltage",
	"HT supported",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL
};

static char **proc_features[]={
	proc_features_0,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	proc_features_16,
	NULL, NULL, NULL, NULL,
};

static char *
feature_set_info(char *page, u64 avail, u64 status, u64 control, u64 set)
{
	char *p = page;
	char **vf, **v;
	int i;

	vf = v = proc_features[set];
	for(i=0; i < 64; i++, avail >>=1, status >>=1, control >>=1) {

		if (!(control))		/* No remaining bits set */
			break;
		if (!(avail & 0x1))	/* Print only bits that are available */
			continue;
		if (vf)
			v = vf + i;
		if ( v && *v ) {
			p += sprintf(p, "%-40s : %s %s\n", *v,
				avail & 0x1 ? (status & 0x1 ?
						"On " : "Off"): "",
				avail & 0x1 ? (control & 0x1 ?
						"Ctrl" : "NoCtrl"): "");
		} else {
			p += sprintf(p, "Feature set %2ld bit %2d\t\t\t"
					" : %s %s\n",
				set, i,
				avail & 0x1 ? (status & 0x1 ?
						"On " : "Off"): "",
				avail & 0x1 ? (control & 0x1 ?
						"Ctrl" : "NoCtrl"): "");
		}
	}
	return p;
}

static int
processor_info(char *page)
{
	char *p = page;
	u64 avail=1, status=1, control=1, feature_set=0;
	s64 ret;

	do {
		ret = ia64_pal_proc_get_features(&avail, &status, &control,
						feature_set);
		if (ret < 0) {
			return p - page;
		}
		if (ret == 1) {
			feature_set++;
			continue;
		}

		p = feature_set_info(p, avail, status, control, feature_set);

		feature_set++;
	} while(1);

	return p - page;
}

static const char *bus_features[]={
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL, NULL,NULL,
	NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,NULL,
	NULL,NULL,
	"Request  Bus Parking",
	"Bus Lock Mask",
	"Enable Half Transfer",
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL,
	NULL, NULL, NULL, NULL,
	"Enable Cache Line Repl. Shared",
	"Enable Cache Line Repl. Exclusive",
	"Disable Transaction Queuing",
	"Disable Response Error Checking",
	"Disable Bus Error Checking",
	"Disable Bus Requester Internal Error Signalling",
	"Disable Bus Requester Error Signalling",
	"Disable Bus Initialization Event Checking",
	"Disable Bus Initialization Event Signalling",
	"Disable Bus Address Error Checking",
	"Disable Bus Address Error Signalling",
	"Disable Bus Data Error Checking"
};


static int
bus_info(char *page)
{
	char *p = page;
	const char **v = bus_features;
	pal_bus_features_u_t av, st, ct;
	u64 avail, status, control;
	int i;
	s64 ret;

	if ((ret=ia64_pal_bus_get_features(&av, &st, &ct)) != 0) return 0;

	avail   = av.pal_bus_features_val;
	status  = st.pal_bus_features_val;
	control = ct.pal_bus_features_val;

	for(i=0; i < 64; i++, v++, avail >>=1, status >>=1, control >>=1) {
		if ( ! *v ) continue;
		p += sprintf(p, "%-48s : %s%s %s\n", *v,
				avail & 0x1 ? "" : "NotImpl",
				avail & 0x1 ? (status  & 0x1 ? "On" : "Off"): "",
				avail & 0x1 ? (control & 0x1 ? "Ctrl" : "NoCtrl"): "");
	}
	return p - page;
}

static int
version_info(char *page)
{
	pal_version_u_t min_ver, cur_ver;
	char *p = page;

	if (ia64_pal_version(&min_ver, &cur_ver) != 0)
		return 0;

	p += sprintf(p,
		     "PAL_vendor : 0x%02x (min=0x%02x)\n"
		     "PAL_A      : %02x.%02x (min=%02x.%02x)\n"
		     "PAL_B      : %02x.%02x (min=%02x.%02x)\n",
		     cur_ver.pal_version_s.pv_pal_vendor,
		     min_ver.pal_version_s.pv_pal_vendor,
		     cur_ver.pal_version_s.pv_pal_a_model,
		     cur_ver.pal_version_s.pv_pal_a_rev,
		     min_ver.pal_version_s.pv_pal_a_model,
		     min_ver.pal_version_s.pv_pal_a_rev,
		     cur_ver.pal_version_s.pv_pal_b_model,
		     cur_ver.pal_version_s.pv_pal_b_rev,
		     min_ver.pal_version_s.pv_pal_b_model,
		     min_ver.pal_version_s.pv_pal_b_rev);
	return p - page;
}

static int
perfmon_info(char *page)
{
	char *p = page;
	u64 pm_buffer[16];
	pal_perf_mon_info_u_t pm_info;

	if (ia64_pal_perf_mon_info(pm_buffer, &pm_info) != 0) return 0;

	p += sprintf(p,
		     "PMC/PMD pairs                 : %d\n"
		     "Counter width                 : %d bits\n"
		     "Cycle event number            : %d\n"
		     "Retired event number          : %d\n"
		     "Implemented PMC               : ",
		     pm_info.pal_perf_mon_info_s.generic, pm_info.pal_perf_mon_info_s.width,
		     pm_info.pal_perf_mon_info_s.cycles, pm_info.pal_perf_mon_info_s.retired);

	p = bitregister_process(p, pm_buffer, 256);
	p += sprintf(p, "\nImplemented PMD               : ");
	p = bitregister_process(p, pm_buffer+4, 256);
	p += sprintf(p, "\nCycles count capable          : ");
	p = bitregister_process(p, pm_buffer+8, 256);
	p += sprintf(p, "\nRetired bundles count capable : ");

#ifdef CONFIG_ITANIUM
	/*
	 * PAL_PERF_MON_INFO reports that only PMC4 can be used to count CPU_CYCLES
	 * which is wrong, both PMC4 and PMD5 support it.
	 */
	if (pm_buffer[12] == 0x10) pm_buffer[12]=0x30;
#endif

	p = bitregister_process(p, pm_buffer+12, 256);

	p += sprintf(p, "\n");

	return p - page;
}

static int
frequency_info(char *page)
{
	char *p = page;
	struct pal_freq_ratio proc, itc, bus;
	u64 base;

	if (ia64_pal_freq_base(&base) == -1)
		p += sprintf(p, "Output clock            : not implemented\n");
	else
		p += sprintf(p, "Output clock            : %ld ticks/s\n", base);

	if (ia64_pal_freq_ratios(&proc, &bus, &itc) != 0) return 0;

	p += sprintf(p,
		     "Processor/Clock ratio   : %d/%d\n"
		     "Bus/Clock ratio         : %d/%d\n"
		     "ITC/Clock ratio         : %d/%d\n",
		     proc.num, proc.den, bus.num, bus.den, itc.num, itc.den);

	return p - page;
}

static int
tr_info(char *page)
{
	char *p = page;
	s64 status;
	pal_tr_valid_u_t tr_valid;
	u64 tr_buffer[4];
	pal_vm_info_1_u_t vm_info_1;
	pal_vm_info_2_u_t vm_info_2;
	u64 i, j;
	u64 max[3], pgm;
	struct ifa_reg {
		u64 valid:1;
		u64 ig:11;
		u64 vpn:52;
	} *ifa_reg;
	struct itir_reg {
		u64 rv1:2;
		u64 ps:6;
		u64 key:24;
		u64 rv2:32;
	} *itir_reg;
	struct gr_reg {
		u64 p:1;
		u64 rv1:1;
		u64 ma:3;
		u64 a:1;
		u64 d:1;
		u64 pl:2;
		u64 ar:3;
		u64 ppn:38;
		u64 rv2:2;
		u64 ed:1;
		u64 ig:11;
	} *gr_reg;
	struct rid_reg {
		u64 ig1:1;
		u64 rv1:1;
		u64 ig2:6;
		u64 rid:24;
		u64 rv2:32;
	} *rid_reg;

	if ((status = ia64_pal_vm_summary(&vm_info_1, &vm_info_2)) !=0) {
		printk(KERN_ERR "ia64_pal_vm_summary=%ld\n", status);
		return 0;
	}
	max[0] = vm_info_1.pal_vm_info_1_s.max_itr_entry+1;
	max[1] = vm_info_1.pal_vm_info_1_s.max_dtr_entry+1;

	for (i=0; i < 2; i++ ) {
		for (j=0; j < max[i]; j++) {

		status = ia64_pal_tr_read(j, i, tr_buffer, &tr_valid);
		if (status != 0) {
			printk(KERN_ERR "palinfo: pal call failed on tr[%lu:%lu]=%ld\n",
			       i, j, status);
			continue;
		}

		ifa_reg  = (struct ifa_reg *)&tr_buffer[2];

		if (ifa_reg->valid == 0) continue;

		gr_reg   = (struct gr_reg *)tr_buffer;
		itir_reg = (struct itir_reg *)&tr_buffer[1];
		rid_reg  = (struct rid_reg *)&tr_buffer[3];

		pgm	 = -1 << (itir_reg->ps - 12);
		p += sprintf(p,
			     "%cTR%lu: av=%d pv=%d dv=%d mv=%d\n"
			     "\tppn  : 0x%lx\n"
			     "\tvpn  : 0x%lx\n"
			     "\tps   : ",
			     "ID"[i], j,
			     tr_valid.pal_tr_valid_s.access_rights_valid,
			     tr_valid.pal_tr_valid_s.priv_level_valid,
			     tr_valid.pal_tr_valid_s.dirty_bit_valid,
			     tr_valid.pal_tr_valid_s.mem_attr_valid,
			     (gr_reg->ppn & pgm)<< 12, (ifa_reg->vpn & pgm)<< 12);

		p = bitvector_process(p, 1<< itir_reg->ps);

		p += sprintf(p,
			     "\n\tpl   : %d\n"
			     "\tar   : %d\n"
			     "\trid  : %x\n"
			     "\tp    : %d\n"
			     "\tma   : %d\n"
			     "\td    : %d\n",
			     gr_reg->pl, gr_reg->ar, rid_reg->rid, gr_reg->p, gr_reg->ma,
			     gr_reg->d);
		}
	}
	return p - page;
}



/*
 * List {name,function} pairs for every entry in /proc/palinfo/cpu*
 */
static palinfo_entry_t palinfo_entries[]={
	{ "version_info",	version_info, },
	{ "vm_info",		vm_info, },
	{ "cache_info",		cache_info, },
	{ "power_info",		power_info, },
	{ "register_info",	register_info, },
	{ "processor_info",	processor_info, },
	{ "perfmon_info",	perfmon_info, },
	{ "frequency_info",	frequency_info, },
	{ "bus_info",		bus_info },
	{ "tr_info",		tr_info, }
};

#define NR_PALINFO_ENTRIES	(int) ARRAY_SIZE(palinfo_entries)

/*
 * this array is used to keep track of the proc entries we create. This is
 * required in the module mode when we need to remove all entries. The procfs code
 * does not do recursion of deletion
 *
 * Notes:
 *	- +1 accounts for the cpuN directory entry in /proc/pal
 */
#define NR_PALINFO_PROC_ENTRIES	(NR_CPUS*(NR_PALINFO_ENTRIES+1))

static struct proc_dir_entry *palinfo_proc_entries[NR_PALINFO_PROC_ENTRIES];
static struct proc_dir_entry *palinfo_dir;

/*
 * This data structure is used to pass which cpu,function is being requested
 * It must fit in a 64bit quantity to be passed to the proc callback routine
 *
 * In SMP mode, when we get a request for another CPU, we must call that
 * other CPU using IPI and wait for the result before returning.
 */
typedef union {
	u64 value;
	struct {
		unsigned	req_cpu: 32;	/* for which CPU this info is */
		unsigned	func_id: 32;	/* which function is requested */
	} pal_func_cpu;
} pal_func_cpu_u_t;

#define req_cpu	pal_func_cpu.req_cpu
#define func_id pal_func_cpu.func_id

#ifdef CONFIG_SMP

/*
 * used to hold information about final function to call
 */
typedef struct {
	palinfo_func_t	func;	/* pointer to function to call */
	char		*page;	/* buffer to store results */
	int		ret;	/* return value from call */
} palinfo_smp_data_t;


/*
 * this function does the actual final call and he called
 * from the smp code, i.e., this is the palinfo callback routine
 */
static void
palinfo_smp_call(void *info)
{
	palinfo_smp_data_t *data = (palinfo_smp_data_t *)info;
	data->ret = (*data->func)(data->page);
}

/*
 * function called to trigger the IPI, we need to access a remote CPU
 * Return:
 *	0 : error or nothing to output
 *	otherwise how many bytes in the "page" buffer were written
 */
static
int palinfo_handle_smp(pal_func_cpu_u_t *f, char *page)
{
	palinfo_smp_data_t ptr;
	int ret;

	ptr.func = palinfo_entries[f->func_id].proc_read;
	ptr.page = page;
	ptr.ret  = 0; /* just in case */


	/* will send IPI to other CPU and wait for completion of remote call */
	if ((ret=smp_call_function_single(f->req_cpu, palinfo_smp_call, &ptr, 0, 1))) {
		printk(KERN_ERR "palinfo: remote CPU call from %d to %d on function %d: "
		       "error %d\n", smp_processor_id(), f->req_cpu, f->func_id, ret);
		return 0;
	}
	return ptr.ret;
}
#else /* ! CONFIG_SMP */
static
int palinfo_handle_smp(pal_func_cpu_u_t *f, char *page)
{
	printk(KERN_ERR "palinfo: should not be called with non SMP kernel\n");
	return 0;
}
#endif /* CONFIG_SMP */

/*
 * Entry point routine: all calls go through this function
 */
static int
palinfo_read_entry(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	int len=0;
	pal_func_cpu_u_t *f = (pal_func_cpu_u_t *)&data;

	/*
	 * in SMP mode, we may need to call another CPU to get correct
	 * information. PAL, by definition, is processor specific
	 */
	if (f->req_cpu == get_cpu())
		len = (*palinfo_entries[f->func_id].proc_read)(page);
	else
		len = palinfo_handle_smp(f, page);

	put_cpu();

	if (len <= off+count) *eof = 1;

	*start = page + off;
	len   -= off;

	if (len>count) len = count;
	if (len<0) len = 0;

	return len;
}

static void __cpuinit
create_palinfo_proc_entries(unsigned int cpu)
{
#	define CPUSTR	"cpu%d"

	pal_func_cpu_u_t f;
	struct proc_dir_entry **pdir;
	struct proc_dir_entry *cpu_dir;
	int j;
	char cpustr[sizeof(CPUSTR)];


	/*
	 * we keep track of created entries in a depth-first order for
	 * cleanup purposes. Each entry is stored into palinfo_proc_entries
	 */
	sprintf(cpustr,CPUSTR, cpu);

	cpu_dir = proc_mkdir(cpustr, palinfo_dir);

	f.req_cpu = cpu;

	/*
	 * Compute the location to store per cpu entries
	 * We dont store the top level entry in this list, but
	 * remove it finally after removing all cpu entries.
	 */
	pdir = &palinfo_proc_entries[cpu*(NR_PALINFO_ENTRIES+1)];
	*pdir++ = cpu_dir;
	for (j=0; j < NR_PALINFO_ENTRIES; j++) {
		f.func_id = j;
		*pdir = create_proc_read_entry(
				palinfo_entries[j].name, 0, cpu_dir,
				palinfo_read_entry, (void *)f.value);
		if (*pdir)
			(*pdir)->owner = THIS_MODULE;
		pdir++;
	}
}

static void
remove_palinfo_proc_entries(unsigned int hcpu)
{
	int j;
	struct proc_dir_entry *cpu_dir, **pdir;

	pdir = &palinfo_proc_entries[hcpu*(NR_PALINFO_ENTRIES+1)];
	cpu_dir = *pdir;
	*pdir++=NULL;
	for (j=0; j < (NR_PALINFO_ENTRIES); j++) {
		if ((*pdir)) {
			remove_proc_entry ((*pdir)->name, cpu_dir);
			*pdir ++= NULL;
		}
	}

	if (cpu_dir) {
		remove_proc_entry(cpu_dir->name, palinfo_dir);
	}
}

static int __cpuinit palinfo_cpu_callback(struct notifier_block *nfb,
					unsigned long action, void *hcpu)
{
	unsigned int hotcpu = (unsigned long)hcpu;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		create_palinfo_proc_entries(hotcpu);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		remove_palinfo_proc_entries(hotcpu);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __refdata palinfo_cpu_notifier =
{
	.notifier_call = palinfo_cpu_callback,
	.priority = 0,
};

static int __init
palinfo_init(void)
{
	int i = 0;

	printk(KERN_INFO "PAL Information Facility v%s\n", PALINFO_VERSION);
	palinfo_dir = proc_mkdir("pal", NULL);

	/* Create palinfo dirs in /proc for all online cpus */
	for_each_online_cpu(i) {
		create_palinfo_proc_entries(i);
	}

	/* Register for future delivery via notify registration */
	register_hotcpu_notifier(&palinfo_cpu_notifier);

	return 0;
}

static void __exit
palinfo_exit(void)
{
	int i = 0;

	/* remove all nodes: depth first pass. Could optimize this  */
	for_each_online_cpu(i) {
		remove_palinfo_proc_entries(i);
	}

	/*
	 * Remove the top level entry finally
	 */
	remove_proc_entry(palinfo_dir->name, NULL);

	/*
	 * Unregister from cpu notifier callbacks
	 */
	unregister_hotcpu_notifier(&palinfo_cpu_notifier);
}

module_init(palinfo_init);
module_exit(palinfo_exit);
