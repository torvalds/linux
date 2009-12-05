/*
 * x86 instruction attribute tables
 *
 * Written by Masami Hiramatsu <mhiramat@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */
#include <asm/insn.h>

/* Attribute tables are generated from opcode map */
#include "inat-tables.c"

/* Attribute search APIs */
insn_attr_t inat_get_opcode_attribute(insn_byte_t opcode)
{
	return inat_primary_table[opcode];
}

insn_attr_t inat_get_escape_attribute(insn_byte_t opcode, insn_byte_t last_pfx,
				      insn_attr_t esc_attr)
{
	const insn_attr_t *table;
	insn_attr_t lpfx_attr;
	int n, m = 0;

	n = inat_escape_id(esc_attr);
	if (last_pfx) {
		lpfx_attr = inat_get_opcode_attribute(last_pfx);
		m = inat_last_prefix_id(lpfx_attr);
	}
	table = inat_escape_tables[n][0];
	if (!table)
		return 0;
	if (inat_has_variant(table[opcode]) && m) {
		table = inat_escape_tables[n][m];
		if (!table)
			return 0;
	}
	return table[opcode];
}

insn_attr_t inat_get_group_attribute(insn_byte_t modrm, insn_byte_t last_pfx,
				     insn_attr_t grp_attr)
{
	const insn_attr_t *table;
	insn_attr_t lpfx_attr;
	int n, m = 0;

	n = inat_group_id(grp_attr);
	if (last_pfx) {
		lpfx_attr = inat_get_opcode_attribute(last_pfx);
		m = inat_last_prefix_id(lpfx_attr);
	}
	table = inat_group_tables[n][0];
	if (!table)
		return inat_group_common_attribute(grp_attr);
	if (inat_has_variant(table[X86_MODRM_REG(modrm)]) && m) {
		table = inat_group_tables[n][m];
		if (!table)
			return inat_group_common_attribute(grp_attr);
	}
	return table[X86_MODRM_REG(modrm)] |
	       inat_group_common_attribute(grp_attr);
}

insn_attr_t inat_get_avx_attribute(insn_byte_t opcode, insn_byte_t vex_m,
				   insn_byte_t vex_p)
{
	const insn_attr_t *table;
	if (vex_m > X86_VEX_M_MAX || vex_p > INAT_LSTPFX_MAX)
		return 0;
	table = inat_avx_tables[vex_m][vex_p];
	if (!table)
		return 0;
	return table[opcode];
}

