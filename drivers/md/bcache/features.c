// SPDX-License-Identifier: GPL-2.0
/*
 * Feature set bits and string conversion.
 * Inspired by ext4's features compat/incompat/ro_compat related code.
 *
 * Copyright 2020 Coly Li <colyli@suse.de>
 *
 */
#include <linux/bcache.h>
#include "bcache.h"
#include "features.h"

struct feature {
	int		compat;
	unsigned int	mask;
	const char	*string;
};

static struct feature feature_list[] = {
	{BCH_FEATURE_INCOMPAT, BCH_FEATURE_INCOMPAT_LOG_LARGE_BUCKET_SIZE,
		"large_bucket"},
	{0, 0, 0 },
};

#define compose_feature_string(type)				\
({									\
	struct feature *f;						\
	bool first = true;						\
									\
	for (f = &feature_list[0]; f->compat != 0; f++) {		\
		if (f->compat != BCH_FEATURE_ ## type)			\
			continue;					\
		if (BCH_HAS_ ## type ## _FEATURE(&c->cache->sb, f->mask)) {	\
			if (first) {					\
				out += snprintf(out, buf + size - out,	\
						"[");	\
			} else {					\
				out += snprintf(out, buf + size - out,	\
						" [");			\
			}						\
		} else if (!first) {					\
			out += snprintf(out, buf + size - out, " ");	\
		}							\
									\
		out += snprintf(out, buf + size - out, "%s", f->string);\
									\
		if (BCH_HAS_ ## type ## _FEATURE(&c->cache->sb, f->mask))	\
			out += snprintf(out, buf + size - out, "]");	\
									\
		first = false;						\
	}								\
	if (!first)							\
		out += snprintf(out, buf + size - out, "\n");		\
})

int bch_print_cache_set_feature_compat(struct cache_set *c, char *buf, int size)
{
	char *out = buf;
	compose_feature_string(COMPAT);
	return out - buf;
}

int bch_print_cache_set_feature_ro_compat(struct cache_set *c, char *buf, int size)
{
	char *out = buf;
	compose_feature_string(RO_COMPAT);
	return out - buf;
}

int bch_print_cache_set_feature_incompat(struct cache_set *c, char *buf, int size)
{
	char *out = buf;
	compose_feature_string(INCOMPAT);
	return out - buf;
}
