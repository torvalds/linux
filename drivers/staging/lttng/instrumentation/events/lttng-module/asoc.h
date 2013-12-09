#undef TRACE_SYSTEM
#define TRACE_SYSTEM asoc

#if !defined(_TRACE_ASOC_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_ASOC_H

#include <linux/ktime.h>
#include <linux/tracepoint.h>
#include <linux/version.h>

#define DAPM_DIRECT "(direct)"

#ifndef _TRACE_ASOC_DEF
#define _TRACE_ASOC_DEF
struct snd_soc_jack;
struct snd_soc_codec;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
struct snd_soc_platform;
#endif
struct snd_soc_card;
struct snd_soc_dapm_widget;
#endif

/*
 * Log register events
 */
DECLARE_EVENT_CLASS(snd_soc_reg,

	TP_PROTO(struct snd_soc_codec *codec, unsigned int reg,
		 unsigned int val),

	TP_ARGS(codec, reg, val),

	TP_STRUCT__entry(
		__string(	name,		codec->name	)
		__field(	int,		id		)
		__field(	unsigned int,	reg		)
		__field(	unsigned int,	val		)
	),

	TP_fast_assign(
		tp_strcpy(name, codec->name)
		tp_assign(id, codec->id)
		tp_assign(reg, reg)
		tp_assign(val, val)
	),

	TP_printk("codec=%s.%d reg=%x val=%x", __get_str(name),
		  (int)__entry->id, (unsigned int)__entry->reg,
		  (unsigned int)__entry->val)
)

DEFINE_EVENT(snd_soc_reg, snd_soc_reg_write,

	TP_PROTO(struct snd_soc_codec *codec, unsigned int reg,
		 unsigned int val),

	TP_ARGS(codec, reg, val)

)

DEFINE_EVENT(snd_soc_reg, snd_soc_reg_read,

	TP_PROTO(struct snd_soc_codec *codec, unsigned int reg,
		 unsigned int val),

	TP_ARGS(codec, reg, val)

)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0))
DECLARE_EVENT_CLASS(snd_soc_preg,

	TP_PROTO(struct snd_soc_platform *platform, unsigned int reg,
		 unsigned int val),

	TP_ARGS(platform, reg, val),

	TP_STRUCT__entry(
		__string(	name,		platform->name	)
		__field(	int,		id		)
		__field(	unsigned int,	reg		)
		__field(	unsigned int,	val		)
	),

	TP_fast_assign(
		tp_strcpy(name, platform->name)
		tp_assign(id, platform->id)
		tp_assign(reg, reg)
		tp_assign(val, val)
	),

	TP_printk("platform=%s.%d reg=%x val=%x", __get_str(name),
		  (int)__entry->id, (unsigned int)__entry->reg,
		  (unsigned int)__entry->val)
)

DEFINE_EVENT(snd_soc_preg, snd_soc_preg_write,

	TP_PROTO(struct snd_soc_platform *platform, unsigned int reg,
		 unsigned int val),

	TP_ARGS(platform, reg, val)

)

DEFINE_EVENT(snd_soc_preg, snd_soc_preg_read,

	TP_PROTO(struct snd_soc_platform *platform, unsigned int reg,
		 unsigned int val),

	TP_ARGS(platform, reg, val)

)
#endif

DECLARE_EVENT_CLASS(snd_soc_card,

	TP_PROTO(struct snd_soc_card *card, int val),

	TP_ARGS(card, val),

	TP_STRUCT__entry(
		__string(	name,		card->name	)
		__field(	int,		val		)
	),

	TP_fast_assign(
		tp_strcpy(name, card->name)
		tp_assign(val, val)
	),

	TP_printk("card=%s val=%d", __get_str(name), (int)__entry->val)
)

DEFINE_EVENT(snd_soc_card, snd_soc_bias_level_start,

	TP_PROTO(struct snd_soc_card *card, int val),

	TP_ARGS(card, val)

)

DEFINE_EVENT(snd_soc_card, snd_soc_bias_level_done,

	TP_PROTO(struct snd_soc_card *card, int val),

	TP_ARGS(card, val)

)

DECLARE_EVENT_CLASS(snd_soc_dapm_basic,

	TP_PROTO(struct snd_soc_card *card),

	TP_ARGS(card),

	TP_STRUCT__entry(
		__string(	name,	card->name	)
	),

	TP_fast_assign(
		tp_strcpy(name, card->name)
	),

	TP_printk("card=%s", __get_str(name))
)

DEFINE_EVENT(snd_soc_dapm_basic, snd_soc_dapm_start,

	TP_PROTO(struct snd_soc_card *card),

	TP_ARGS(card)

)

DEFINE_EVENT(snd_soc_dapm_basic, snd_soc_dapm_done,

	TP_PROTO(struct snd_soc_card *card),

	TP_ARGS(card)

)

DECLARE_EVENT_CLASS(snd_soc_dapm_widget,

	TP_PROTO(struct snd_soc_dapm_widget *w, int val),

	TP_ARGS(w, val),

	TP_STRUCT__entry(
		__string(	name,	w->name		)
		__field(	int,	val		)
	),

	TP_fast_assign(
		tp_strcpy(name, w->name)
		tp_assign(val, val)
	),

	TP_printk("widget=%s val=%d", __get_str(name),
		  (int)__entry->val)
)

DEFINE_EVENT(snd_soc_dapm_widget, snd_soc_dapm_widget_power,

	TP_PROTO(struct snd_soc_dapm_widget *w, int val),

	TP_ARGS(w, val)

)

DEFINE_EVENT(snd_soc_dapm_widget, snd_soc_dapm_widget_event_start,

	TP_PROTO(struct snd_soc_dapm_widget *w, int val),

	TP_ARGS(w, val)

)

