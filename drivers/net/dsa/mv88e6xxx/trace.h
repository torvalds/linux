/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Copyright 2022 NXP
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM	mv88e6xxx

#if !defined(_MV88E6XXX_TRACE_H) || defined(TRACE_HEADER_MULTI_READ)
#define _MV88E6XXX_TRACE_H

#include <linux/device.h>
#include <linux/if_ether.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(mv88e6xxx_atu_violation,

	TP_PROTO(const struct device *dev, int spid, u16 portvec,
		 const unsigned char *addr, u16 fid),

	TP_ARGS(dev, spid, portvec, addr, fid),

	TP_STRUCT__entry(
		__string(name, dev_name(dev))
		__field(int, spid)
		__field(u16, portvec)
		__array(unsigned char, addr, ETH_ALEN)
		__field(u16, fid)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->spid = spid;
		__entry->portvec = portvec;
		memcpy(__entry->addr, addr, ETH_ALEN);
		__entry->fid = fid;
	),

	TP_printk("dev %s spid %d portvec 0x%x addr %pM fid %u",
		  __get_str(name), __entry->spid, __entry->portvec,
		  __entry->addr, __entry->fid)
);

DEFINE_EVENT(mv88e6xxx_atu_violation, mv88e6xxx_atu_member_violation,
	     TP_PROTO(const struct device *dev, int spid, u16 portvec,
		      const unsigned char *addr, u16 fid),
	     TP_ARGS(dev, spid, portvec, addr, fid));

DEFINE_EVENT(mv88e6xxx_atu_violation, mv88e6xxx_atu_miss_violation,
	     TP_PROTO(const struct device *dev, int spid, u16 portvec,
		      const unsigned char *addr, u16 fid),
	     TP_ARGS(dev, spid, portvec, addr, fid));

DEFINE_EVENT(mv88e6xxx_atu_violation, mv88e6xxx_atu_full_violation,
	     TP_PROTO(const struct device *dev, int spid, u16 portvec,
		      const unsigned char *addr, u16 fid),
	     TP_ARGS(dev, spid, portvec, addr, fid));

DECLARE_EVENT_CLASS(mv88e6xxx_vtu_violation,

	TP_PROTO(const struct device *dev, int spid, u16 vid),

	TP_ARGS(dev, spid, vid),

	TP_STRUCT__entry(
		__string(name, dev_name(dev))
		__field(int, spid)
		__field(u16, vid)
	),

	TP_fast_assign(
		__assign_str(name);
		__entry->spid = spid;
		__entry->vid = vid;
	),

	TP_printk("dev %s spid %d vid %u",
		  __get_str(name), __entry->spid, __entry->vid)
);

DEFINE_EVENT(mv88e6xxx_vtu_violation, mv88e6xxx_vtu_member_violation,
	     TP_PROTO(const struct device *dev, int spid, u16 vid),
	     TP_ARGS(dev, spid, vid));

DEFINE_EVENT(mv88e6xxx_vtu_violation, mv88e6xxx_vtu_miss_violation,
	     TP_PROTO(const struct device *dev, int spid, u16 vid),
	     TP_ARGS(dev, spid, vid));

#endif /* _MV88E6XXX_TRACE_H */

/* We don't want to use include/trace/events */
#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE	trace
/* This part must be outside protection */
#include <trace/define_trace.h>
