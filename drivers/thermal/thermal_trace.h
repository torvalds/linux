/* SPDX-License-Identifier: GPL-2.0 */
#undef TRACE_SYSTEM
#define TRACE_SYSTEM thermal

#if !defined(_TRACE_THERMAL_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_THERMAL_H

#include <linux/devfreq.h>
#include <linux/thermal.h>
#include <linux/tracepoint.h>

TRACE_DEFINE_ENUM(THERMAL_TRIP_CRITICAL);
TRACE_DEFINE_ENUM(THERMAL_TRIP_HOT);
TRACE_DEFINE_ENUM(THERMAL_TRIP_PASSIVE);
TRACE_DEFINE_ENUM(THERMAL_TRIP_ACTIVE);

#define show_tzt_type(type)					\
	__print_symbolic(type,					\
			 { THERMAL_TRIP_CRITICAL, "CRITICAL"},	\
			 { THERMAL_TRIP_HOT,      "HOT"},	\
			 { THERMAL_TRIP_PASSIVE,  "PASSIVE"},	\
			 { THERMAL_TRIP_ACTIVE,   "ACTIVE"})

TRACE_EVENT(thermal_temperature,

	TP_PROTO(struct thermal_zone_device *tz),

	TP_ARGS(tz),

	TP_STRUCT__entry(
		__string(thermal_zone, tz->type)
		__field(int, id)
		__field(int, temp_prev)
		__field(int, temp)
	),

	TP_fast_assign(
		__assign_str(thermal_zone, tz->type);
		__entry->id = tz->id;
		__entry->temp_prev = tz->last_temperature;
		__entry->temp = tz->temperature;
	),

	TP_printk("thermal_zone=%s id=%d temp_prev=%d temp=%d",
		__get_str(thermal_zone), __entry->id, __entry->temp_prev,
		__entry->temp)
);

TRACE_EVENT(cdev_update,

	TP_PROTO(struct thermal_cooling_device *cdev, unsigned long target),

	TP_ARGS(cdev, target),

	TP_STRUCT__entry(
		__string(type, cdev->type)
		__field(unsigned long, target)
	),

	TP_fast_assign(
		__assign_str(type, cdev->type);
		__entry->target = target;
	),

	TP_printk("type=%s target=%lu", __get_str(type), __entry->target)
);

TRACE_EVENT(thermal_zone_trip,

	TP_PROTO(struct thermal_zone_device *tz, int trip,
		enum thermal_trip_type trip_type),

	TP_ARGS(tz, trip, trip_type),

	TP_STRUCT__entry(
		__string(thermal_zone, tz->type)
		__field(int, id)
		__field(int, trip)
		__field(enum thermal_trip_type, trip_type)
	),

	TP_fast_assign(
		__assign_str(thermal_zone, tz->type);
		__entry->id = tz->id;
		__entry->trip = trip;
		__entry->trip_type = trip_type;
	),

	TP_printk("thermal_zone=%s id=%d trip=%d trip_type=%s",
		__get_str(thermal_zone), __entry->id, __entry->trip,
		show_tzt_type(__entry->trip_type))
);

#ifdef CONFIG_CPU_THERMAL
TRACE_EVENT(thermal_power_cpu_get_power_simple,
	TP_PROTO(int cpu, u32 power),

	TP_ARGS(cpu, power),

	TP_STRUCT__entry(
		__field(int, cpu)
		__field(u32, power)
	),

	TP_fast_assign(
		__entry->cpu = cpu;
		__entry->power = power;
	),

	TP_printk("cpu=%d power=%u", __entry->cpu, __entry->power)
);

TRACE_EVENT(thermal_power_cpu_limit,
	TP_PROTO(const struct cpumask *cpus, unsigned int freq,
		unsigned long cdev_state, u32 power),

	TP_ARGS(cpus, freq, cdev_state, power),

	TP_STRUCT__entry(
		__bitmask(cpumask, num_possible_cpus())
		__field(unsigned int,  freq      )
		__field(unsigned long, cdev_state)
		__field(u32,           power     )
	),

	TP_fast_assign(
		__assign_bitmask(cpumask, cpumask_bits(cpus),
				num_possible_cpus());
		__entry->freq = freq;
		__entry->cdev_state = cdev_state;
		__entry->power = power;
	),

	TP_printk("cpus=%s freq=%u cdev_state=%lu power=%u",
		__get_bitmask(cpumask), __entry->freq, __entry->cdev_state,
		__entry->power)
);
#endif /* CONFIG_CPU_THERMAL */

#ifdef CONFIG_DEVFREQ_THERMAL
TRACE_EVENT(thermal_power_devfreq_get_power,
	TP_PROTO(struct thermal_cooling_device *cdev,
		 struct devfreq_dev_status *status, unsigned long freq,
		u32 power),

	TP_ARGS(cdev, status,  freq, power),

	TP_STRUCT__entry(
		__string(type,         cdev->type    )
		__field(unsigned long, freq          )
		__field(u32,           busy_time)
		__field(u32,           total_time)
		__field(u32,           power)
	),

	TP_fast_assign(
		__assign_str(type, cdev->type);
		__entry->freq = freq;
		__entry->busy_time = status->busy_time;
		__entry->total_time = status->total_time;
		__entry->power = power;
	),

	TP_printk("type=%s freq=%lu load=%u power=%u",
		__get_str(type), __entry->freq,
		__entry->total_time == 0 ? 0 :
			(100 * __entry->busy_time) / __entry->total_time,
		__entry->power)
);

TRACE_EVENT(thermal_power_devfreq_limit,
	TP_PROTO(struct thermal_cooling_device *cdev, unsigned long freq,
		unsigned long cdev_state, u32 power),

	TP_ARGS(cdev, freq, cdev_state, power),

	TP_STRUCT__entry(
		__string(type,         cdev->type)
		__field(unsigned int,  freq      )
		__field(unsigned long, cdev_state)
		__field(u32,           power     )
	),

	TP_fast_assign(
		__assign_str(type, cdev->type);
		__entry->freq = freq;
		__entry->cdev_state = cdev_state;
		__entry->power = power;
	),

	TP_printk("type=%s freq=%u cdev_state=%lu power=%u",
		__get_str(type), __entry->freq, __entry->cdev_state,
		__entry->power)
);
#endif /* CONFIG_DEVFREQ_THERMAL */
#endif /* _TRACE_THERMAL_H */

#undef TRACE_INCLUDE_PATH
#define TRACE_INCLUDE_PATH .

#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_FILE thermal_trace

/* This part must be outside protection */
#include <trace/define_trace.h>
