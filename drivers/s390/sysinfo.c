/*
 *  drivers/s390/sysinfo.c
 *
 *    Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ulrich Weigand (Ulrich.Weigand@de.ibm.com)
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <asm/ebcdic.h>

/* Sigh, math-emu. Don't ask. */
#include <asm/sfp-util.h>
#include <math-emu/soft-fp.h>
#include <math-emu/single.h>

struct sysinfo_1_1_1 {
	char reserved_0[32];
	char manufacturer[16];
	char type[4];
	char reserved_1[12];
	char model_capacity[16];
	char sequence[16];
	char plant[4];
	char model[16];
};

struct sysinfo_1_2_1 {
	char reserved_0[80];
	char sequence[16];
	char plant[4];
	char reserved_1[2];
	unsigned short cpu_address;
};

struct sysinfo_1_2_2 {
	char format;
	char reserved_0[1];
	unsigned short acc_offset;
	char reserved_1[24];
	unsigned int secondary_capability;
	unsigned int capability;
	unsigned short cpus_total;
	unsigned short cpus_configured;
	unsigned short cpus_standby;
	unsigned short cpus_reserved;
	unsigned short adjustment[0];
};

struct sysinfo_1_2_2_extension {
	unsigned int alt_capability;
	unsigned short alt_adjustment[0];
};

struct sysinfo_2_2_1 {
	char reserved_0[80];
	char sequence[16];
	char plant[4];
	unsigned short cpu_id;
	unsigned short cpu_address;
};

struct sysinfo_2_2_2 {
	char reserved_0[32];
	unsigned short lpar_number;
	char reserved_1;
	unsigned char characteristics;
	unsigned short cpus_total;
	unsigned short cpus_configured;
	unsigned short cpus_standby;
	unsigned short cpus_reserved;
	char name[8];
	unsigned int caf;
	char reserved_2[16];
	unsigned short cpus_dedicated;
	unsigned short cpus_shared;
};

#define LPAR_CHAR_DEDICATED	(1 << 7)
#define LPAR_CHAR_SHARED	(1 << 6)
#define LPAR_CHAR_LIMITED	(1 << 5)

struct sysinfo_3_2_2 {
	char reserved_0[31];
	unsigned char count;
	struct {
		char reserved_0[4];
		unsigned short cpus_total;
		unsigned short cpus_configured;
		unsigned short cpus_standby;
		unsigned short cpus_reserved;
		char name[8];
		unsigned int caf;
		char cpi[16];
		char reserved_1[24];

	} vm[8];
};

static inline int stsi(void *sysinfo, int fc, int sel1, int sel2)
{
	register int r0 asm("0") = (fc << 28) | sel1;
	register int r1 asm("1") = sel2;

	asm volatile(
		"   stsi 0(%2)\n"
		"0: jz   2f\n"
		"1: lhi  %0,%3\n"
		"2:\n"
		EX_TABLE(0b,1b)
		: "+d" (r0) : "d" (r1), "a" (sysinfo), "K" (-ENOSYS)
		: "cc", "memory" );
	return r0;
}

static inline int stsi_0(void)
{
	int rc = stsi (NULL, 0, 0, 0);
	return rc == -ENOSYS ? rc : (((unsigned int) rc) >> 28);
}

static int stsi_1_1_1(struct sysinfo_1_1_1 *info, char *page, int len)
{
	if (stsi(info, 1, 1, 1) == -ENOSYS)
		return len;

	EBCASC(info->manufacturer, sizeof(info->manufacturer));
	EBCASC(info->type, sizeof(info->type));
	EBCASC(info->model, sizeof(info->model));
	EBCASC(info->sequence, sizeof(info->sequence));
	EBCASC(info->plant, sizeof(info->plant));
	EBCASC(info->model_capacity, sizeof(info->model_capacity));
	len += sprintf(page + len, "Manufacturer:         %-16.16s\n",
		       info->manufacturer);
	len += sprintf(page + len, "Type:                 %-4.4s\n",
		       info->type);
	if (info->model[0] != '\0')
		/*
		 * Sigh: the model field has been renamed with System z9
		 * to model_capacity and a new model field has been added
		 * after the plant field. To avoid confusing older programs
		 * the "Model:" prints "model_capacity model" or just
		 * "model_capacity" if the model string is empty .
		 */
		len += sprintf(page + len,
			       "Model:                %-16.16s %-16.16s\n",
			       info->model_capacity, info->model);
	else
		len += sprintf(page + len, "Model:                %-16.16s\n",
			       info->model_capacity);
	len += sprintf(page + len, "Sequence Code:        %-16.16s\n",
		       info->sequence);
	len += sprintf(page + len, "Plant:                %-4.4s\n",
		       info->plant);
	len += sprintf(page + len, "Model Capacity:       %-16.16s\n",
		       info->model_capacity);
	return len;
}

