#include <linux/compiler.h>
#include <linux/module.h>
#include <linux/types.h>

#include <asm/processor.h>
#include <asm/cmpxchg.h>
#include <linux/atomic.h>

long long atomic64_read_cx8(long long, const atomic64_t *v);
EXPORT_SYMBOL(atomic64_read_cx8);
long long atomic64_set_cx8(long long, const atomic64_t *v);
EXPORT_SYMBOL(atomic64_set_cx8);
long long atomic64_xchg_cx8(long long, unsigned high);
EXPORT_SYMBOL(atomic64_xchg_cx8);
long long atomic64_add_return_cx8(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_add_return_cx8);
long long atomic64_sub_return_cx8(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_sub_return_cx8);
long long atomic64_inc_return_cx8(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_inc_return_cx8);
long long atomic64_dec_return_cx8(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_dec_return_cx8);
long long atomic64_dec_if_positive_cx8(atomic64_t *v);
EXPORT_SYMBOL(atomic64_dec_if_positive_cx8);
int atomic64_inc_not_zero_cx8(atomic64_t *v);
EXPORT_SYMBOL(atomic64_inc_not_zero_cx8);
int atomic64_add_unless_cx8(atomic64_t *v, long long a, long long u);
EXPORT_SYMBOL(atomic64_add_unless_cx8);

#ifndef CONFIG_X86_CMPXCHG64
long long atomic64_read_386(long long, const atomic64_t *v);
EXPORT_SYMBOL(atomic64_read_386);
long long atomic64_set_386(long long, const atomic64_t *v);
EXPORT_SYMBOL(atomic64_set_386);
long long atomic64_xchg_386(long long, unsigned high);
EXPORT_SYMBOL(atomic64_xchg_386);
long long atomic64_add_return_386(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_add_return_386);
long long atomic64_sub_return_386(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_sub_return_386);
long long atomic64_inc_return_386(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_inc_return_386);
long long atomic64_dec_return_386(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_dec_return_386);
long long atomic64_add_386(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_add_386);
long long atomic64_sub_386(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_sub_386);
long long atomic64_inc_386(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_inc_386);
long long atomic64_dec_386(long long a, atomic64_t *v);
EXPORT_SYMBOL(atomic64_dec_386);
long long atomic64_dec_if_positive_386(atomic64_t *v);
EXPORT_SYMBOL(atomic64_dec_if_positive_386);
int atomic64_inc_not_zero_386(atomic64_t *v);
EXPORT_SYMBOL(atomic64_inc_not_zero_386);
int atomic64_add_unless_386(atomic64_t *v, long long a, long long u);
EXPORT_SYMBOL(atomic64_add_unless_386);
#endif
