/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * SGI UV Core Functions
 *
 * Copyright (C) 2008 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/module.h>
#include <linux/percpu.h>
#include <asm/uv/uv.h>
#include <asm/uv/uv_mmrs.h>
#include <asm/uv/uv_hub.h>

bool ia64_is_uv;
EXPORT_SYMBOL_GPL(ia64_is_uv);

DEFINE_PER_CPU(struct uv_hub_info_s, __uv_hub_info);
EXPORT_PER_CPU_SYMBOL_GPL(__uv_hub_info);

struct redir_addr {
	unsigned long redirect;
	unsigned long alias;
};

#define DEST_SHIFT UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR_DEST_BASE_SHFT

static __initdata struct redir_addr redir_addrs[] = {
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_0_MMR, UVH_SI_ALIAS0_OVERLAY_CONFIG},
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_1_MMR, UVH_SI_ALIAS1_OVERLAY_CONFIG},
	{UVH_RH_GAM_ALIAS210_REDIRECT_CONFIG_2_MMR, UVH_SI_ALIAS2_OVERLAY_CONFIG},
};

static __init void get_lowmem_redirect(unsigned long *base, unsigned long *size)
{
	union uvh_si_alias0_overlay_config_u alias;
	union uvh_rh_gam_alias210_redirect_config_2_mmr_u redirect;
	int i;

	for (i = 0; i < ARRAY_SIZE(redir_addrs); i++) {
		alias.v = uv_read_local_mmr(redir_addrs[i].alias);
		if (alias.s.base == 0) {
			*size = (1UL << alias.s.m_alias);
			redirect.v = uv_read_local_mmr(redir_addrs[i].redirect);
			*base = (unsigned long)redirect.s.dest_base << DEST_SHIFT;
			return;
		}
	}
	BUG();
}

void __init uv_probe_system_type(void)
{
	struct acpi_table_rsdp *rsdp;
	struct acpi_table_xsdt *xsdt;

	if (efi.acpi20 == EFI_INVALID_TABLE_ADDR) {
		pr_err("ACPI 2.0 RSDP not found.\n");
		return;
	}

	rsdp = (struct acpi_table_rsdp *)__va(efi.acpi20);
	if (strncmp(rsdp->signature, ACPI_SIG_RSDP, sizeof(ACPI_SIG_RSDP) - 1)) {
		pr_err("ACPI 2.0 RSDP signature incorrect.\n");
		return;
	}

	xsdt = (struct acpi_table_xsdt *)__va(rsdp->xsdt_physical_address);
	if (strncmp(xsdt->header.signature, ACPI_SIG_XSDT,
			sizeof(ACPI_SIG_XSDT) - 1)) {
		pr_err("ACPI 2.0 XSDT signature incorrect.\n");
		return;
	}

	if (!strcmp(xsdt->header.oem_id, "SGI") &&
	    !strcmp(xsdt->header.oem_table_id + 4, "UV"))
		ia64_is_uv = true;
}

void __init uv_setup(char **cmdline_p)
{
	union uvh_si_addr_map_config_u m_n_config;
	union uvh_node_id_u node_id;
	unsigned long gnode_upper;
	int nid, cpu, m_val, n_val;
	unsigned long mmr_base, lowmem_redir_base, lowmem_redir_size;

	get_lowmem_redirect(&lowmem_redir_base, &lowmem_redir_size);
	node_id.v = uv_read_local_mmr(UVH_NODE_ID);
	m_n_config.v = uv_read_local_mmr(UVH_SI_ADDR_MAP_CONFIG);
	mmr_base = uv_read_local_mmr(UVH_RH_GAM_MMR_OVERLAY_CONFIG_MMR) &
			~UV_MMR_ENABLE;

	m_val = m_n_config.s.m_skt;
	n_val = m_n_config.s.n_skt;
	printk(KERN_DEBUG "UV: global MMR base 0x%lx\n", mmr_base);

	gnode_upper = (((unsigned long)node_id.s.node_id) &
		       ~((1 << n_val) - 1)) << m_val;

	for_each_present_cpu(cpu) {
		nid = cpu_to_node(cpu);
		uv_cpu_hub_info(cpu)->lowmem_remap_base = lowmem_redir_base;
		uv_cpu_hub_info(cpu)->lowmem_remap_top =
			lowmem_redir_base + lowmem_redir_size;
		uv_cpu_hub_info(cpu)->m_val = m_val;
		uv_cpu_hub_info(cpu)->n_val = n_val;
		uv_cpu_hub_info(cpu)->pnode_mask = (1 << n_val) -1;
		uv_cpu_hub_info(cpu)->gpa_mask = (1 << (m_val + n_val)) - 1;
		uv_cpu_hub_info(cpu)->gnode_upper = gnode_upper;
		uv_cpu_hub_info(cpu)->global_mmr_base = mmr_base;
		uv_cpu_hub_info(cpu)->coherency_domain_number = 0;/* ZZZ */
		printk(KERN_DEBUG "UV cpu %d, nid %d\n", cpu, nid);
	}
}