#if 0 /* Currently unused */
static int stsi_1_2_1(struct sysinfo_1_2_1 *info, char *page, int len)
{
	if (stsi(info, 1, 2, 1) == -ENOSYS)
		return len;

	len += sprintf(page + len, "\n");
	EBCASC(info->sequence, sizeof(info->sequence));
	EBCASC(info->plant, sizeof(info->plant));
	len += sprintf(page + len, "Sequence Code of CPU: %-16.16s\n",
		       info->sequence);
	len += sprintf(page + len, "Plant of CPU:         %-16.16s\n",
		       info->plant);
	return len;
}
#endif

static int stsi_1_2_2(struct sysinfo_1_2_2 *info, char *page, int len)
{
	struct sysinfo_1_2_2_extension *ext;
	int i;

	if (stsi(info, 1, 2, 2) == -ENOSYS)
		return len;
	ext = (struct sysinfo_1_2_2_extension *)
		((unsigned long) info + info->acc_offset);

	len += sprintf(page + len, "\n");
	len += sprintf(page + len, "CPUs Total:           %d\n",
		       info->cpus_total);
	len += sprintf(page + len, "CPUs Configured:      %d\n",
		       info->cpus_configured);
	len += sprintf(page + len, "CPUs Standby:         %d\n",
		       info->cpus_standby);
	len += sprintf(page + len, "CPUs Reserved:        %d\n",
		       info->cpus_reserved);

	if (info->format == 1) {
		/*
		 * Sigh 2. According to the specification the alternate
		 * capability field is a 32 bit floating point number
		 * if the higher order 8 bits are not zero. Printing
		 * a floating point number in the kernel is a no-no,
		 * always print the number as 32 bit unsigned integer.
		 * The user-space needs to know about the strange
		 * encoding of the alternate cpu capability.
		 */
		len += sprintf(page + len, "Capability:           %u %u\n",
			       info->capability, ext->alt_capability);
		for (i = 2; i <= info->cpus_total; i++)
			len += sprintf(page + len,
				       "Adjustment %02d-way:    %u %u\n",
				       i, info->adjustment[i-2],
				       ext->alt_adjustment[i-2]);

	} else {
		len += sprintf(page + len, "Capability:           %u\n",
			       info->capability);
		for (i = 2; i <= info->cpus_total; i++)
			len += sprintf(page + len,
				       "Adjustment %02d-way:    %u\n",
				       i, info->adjustment[i-2]);
	}

	if (info->secondary_capability != 0)
		len += sprintf(page + len, "Secondary Capability: %d\n",
			       info->secondary_capability);

	return len;
}

#if 0 /* Currently unused */
static int stsi_2_2_1(struct sysinfo_2_2_1 *info, char *page, int len)
{
	if (stsi(info, 2, 2, 1) == -ENOSYS)
		return len;

	len += sprintf(page + len, "\n");
	EBCASC (info->sequence, sizeof(info->sequence));
	EBCASC (info->plant, sizeof(info->plant));
	len += sprintf(page + len, "Sequence Code of logical CPU: %-16.16s\n",
		       info->sequence);
	len += sprintf(page + len, "Plant of logical CPU: %-16.16s\n",
		       info->plant);
	return len;
}
#endif

static int stsi_2_2_2(struct sysinfo_2_2_2 *info, char *page, int len)
{
	if (stsi(info, 2, 2, 2) == -ENOSYS)
		return len;

	EBCASC (info->name, sizeof(info->name));

	len += sprintf(page + len, "\n");
	len += sprintf(page + len, "LPAR Number:          %d\n",
		       info->lpar_number);

	len += sprintf(page + len, "LPAR Characteristics: ");
	if (info->characteristics & LPAR_CHAR_DEDICATED)
		len += sprintf(page + len, "Dedicated ");
	if (info->characteristics & LPAR_CHAR_SHARED)
		len += sprintf(page + len, "Shared ");
	if (info->characteristics & LPAR_CHAR_LIMITED)
		len += sprintf(page + len, "Limited ");
	len += sprintf(page + len, "\n");

	len += sprintf(page + len, "LPAR Name:            %-8.8s\n",
		       info->name);

	len += sprintf(page + len, "LPAR Adjustment:      %d\n",
		       info->caf);

	len += sprintf(page + len, "LPAR CPUs Total:      %d\n",
		       info->cpus_total);
	len += sprintf(page + len, "LPAR CPUs Configured: %d\n",
		       info->cpus_configured);
	len += sprintf(page + len, "LPAR CPUs Standby:    %d\n",
		       info->cpus_standby);
	len += sprintf(page + len, "LPAR CPUs Reserved:   %d\n",
		       info->cpus_reserved);
	len += sprintf(page + len, "LPAR CPUs Dedicated:  %d\n",
		       info->cpus_dedicated);
	len += sprintf(page + len, "LPAR CPUs Shared:     %d\n",
		       info->cpus_shared);
	return len;
}

