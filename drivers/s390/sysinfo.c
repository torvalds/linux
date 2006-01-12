/*
 *  drivers/s390/sysinfo.c
 *
 *    Copyright (C) 2001 IBM Deutschland Entwicklung GmbH, IBM Corporation
 *    Author(s): Ulrich Weigand (Ulrich.Weigand@de.ibm.com)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/proc_fs.h>
#include <linux/init.h>
#include <asm/ebcdic.h>

struct sysinfo_1_1_1
{
	char reserved_0[32];
	char manufacturer[16];
	char type[4];
	char reserved_1[12];
	char model[16];
	char sequence[16];
	char plant[4];
};

struct sysinfo_1_2_1
{
	char reserved_0[80];
	char sequence[16];
	char plant[4];
	char reserved_1[2];
	unsigned short cpu_address;
};

struct sysinfo_1_2_2
{
	char reserved_0[32];
	unsigned int capability;
	unsigned short cpus_total;
	unsigned short cpus_configured;
	unsigned short cpus_standby;
	unsigned short cpus_reserved;
	unsigned short adjustment[0];
};

struct sysinfo_2_2_1
{
	char reserved_0[80];
	char sequence[16];
	char plant[4];
	unsigned short cpu_id;
	unsigned short cpu_address;
};

struct sysinfo_2_2_2
{
	char reserved_0[32];
	unsigned short lpar_number;
	char reserved_1;
	unsigned char characteristics;
	#define LPAR_CHAR_DEDICATED	(1 << 7)
	#define LPAR_CHAR_SHARED	(1 << 6)
	#define LPAR_CHAR_LIMITED	(1 << 5)
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

struct sysinfo_3_2_2
{
	char reserved_0[31];
	unsigned char count;
	struct
	{
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

union s390_sysinfo
{
	struct sysinfo_1_1_1 sysinfo_1_1_1;
	struct sysinfo_1_2_1 sysinfo_1_2_1;
	struct sysinfo_1_2_2 sysinfo_1_2_2;
	struct sysinfo_2_2_1 sysinfo_2_2_1;
	struct sysinfo_2_2_2 sysinfo_2_2_2;
	struct sysinfo_3_2_2 sysinfo_3_2_2;
};

static inline int stsi (void *sysinfo, 
                        int fc, int sel1, int sel2)
{
	int cc, retv;

#ifndef CONFIG_64BIT
	__asm__ __volatile__ (	"lr\t0,%2\n"
				"\tlr\t1,%3\n"
				"\tstsi\t0(%4)\n"
				"0:\tipm\t%0\n"
				"\tsrl\t%0,28\n"
				"1:lr\t%1,0\n"
				".section .fixup,\"ax\"\n"
				"2:\tlhi\t%0,3\n"
				"\tbras\t1,3f\n"
				"\t.long 1b\n"
				"3:\tl\t1,0(1)\n"
				"\tbr\t1\n"
				".previous\n"
				".section __ex_table,\"a\"\n"
				"\t.align 4\n"
				"\t.long 0b,2b\n"
				".previous\n"
				: "=d" (cc), "=d" (retv)
				: "d" ((fc << 28) | sel1), "d" (sel2), "a" (sysinfo) 
				: "cc", "memory", "0", "1" );
#else
	__asm__ __volatile__ (	"lr\t0,%2\n"
				"lr\t1,%3\n"
				"\tstsi\t0(%4)\n"
				"0:\tipm\t%0\n"
				"\tsrl\t%0,28\n"
				"1:lr\t%1,0\n"
				".section .fixup,\"ax\"\n"
				"2:\tlhi\t%0,3\n"
				"\tjg\t1b\n"
				".previous\n"
				".section __ex_table,\"a\"\n"
				"\t.align 8\n"
				"\t.quad 0b,2b\n"
				".previous\n"
				: "=d" (cc), "=d" (retv)
				: "d" ((fc << 28) | sel1), "d" (sel2), "a" (sysinfo) 
				: "cc", "memory", "0", "1" );
#endif

	return cc? -1 : retv;
}

static inline int stsi_0 (void)
{
	int rc = stsi (NULL, 0, 0, 0);
	return rc == -1 ? rc : (((unsigned int)rc) >> 28);
}

static inline int stsi_1_1_1 (struct sysinfo_1_1_1 *info)
{
	int rc = stsi (info, 1, 1, 1);
	if (rc != -1)
	{
		EBCASC (info->manufacturer, sizeof(info->manufacturer));
		EBCASC (info->type, sizeof(info->type));
		EBCASC (info->model, sizeof(info->model));
		EBCASC (info->sequence, sizeof(info->sequence));
		EBCASC (info->plant, sizeof(info->plant));
	}
	return rc == -1 ? rc : 0;
}

static inline int stsi_1_2_1 (struct sysinfo_1_2_1 *info)
{
	int rc = stsi (info, 1, 2, 1);
	if (rc != -1)
	{
		EBCASC (info->sequence, sizeof(info->sequence));
		EBCASC (info->plant, sizeof(info->plant));
	}
	return rc == -1 ? rc : 0;
}

static inline int stsi_1_2_2 (struct sysinfo_1_2_2 *info)
{
	int rc = stsi (info, 1, 2, 2);
	return rc == -1 ? rc : 0;
}

static inline int stsi_2_2_1 (struct sysinfo_2_2_1 *info)
{
	int rc = stsi (info, 2, 2, 1);
	if (rc != -1)
	{
		EBCASC (info->sequence, sizeof(info->sequence));
		EBCASC (info->plant, sizeof(info->plant));
	}
	return rc == -1 ? rc : 0;
}

static inline int stsi_2_2_2 (struct sysinfo_2_2_2 *info)
{
	int rc = stsi (info, 2, 2, 2);
	if (rc != -1)
	{
		EBCASC (info->name, sizeof(info->name));
  	}
	return rc == -1 ? rc : 0;
}

static inline int stsi_3_2_2 (struct sysinfo_3_2_2 *info)
{
	int rc = stsi (info, 3, 2, 2);
	if (rc != -1)
	{
		int i;
		for (i = 0; i < info->count; i++)
		{
			EBCASC (info->vm[i].name, sizeof(info->vm[i].name));
			EBCASC (info->vm[i].cpi, sizeof(info->vm[i].cpi));
		}
	}
	return rc == -1 ? rc : 0;
}


static int proc_read_sysinfo(char *page, char **start,
                             off_t off, int count,
                             int *eof, void *data)
{
	unsigned long info_page = get_zeroed_page (GFP_KERNEL); 
	union s390_sysinfo *info = (union s390_sysinfo *) info_page;
	int len = 0;
	int level;
	int i;
	
	if (!info)
		return 0;

	level = stsi_0 ();

	if (level >= 1 && stsi_1_1_1 (&info->sysinfo_1_1_1) == 0)
	{
		len += sprintf (page+len, "Manufacturer:         %-16.16s\n",
				info->sysinfo_1_1_1.manufacturer);
		len += sprintf (page+len, "Type:                 %-4.4s\n",
				info->sysinfo_1_1_1.type);
		len += sprintf (page+len, "Model:                %-16.16s\n",
				info->sysinfo_1_1_1.model);
		len += sprintf (page+len, "Sequence Code:        %-16.16s\n",
				info->sysinfo_1_1_1.sequence);
		len += sprintf (page+len, "Plant:                %-4.4s\n",
				info->sysinfo_1_1_1.plant);
	}

	if (level >= 1 && stsi_1_2_2 (&info->sysinfo_1_2_2) == 0)
	{
		len += sprintf (page+len, "\n");
		len += sprintf (page+len, "CPUs Total:           %d\n",
				info->sysinfo_1_2_2.cpus_total);
		len += sprintf (page+len, "CPUs Configured:      %d\n",
				info->sysinfo_1_2_2.cpus_configured);
		len += sprintf (page+len, "CPUs Standby:         %d\n",
				info->sysinfo_1_2_2.cpus_standby);
		len += sprintf (page+len, "CPUs Reserved:        %d\n",
				info->sysinfo_1_2_2.cpus_reserved);
	
		len += sprintf (page+len, "Capability:           %d\n",
				info->sysinfo_1_2_2.capability);

		for (i = 2; i <= info->sysinfo_1_2_2.cpus_total; i++)
			len += sprintf (page+len, "Adjustment %02d-way:    %d\n",
					i, info->sysinfo_1_2_2.adjustment[i-2]);
	}

	if (level >= 2 && stsi_2_2_2 (&info->sysinfo_2_2_2) == 0)
	{
		len += sprintf (page+len, "\n");
		len += sprintf (page+len, "LPAR Number:          %d\n",
				info->sysinfo_2_2_2.lpar_number);

		len += sprintf (page+len, "LPAR Characteristics: ");
		if (info->sysinfo_2_2_2.characteristics & LPAR_CHAR_DEDICATED)
			len += sprintf (page+len, "Dedicated ");
		if (info->sysinfo_2_2_2.characteristics & LPAR_CHAR_SHARED)
			len += sprintf (page+len, "Shared ");
		if (info->sysinfo_2_2_2.characteristics & LPAR_CHAR_LIMITED)
			len += sprintf (page+len, "Limited ");
		len += sprintf (page+len, "\n");
	
		len += sprintf (page+len, "LPAR Name:            %-8.8s\n",
				info->sysinfo_2_2_2.name);
	
		len += sprintf (page+len, "LPAR Adjustment:      %d\n",
				info->sysinfo_2_2_2.caf);
	
		len += sprintf (page+len, "LPAR CPUs Total:      %d\n",
				info->sysinfo_2_2_2.cpus_total);
		len += sprintf (page+len, "LPAR CPUs Configured: %d\n",
				info->sysinfo_2_2_2.cpus_configured);
		len += sprintf (page+len, "LPAR CPUs Standby:    %d\n",
				info->sysinfo_2_2_2.cpus_standby);
		len += sprintf (page+len, "LPAR CPUs Reserved:   %d\n",
				info->sysinfo_2_2_2.cpus_reserved);
		len += sprintf (page+len, "LPAR CPUs Dedicated:  %d\n",
				info->sysinfo_2_2_2.cpus_dedicated);
		len += sprintf (page+len, "LPAR CPUs Shared:     %d\n",
				info->sysinfo_2_2_2.cpus_shared);
	}

	if (level >= 3 && stsi_3_2_2 (&info->sysinfo_3_2_2) == 0)
	{
		for (i = 0; i < info->sysinfo_3_2_2.count; i++)
		{
			len += sprintf (page+len, "\n");
			len += sprintf (page+len, "VM%02d Name:            %-8.8s\n",
					i, info->sysinfo_3_2_2.vm[i].name);
			len += sprintf (page+len, "VM%02d Control Program: %-16.16s\n",
					i, info->sysinfo_3_2_2.vm[i].cpi);
	
			len += sprintf (page+len, "VM%02d Adjustment:      %d\n",
					i, info->sysinfo_3_2_2.vm[i].caf);
	
			len += sprintf (page+len, "VM%02d CPUs Total:      %d\n",
					i, info->sysinfo_3_2_2.vm[i].cpus_total);
			len += sprintf (page+len, "VM%02d CPUs Configured: %d\n",
					i, info->sysinfo_3_2_2.vm[i].cpus_configured);
			len += sprintf (page+len, "VM%02d CPUs Standby:    %d\n",
					i, info->sysinfo_3_2_2.vm[i].cpus_standby);
			len += sprintf (page+len, "VM%02d CPUs Reserved:   %d\n",
					i, info->sysinfo_3_2_2.vm[i].cpus_reserved);
		}
	}

	free_page (info_page);
        return len;
}

static __init int create_proc_sysinfo(void)
{
	create_proc_read_entry ("sysinfo", 0444, NULL, 
				proc_read_sysinfo, NULL);
	return 0;
}

__initcall(create_proc_sysinfo);

