/* SPDX-License-Identifier: GPL-2.0 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM tegra_vde

#if !defined(TEGRA_VDE_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define TEGRA_VDE_TRACE_H

#include <linux/tracepoint.h>

#include "vde.h"

DECLARE_EVENT_CLASS(register_access,
	TP_PROTO(struct tegra_vde *vde, void __iomem *base,
		 u32 offset, u32 value),
	TP_ARGS(vde, base, offset, value),
	TP_STRUCT__entry(
		__string(hw_name, tegra_vde_reg_base_name(vde, base))
		__field(u32, offset)
		__field(u32, value)
	),
	TP_fast_assign(
		__assign_str(hw_name);
		__entry->offset = offset;
		__entry->value = value;
	),
	TP_printk("%s:0x%03x 0x%08x", __get_str(hw_name), __entry->offset,
		  __entry->value)
);

DEFINE_EVENT(register_access, vde_writel,
	TP_PROTO(struct tegra_vde *vde, void __iomem *base,
		 u32 offset, u32 value),
	TP_ARGS(vde, base, offset, value));
DEFINE_EVENT(register_access, vde_readl,
	TP_PROTO(struct tegra_vde *vde, void __iomem *base,
		 u32 offset, u32 value),
	TP_ARGS(vde, base, offset, value));

TRACE_EVENT(vde_setup_iram_entry,
	TP_PROTO(unsigned int table, unsigned int row, u32 value, u32 aux_addr),
	TP_ARGS(table, row, value, aux_addr),
	TP_STRUCT__entry(
		__field(unsigned int, table)
		__field(unsigned int, row)
		__field(u32, value)
		__field(u32, aux_addr)
	),
	TP_fast_assign(
		__entry->table = table;
		__entry->row = row;
		__entry->value = value;
		__entry->aux_addr = aux_addr;
	),
	TP_printk("[%u][%u] = { 0x%08x (flags = \"%s\", frame_num = %u); 0x%08x }",
		  __entry->table, __entry->row, __entry->value,
		  __print_flags(__entry->value, " ", { (1 << 25), "B" }),
		  __entry->value & 0x7FFFFF, __entry->aux_addr)
);

TRACE_EVENT(vde_ref_l0,
	TP_PROTO(unsigned int frame_num),
	TP_ARGS(frame_num),
	TP_STRUCT__entry(
		__field(unsigned int, frame_num)
	),
	TP_fast_assign(
		__entry->frame_num = frame_num;
	),
	TP_printk("REF L0: DPB: Frame 0: frame_num = %u", __entry->frame_num)
);

TRACE_EVENT(vde_ref_l1,
	TP_PROTO(unsigned int with_later_poc_nb,
		 unsigned int with_earlier_poc_nb),
	TP_ARGS(with_later_poc_nb, with_earlier_poc_nb),
	TP_STRUCT__entry(
		__field(unsigned int, with_later_poc_nb)
		__field(unsigned int, with_earlier_poc_nb)
	),
	TP_fast_assign(
		__entry->with_later_poc_nb = with_later_poc_nb;
		__entry->with_earlier_poc_nb = with_earlier_poc_nb;
	),
	TP_printk("REF L1: with_later_poc_nb %u, with_earlier_poc_nb %u",
		  __entry->with_later_poc_nb, __entry->with_earlier_poc_nb)
);

#endif /* TEGRA_VDE_TRACE_H */

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH ../../drivers/media/platform/nvidia/tegra-vde
#define TRACE_INCLUDE_FILE trace
#include <trace/define_trace.h>