static int stsi_3_2_2(struct sysinfo_3_2_2 *info, char *page, int len)
{
	int i;

	if (stsi(info, 3, 2, 2) == -ENOSYS)
		return len;
	for (i = 0; i < info->count; i++) {
		EBCASC (info->vm[i].name, sizeof(info->vm[i].name));
		EBCASC (info->vm[i].cpi, sizeof(info->vm[i].cpi));
		len += sprintf(page + len, "\n");
		len += sprintf(page + len, "VM%02d Name:            %-8.8s\n",
			       i, info->vm[i].name);
		len += sprintf(page + len, "VM%02d Control Program: %-16.16s\n",
			       i, info->vm[i].cpi);

		len += sprintf(page + len, "VM%02d Adjustment:      %d\n",
			       i, info->vm[i].caf);

		len += sprintf(page + len, "VM%02d CPUs Total:      %d\n",
			       i, info->vm[i].cpus_total);
		len += sprintf(page + len, "VM%02d CPUs Configured: %d\n",
			       i, info->vm[i].cpus_configured);
		len += sprintf(page + len, "VM%02d CPUs Standby:    %d\n",
			       i, info->vm[i].cpus_standby);
		len += sprintf(page + len, "VM%02d CPUs Reserved:   %d\n",
			       i, info->vm[i].cpus_reserved);
	}
	return len;
}


static int proc_read_sysinfo(char *page, char **start,
                             off_t off, int count,
                             int *eof, void *data)
{
	unsigned long info = get_zeroed_page (GFP_KERNEL);
	int level, len;
	
	if (!info)
		return 0;

	len = 0;
	level = stsi_0();
	if (level >= 1)
		len = stsi_1_1_1((struct sysinfo_1_1_1 *) info, page, len);

	if (level >= 1)
		len = stsi_1_2_2((struct sysinfo_1_2_2 *) info, page, len);

	if (level >= 2)
		len = stsi_2_2_2((struct sysinfo_2_2_2 *) info, page, len);

	if (level >= 3)
		len = stsi_3_2_2((struct sysinfo_3_2_2 *) info, page, len);

	free_page (info);
        return len;
}

static __init int create_proc_sysinfo(void)
{
	create_proc_read_entry("sysinfo", 0444, NULL,
			       proc_read_sysinfo, NULL);
	return 0;
}

__initcall(create_proc_sysinfo);

int get_cpu_capability(unsigned int *capability)
{
	struct sysinfo_1_2_2 *info;
	int rc;

	info = (void *) get_zeroed_page(GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	rc = stsi(info, 1, 2, 2);
	if (rc == -ENOSYS)
		goto out;
	rc = 0;
	*capability = info->capability;
out:
	free_page((unsigned long) info);
	return rc;
}

/*
 * CPU capability might have changed. Therefore recalculate loops_per_jiffy.
 */
void s390_adjust_jiffies(void)
{
	struct sysinfo_1_2_2 *info;
	const unsigned int fmil = 0x4b189680;	/* 1e7 as 32-bit float. */
	FP_DECL_S(SA); FP_DECL_S(SB); FP_DECL_S(SR);
	FP_DECL_EX;
	unsigned int capability;

	info = (void *) get_zeroed_page(GFP_KERNEL);
	if (!info)
		return;

	if (stsi(info, 1, 2, 2) != -ENOSYS) {
		/*
		 * Major sigh. The cpu capability encoding is "special".
		 * If the first 9 bits of info->capability are 0 then it
		 * is a 32 bit unsigned integer in the range 0 .. 2^23.
		 * If the first 9 bits are != 0 then it is a 32 bit float.
		 * In addition a lower value indicates a proportionally
		 * higher cpu capacity. Bogomips are the other way round.
		 * To get to a halfway suitable number we divide 1e7
		 * by the cpu capability number. Yes, that means a floating
		 * point division .. math-emu here we come :-)
		 */
		FP_UNPACK_SP(SA, &fmil);
		if ((info->capability >> 23) == 0)
			FP_FROM_INT_S(SB, info->capability, 32, int);
		else
			FP_UNPACK_SP(SB, &info->capability);
		FP_DIV_S(SR, SA, SB);
		FP_TO_INT_S(capability, SR, 32, 0);
	} else
		/*
		 * Really old machine without stsi block for basic
		 * cpu information. Report 42.0 bogomips.
		 */
		capability = 42;
	loops_per_jiffy = capability * (500000/HZ);
	free_page((unsigned long) info);
}

/*
 * calibrate the delay loop
 */
void __cpuinit calibrate_delay(void)
{
	s390_adjust_jiffies();
	/* Print the good old Bogomips line .. */
	printk(KERN_DEBUG "Calibrating delay loop (skipped)... "
	       "%lu.%02lu BogoMIPS preset\n", loops_per_jiffy/(500000/HZ),
	       (loops_per_jiffy/(5000/HZ)) % 100);
}
