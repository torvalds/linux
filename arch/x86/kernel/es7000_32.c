/*
 * Written by: Garry Forsgren, Unisys Corporation
 *             Natalie Protasevich, Unisys Corporation
 * This file contains the code to configure and interface
 * with Unisys ES7000 series hardware system manager.
 *
 * Copyright (c) 2003 Unisys Corporation.  All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write the Free Software Foundation, Inc., 59
 * Temple Place - Suite 330, Boston MA 02111-1307, USA.
 *
 * Contact information: Unisys Corporation, Township Line & Union Meeting
 * Roads-A, Unisys Way, Blue Bell, Pennsylvania, 19424, or:
 *
 * http://www.unisys.com
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/notifier.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <asm/io.h>
#include <asm/nmi.h>
#include <asm/smp.h>
#include <asm/apicdef.h>
#include <mach_mpparse.h>

/*
 * ES7000 chipsets
 */

#define NON_UNISYS		0
#define ES7000_CLASSIC		1
#define ES7000_ZORRO		2


#define	MIP_REG			1
#define	MIP_PSAI_REG		4

#define	MIP_BUSY		1
#define	MIP_SPIN		0xf0000
#define	MIP_VALID		0x0100000000000000ULL
#define	MIP_PORT(VALUE)	((VALUE >> 32) & 0xffff)

#define	MIP_RD_LO(VALUE)	(VALUE & 0xffffffff)

struct mip_reg_info {
	unsigned long long mip_info;
	unsigned long long delivery_info;
	unsigned long long host_reg;
	unsigned long long mip_reg;
};

struct part_info {
	unsigned char type;
	unsigned char length;
	unsigned char part_id;
	unsigned char apic_mode;
	unsigned long snum;
	char ptype[16];
	char sname[64];
	char pname[64];
};

struct psai {
	unsigned long long entry_type;
	unsigned long long addr;
	unsigned long long bep_addr;
};

struct es7000_mem_info {
	unsigned char type;
	unsigned char length;
	unsigned char resv[6];
	unsigned long long  start;
	unsigned long long  size;
};

struct es7000_oem_table {
	unsigned long long hdr;
	struct mip_reg_info mip;
	struct part_info pif;
	struct es7000_mem_info shm;
	struct psai psai;
};

#ifdef CONFIG_ACPI

struct oem_table {
	struct acpi_table_header Header;
	u32 OEMTableAddr;
	u32 OEMTableSize;
};

extern int find_unisys_acpi_oem_table(unsigned long *oem_addr);
#endif

struct mip_reg {
	unsigned long long off_0;
	unsigned long long off_8;
	unsigned long long off_10;
	unsigned long long off_18;
	unsigned long long off_20;
	unsigned long long off_28;
	unsigned long long off_30;
	unsigned long long off_38;
};

#define	MIP_SW_APIC		0x1020b
#define	MIP_FUNC(VALUE)		(VALUE & 0xff)

/*
 * ES7000 Globals
 */

static volatile unsigned long	*psai = NULL;
static struct mip_reg		*mip_reg;
static struct mip_reg		*host_reg;
static int 			mip_port;
static unsigned long		mip_addr, host_addr;

int es7000_plat;

/*
 * GSI override for ES7000 platforms.
 */

static unsigned int base;

static int
es7000_rename_gsi(int ioapic, int gsi)
{
	if (es7000_plat == ES7000_ZORRO)
		return gsi;

	if (!base) {
		int i;
		for (i = 0; i < nr_ioapics; i++)
			base += nr_ioapic_registers[i];
	}

	if (!ioapic && (gsi < 16))
		gsi += base;
	return gsi;
}

void __init
setup_unisys(void)
{
	/*
	 * Determine the generation of the ES7000 currently running.
	 *
	 * es7000_plat = 1 if the machine is a 5xx ES7000 box
	 * es7000_plat = 2 if the machine is a x86_64 ES7000 box
	 *
	 */
	if (!(boot_cpu_data.x86 <= 15 && boot_cpu_data.x86_model <= 2))
		es7000_plat = ES7000_ZORRO;
	else
		es7000_plat = ES7000_CLASSIC;
	ioapic_renumber_irq = es7000_rename_gsi;
}

/*
 * Parse the OEM Table
 */

