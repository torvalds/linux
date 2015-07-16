/*
 * Copyright (C) 2007-2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007-2009 PetaLogix
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>
#include <asm/cpuinfo.h>
#include <asm/pvr.h>

static const char family_string[] = CONFIG_XILINX_MICROBLAZE0_FAMILY;
static const char cpu_ver_string[] = CONFIG_XILINX_MICROBLAZE0_HW_VER;

#define err_printk(x) \
	early_printk("ERROR: Microblaze " x "-different for kernel and DTS\n");

void __init set_cpuinfo_static(struct cpuinfo *ci, struct device_node *cpu)
{
	u32 i = 0;

	ci->use_instr =
		(fcpu(cpu, "xlnx,use-barrel") ? PVR0_USE_BARREL_MASK : 0) |
		(fcpu(cpu, "xlnx,use-msr-instr") ? PVR2_USE_MSR_INSTR : 0) |
		(fcpu(cpu, "xlnx,use-pcmp-instr") ? PVR2_USE_PCMP_INSTR : 0) |
		(fcpu(cpu, "xlnx,use-div") ? PVR0_USE_DIV_MASK : 0);
	if (CONFIG_XILINX_MICROBLAZE0_USE_BARREL)
		i |= PVR0_USE_BARREL_MASK;
	if (CONFIG_XILINX_MICROBLAZE0_USE_MSR_INSTR)
		i |= PVR2_USE_MSR_INSTR;
	if (CONFIG_XILINX_MICROBLAZE0_USE_PCMP_INSTR)
		i |= PVR2_USE_PCMP_INSTR;
	if (CONFIG_XILINX_MICROBLAZE0_USE_DIV)
		i |= PVR0_USE_DIV_MASK;
	if (ci->use_instr != i)
		err_printk("BARREL, MSR, PCMP or DIV");

	ci->use_mult = fcpu(cpu, "xlnx,use-hw-mul");
	if (ci->use_mult != CONFIG_XILINX_MICROBLAZE0_USE_HW_MUL)
		err_printk("HW_MUL");
	ci->use_mult =
		(ci->use_mult > 1 ?
				(PVR2_USE_MUL64_MASK | PVR0_USE_HW_MUL_MASK) :
				(ci->use_mult == 1 ? PVR0_USE_HW_MUL_MASK : 0));

	ci->use_fpu = fcpu(cpu, "xlnx,use-fpu");
	if (ci->use_fpu != CONFIG_XILINX_MICROBLAZE0_USE_FPU)
		err_printk("HW_FPU");
	ci->use_fpu = (ci->use_fpu > 1 ?
				(PVR2_USE_FPU2_MASK | PVR0_USE_FPU_MASK) :
				(ci->use_fpu == 1 ? PVR0_USE_FPU_MASK : 0));

	ci->use_exc =
		(fcpu(cpu, "xlnx,unaligned-exceptions") ?
				PVR2_UNALIGNED_EXC_MASK : 0) |
		(fcpu(cpu, "xlnx,ill-opcode-exception") ?
				PVR2_ILL_OPCODE_EXC_MASK : 0) |
		(fcpu(cpu, "xlnx,iopb-bus-exception") ?
				PVR2_IOPB_BUS_EXC_MASK : 0) |
		(fcpu(cpu, "xlnx,dopb-bus-exception") ?
				PVR2_DOPB_BUS_EXC_MASK : 0) |
		(fcpu(cpu, "xlnx,div-zero-exception") ?
				PVR2_DIV_ZERO_EXC_MASK : 0) |
		(fcpu(cpu, "xlnx,fpu-exception") ? PVR2_FPU_EXC_MASK : 0) |
		(fcpu(cpu, "xlnx,fsl-exception") ? PVR2_USE_EXTEND_FSL : 0);

	ci->use_icache = fcpu(cpu, "xlnx,use-icache");
	ci->icache_tagbits = fcpu(cpu, "xlnx,addr-tag-bits");
	ci->icache_write = fcpu(cpu, "xlnx,allow-icache-wr");
	ci->icache_line_length = fcpu(cpu, "xlnx,icache-line-len") << 2;
	if (!ci->icache_line_length) {
		if (fcpu(cpu, "xlnx,icache-use-fsl"))
			ci->icache_line_length = 4 << 2;
		else
			ci->icache_line_length = 1 << 2;
	}
	ci->icache_size = fcpu(cpu, "i-cache-size");
	ci->icache_base = fcpu(cpu, "i-cache-baseaddr");
	ci->icache_high = fcpu(cpu, "i-cache-highaddr");

	ci->use_dcache = fcpu(cpu, "xlnx,use-dcache");
	ci->dcache_tagbits = fcpu(cpu, "xlnx,dcache-addr-tag");
	ci->dcache_write = fcpu(cpu, "xlnx,allow-dcache-wr");
	ci->dcache_line_length = fcpu(cpu, "xlnx,dcache-line-len") << 2;
	if (!ci->dcache_line_length) {
		if (fcpu(cpu, "xlnx,dcache-use-fsl"))
			ci->dcache_line_length = 4 << 2;
		else
			ci->dcache_line_length = 1 << 2;
	}
	ci->dcache_size = fcpu(cpu, "d-cache-size");
	ci->dcache_base = fcpu(cpu, "d-cache-baseaddr");
	ci->dcache_high = fcpu(cpu, "d-cache-highaddr");
	ci->dcache_wb = fcpu(cpu, "xlnx,dcache-use-writeback");

	ci->use_dopb = fcpu(cpu, "xlnx,d-opb");
	ci->use_iopb = fcpu(cpu, "xlnx,i-opb");
	ci->use_dlmb = fcpu(cpu, "xlnx,d-lmb");
	ci->use_ilmb = fcpu(cpu, "xlnx,i-lmb");

	ci->num_fsl = fcpu(cpu, "xlnx,fsl-links");
	ci->irq_edge = fcpu(cpu, "xlnx,interrupt-is-edge");
	ci->irq_positive = fcpu(cpu, "xlnx,edge-is-positive");
	ci->area_optimised = 0;

	ci->hw_debug = fcpu(cpu, "xlnx,debug-enabled");
	ci->num_pc_brk = fcpu(cpu, "xlnx,number-of-pc-brk");
	ci->num_rd_brk = fcpu(cpu, "xlnx,number-of-rd-addr-brk");
	ci->num_wr_brk = fcpu(cpu, "xlnx,number-of-wr-addr-brk");

	ci->pvr_user1 = fcpu(cpu, "xlnx,pvr-user1");
	ci->pvr_user2 = fcpu(cpu, "xlnx,pvr-user2");

	ci->mmu = fcpu(cpu, "xlnx,use-mmu");
	ci->mmu_privins = fcpu(cpu, "xlnx,mmu-privileged-instr");
	ci->endian = fcpu(cpu, "xlnx,endianness");

	ci->ver_code = 0;
	ci->fpga_family_code = 0;

	/* Do various fixups based on CPU version and FPGA family strings */

	/* Resolved the CPU version code */
	for (i = 0; cpu_ver_lookup[i].s != NULL; i++) {
		if (strcmp(cpu_ver_lookup[i].s, cpu_ver_string) == 0)
			ci->ver_code = cpu_ver_lookup[i].k;
	}

	/* Resolved the fpga family code */
	for (i = 0; family_string_lookup[i].s != NULL; i++) {
		if (strcmp(family_string_lookup[i].s, family_string) == 0)
			ci->fpga_family_code = family_string_lookup[i].k;
	}

	/* FIXME - mb3 and spartan2 do not exist in PVR */
	/* This is mb3 and on a non Spartan2 */
	if (ci->ver_code == 0x20 && ci->fpga_family_code != 0xf0)
		/* Hardware Multiplier in use */
		ci->use_mult = 1;
}
