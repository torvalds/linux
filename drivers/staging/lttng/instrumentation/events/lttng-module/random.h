#undef TRACE_SYSTEM
#define TRACE_SYSTEM random

#if !defined(_TRACE_RANDOM_H) || defined(TRACE_HEADER_MULTI_READ)
#define _TRACE_RANDOM_H

#include <linux/writeback.h>
#include <linux/tracepoint.h>

DECLARE_EVENT_CLASS(random__mix_pool_bytes,
	TP_PROTO(const char *pool_name, int bytes, unsigned long IP),

	TP_ARGS(pool_name, bytes, IP),

	TP_STRUCT__entry(
		__string(   pool_name,	pool_name		)
		__field(	  int,	bytes			)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		tp_strcpy(pool_name, pool_name)
		tp_assign(bytes, bytes)
		tp_assign(IP, IP)
	),

	TP_printk("%s pool: bytes %d caller %pF",
		  __get_str(pool_name), __entry->bytes, (void *)__entry->IP)
)

DEFINE_EVENT_MAP(random__mix_pool_bytes, mix_pool_bytes,

	random_mix_pool_bytes,

	TP_PROTO(const char *pool_name, int bytes, unsigned long IP),

	TP_ARGS(pool_name, bytes, IP)
)

DEFINE_EVENT_MAP(random__mix_pool_bytes, mix_pool_bytes_nolock,

	random_mix_pool_bytes_nolock,

	TP_PROTO(const char *pool_name, int bytes, unsigned long IP),

	TP_ARGS(pool_name, bytes, IP)
)

TRACE_EVENT_MAP(credit_entropy_bits,

	random_credit_entropy_bits,

	TP_PROTO(const char *pool_name, int bits, int entropy_count,
		 int entropy_total, unsigned long IP),

	TP_ARGS(pool_name, bits, entropy_count, entropy_total, IP),

	TP_STRUCT__entry(
		__string(   pool_name,	pool_name		)
		__field(	  int,	bits			)
		__field(	  int,	entropy_count		)
		__field(	  int,	entropy_total		)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		tp_strcpy(pool_name, pool_name)
		tp_assign(bits, bits)
		tp_assign(entropy_count, entropy_count)
		tp_assign(entropy_total, entropy_total)
		tp_assign(IP, IP)
	),

	TP_printk("%s pool: bits %d entropy_count %d entropy_total %d "
		  "caller %pF", __get_str(pool_name), __entry->bits,
		  __entry->entropy_count, __entry->entropy_total,
		  (void *)__entry->IP)
)

TRACE_EVENT_MAP(get_random_bytes,

	random_get_random_bytes,

	TP_PROTO(int nbytes, unsigned long IP),

	TP_ARGS(nbytes, IP),

	TP_STRUCT__entry(
		__field(	  int,	nbytes			)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		tp_assign(nbytes, nbytes)
		tp_assign(IP, IP)
	),

	TP_printk("nbytes %d caller %pF", __entry->nbytes, (void *)__entry->IP)
)

DECLARE_EVENT_CLASS(random__extract_entropy,
	TP_PROTO(const char *pool_name, int nbytes, int entropy_count,
		 unsigned long IP),

	TP_ARGS(pool_name, nbytes, entropy_count, IP),

	TP_STRUCT__entry(
		__string(   pool_name,	pool_name		)
		__field(	  int,	nbytes			)
		__field(	  int,	entropy_count		)
		__field(unsigned long,	IP			)
	),

	TP_fast_assign(
		tp_strcpy(pool_name, pool_name)
		tp_assign(nbytes, nbytes)
		tp_assign(entropy_count, entropy_count)
		tp_assign(IP, IP)
	),

	TP_printk("%s pool: nbytes %d entropy_count %d caller %pF",
		  __get_str(pool_name), __entry->nbytes, __entry->entropy_count,
		  (void *)__entry->IP)
)


DEFINE_EVENT_MAP(random__extract_entropy, extract_entropy,

	random_extract_entropy,

	TP_PROTO(const char *pool_name, int nbytes, int entropy_count,
		 unsigned long IP),

	TP_ARGS(pool_name, nbytes, entropy_count, IP)
)

DEFINE_EVENT_MAP(random__extract_entropy, extract_entropy_user,

	random_extract_entropy_user,

	TP_PROTO(const char *pool_name, int nbytes, int entropy_count,
		 unsigned long IP),

	TP_ARGS(pool_name, nbytes, entropy_count, IP)
)



#endif /* _TRACE_RANDOM_H */

/* This part must be outside protection */
#include "../../../probes/define_trace.h"