int __init
parse_unisys_oem (char *oemptr)
{
	int                     i;
	int 			success = 0;
	unsigned char           type, size;
	unsigned long           val;
	char                    *tp = NULL;
	struct psai             *psaip = NULL;
	struct mip_reg_info 	*mi;
	struct mip_reg		*host, *mip;

	tp = oemptr;

	tp += 8;

	for (i=0; i <= 6; i++) {
		type = *tp++;
		size = *tp++;
		tp -= 2;
		switch (type) {
		case MIP_REG:
			mi = (struct mip_reg_info *)tp;
			val = MIP_RD_LO(mi->host_reg);
			host_addr = val;
			host = (struct mip_reg *)val;
			host_reg = __va(host);
			val = MIP_RD_LO(mi->mip_reg);
			mip_port = MIP_PORT(mi->mip_info);
			mip_addr = val;
			mip = (struct mip_reg *)val;
			mip_reg = __va(mip);
			pr_debug("es7000_mipcfg: host_reg = 0x%lx \n",
				 (unsigned long)host_reg);
			pr_debug("es7000_mipcfg: mip_reg = 0x%lx \n",
				 (unsigned long)mip_reg);
			success++;
			break;
		case MIP_PSAI_REG:
			psaip = (struct psai *)tp;
			if (tp != NULL) {
				if (psaip->addr)
					psai = __va(psaip->addr);
				else
					psai = NULL;
				success++;
			}
			break;
		default:
			break;
		}
		tp += size;
	}

	if (success < 2) {
		es7000_plat = NON_UNISYS;
	} else
		setup_unisys();
	return es7000_plat;
}

#ifdef CONFIG_ACPI
int __init
find_unisys_acpi_oem_table(unsigned long *oem_addr)
{
	struct acpi_table_header *header = NULL;
	int i = 0;
	while (ACPI_SUCCESS(acpi_get_table("OEM1", i++, &header))) {
		if (!memcmp((char *) &header->oem_id, "UNISYS", 6)) {
			struct oem_table *t = (struct oem_table *)header;
			*oem_addr = (unsigned long)__acpi_map_table(t->OEMTableAddr,
								    t->OEMTableSize);
			return 0;
		}
	}
	return -1;
}
#endif

static void
es7000_spin(int n)
{
	int i = 0;

	while (i++ < n)
		rep_nop();
}

static int __init
es7000_mip_write(struct mip_reg *mip_reg)
{
	int			status = 0;
	int			spin;

	spin = MIP_SPIN;
	while (((unsigned long long)host_reg->off_38 &
		(unsigned long long)MIP_VALID) != 0) {
			if (--spin <= 0) {
				printk("es7000_mip_write: Timeout waiting for Host Valid Flag");
				return -1;
			}
		es7000_spin(MIP_SPIN);
	}

	memcpy(host_reg, mip_reg, sizeof(struct mip_reg));
	outb(1, mip_port);

	spin = MIP_SPIN;

	while (((unsigned long long)mip_reg->off_38 &
		(unsigned long long)MIP_VALID) == 0) {
		if (--spin <= 0) {
			printk("es7000_mip_write: Timeout waiting for MIP Valid Flag");
			return -1;
		}
		es7000_spin(MIP_SPIN);
	}

	status = ((unsigned long long)mip_reg->off_0 &
		(unsigned long long)0xffff0000000000ULL) >> 48;
	mip_reg->off_38 = ((unsigned long long)mip_reg->off_38 &
		(unsigned long long)~MIP_VALID);
	return status;
}

int
es7000_start_cpu(int cpu, unsigned long eip)
{
	unsigned long vect = 0, psaival = 0;

	if (psai == NULL)
		return -1;

	vect = ((unsigned long)__pa(eip)/0x1000) << 16;
	psaival = (0x1000000 | vect | cpu);

	while (*psai & 0x1000000)
                ;

	*psai = psaival;

	return 0;

}

void __init
es7000_sw_apic(void)
{
	if (es7000_plat) {
		int mip_status;
		struct mip_reg es7000_mip_reg;

		printk("ES7000: Enabling APIC mode.\n");
        	memset(&es7000_mip_reg, 0, sizeof(struct mip_reg));
        	es7000_mip_reg.off_0 = MIP_SW_APIC;
        	es7000_mip_reg.off_38 = (MIP_VALID);
        	while ((mip_status = es7000_mip_write(&es7000_mip_reg)) != 0)
              		printk("es7000_sw_apic: command failed, status = %x\n",
				mip_status);
		return;
	}
}