DEFINE_EVENT(snd_soc_dapm_widget, snd_soc_dapm_widget_event_done,

	TP_PROTO(struct snd_soc_dapm_widget *w, int val),

	TP_ARGS(w, val)

)

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,2,0))
TRACE_EVENT(snd_soc_dapm_walk_done,

	TP_PROTO(struct snd_soc_card *card),

	TP_ARGS(card),

	TP_STRUCT__entry(
		__string(	name,	card->name		)
		__field(	int,	power_checks		)
		__field(	int,	path_checks		)
		__field(	int,	neighbour_checks	)
	),

	TP_fast_assign(
		tp_strcpy(name, card->name)
		tp_assign(power_checks, card->dapm_stats.power_checks)
		tp_assign(path_checks, card->dapm_stats.path_checks)
		tp_assign(neighbour_checks, card->dapm_stats.neighbour_checks)
	),

	TP_printk("%s: checks %d power, %d path, %d neighbour",
		  __get_str(name), (int)__entry->power_checks,
		  (int)__entry->path_checks, (int)__entry->neighbour_checks)
)
#endif

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3,5,0))
TRACE_EVENT(snd_soc_dapm_output_path,

	TP_PROTO(struct snd_soc_dapm_widget *widget,
		struct snd_soc_dapm_path *path),

	TP_ARGS(widget, path),

	TP_STRUCT__entry(
		__string(	wname,	widget->name		)
		__string(	pname,	path->name ? path->name : DAPM_DIRECT)
		__string(	psname,	path->sink->name	)
		__field(	int,	path_sink		)
		__field(	int,	path_connect		)
	),

	TP_fast_assign(
		tp_strcpy(wname, widget->name)
		tp_strcpy(pname, path->name ? path->name : DAPM_DIRECT)
		tp_strcpy(psname, path->sink->name)
		tp_assign(path_connect, path->connect)
		tp_assign(path_sink, (long)path->sink)
	),

	TP_printk("%c%s -> %s -> %s\n",
		(int) __entry->path_sink &&
		(int) __entry->path_connect ? '*' : ' ',
		__get_str(wname), __get_str(pname), __get_str(psname))
)

TRACE_EVENT(snd_soc_dapm_input_path,

	TP_PROTO(struct snd_soc_dapm_widget *widget,
		struct snd_soc_dapm_path *path),

	TP_ARGS(widget, path),

	TP_STRUCT__entry(
		__string(	wname,	widget->name		)
		__string(	pname,	path->name ? path->name : DAPM_DIRECT)
		__string(	psname,	path->source->name	)
		__field(	int,	path_source		)
		__field(	int,	path_connect		)
	),

	TP_fast_assign(
		tp_strcpy(wname, widget->name)
		tp_strcpy(pname, path->name ? path->name : DAPM_DIRECT)
		tp_strcpy(psname, path->source->name)
		tp_assign(path_connect, path->connect)
		tp_assign(path_source, (long)path->source)
	),

	TP_printk("%c%s <- %s <- %s\n",
		(int) __entry->path_source &&
		(int) __entry->path_connect ? '*' : ' ',
		__get_str(wname), __get_str(pname), __get_str(psname))
)

TRACE_EVENT(snd_soc_dapm_connected,

	TP_PROTO(int paths, int stream),

	TP_ARGS(paths, stream),

	TP_STRUCT__entry(
		__field(	int,	paths		)
		__field(	int,	stream		)
	),

	TP_fast_assign(
		tp_assign(paths, paths)
		tp_assign(stream, stream)
	),

	TP_printk("%s: found %d paths\n",
		__entry->stream ? "capture" : "playback", __entry->paths)
)
#endif

TRACE_EVENT(snd_soc_jack_irq,

	TP_PROTO(const char *name),

	TP_ARGS(name),

	TP_STRUCT__entry(
		__string(	name,	name		)
	),

	TP_fast_assign(
		tp_strcpy(name, name)
	),

	TP_printk("%s", __get_str(name))
)

TRACE_EVENT(snd_soc_jack_report,

	TP_PROTO(struct snd_soc_jack *jack, int mask, int val),

	TP_ARGS(jack, mask, val),

	TP_STRUCT__entry(
		__string(	name,		jack->jack->name	)
		__field(	int,		mask			)
		__field(	int,		val			)
	),

	TP_fast_assign(
		tp_strcpy(name, jack->jack->name)
		tp_assign(mask, mask)
		tp_assign(val, val)
	),

	TP_printk("jack=%s %x/%x", __get_str(name), (int)__entry->val,
		  (int)__entry->mask)
)

TRACE_EVENT(snd_soc_jack_notify,

	TP_PROTO(struct snd_soc_jack *jack, int val),

	TP_ARGS(jack, val),

	TP_STRUCT__entry(
		__string(	name,		jack->jack->name	)
		__field(	int,		val			)
	),

	TP_fast_assign(
		tp_strcpy(name, jack->jack->name)
		tp_assign(val, val)
	),

	TP_printk("jack=%s %x", __get_str(name), (int)__entry->val)
)

TRACE_EVENT(snd_soc_cache_sync,

	TP_PROTO(struct snd_soc_codec *codec, const char *type,
		 const char *status),

	TP_ARGS(codec, type, status),

	TP_STRUCT__entry(
		__string(	name,		codec->name	)
		__string(	status,		status		)
		__string(	type,		type		)
		__field(	int,		id		)
	),

	TP_fast_assign(
		tp_strcpy(name, codec->name)
		tp_strcpy(status, status)
		tp_strcpy(type, type)
		tp_assign(id, codec->id)
	),

	TP_printk("codec=%s.%d type=%s status=%s", __get_str(name),
		  (int)__entry->id, __get_str(type), __get_str(status))
)

#endif /* _TRACE_ASOC_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
