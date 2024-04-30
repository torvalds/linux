/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __watchcat_PRETIMEOUT_H
#define __watchcat_PRETIMEOUT_H

#define watchcat_GOV_NAME_MAXLEN	20

struct watchcat_device;

struct watchcat_governor {
	const char	name[watchcat_GOV_NAME_MAXLEN];
	void		(*pretimeout)(struct watchcat_device *wdd);
};

#if IS_ENABLED(CONFIG_watchcat_PRETIMEOUT_GOV)
/* Interfaces to watchcat pretimeout governors */
int watchcat_register_governor(struct watchcat_governor *gov);
void watchcat_unregister_governor(struct watchcat_governor *gov);

/* Interfaces to watchcat_dev.c */
int watchcat_register_pretimeout(struct watchcat_device *wdd);
void watchcat_unregister_pretimeout(struct watchcat_device *wdd);
int watchcat_pretimeout_available_governors_get(char *buf);
int watchcat_pretimeout_governor_get(struct watchcat_device *wdd, char *buf);
int watchcat_pretimeout_governor_set(struct watchcat_device *wdd,
				     const char *buf);

#if IS_ENABLED(CONFIG_watchcat_PRETIMEOUT_DEFAULT_GOV_NOOP)
#define watchcat_PRETIMEOUT_DEFAULT_GOV		"noop"
#elif IS_ENABLED(CONFIG_watchcat_PRETIMEOUT_DEFAULT_GOV_PANIC)
#define watchcat_PRETIMEOUT_DEFAULT_GOV		"panic"
#endif

#else
static inline int watchcat_register_pretimeout(struct watchcat_device *wdd)
{
	return 0;
}

static inline void watchcat_unregister_pretimeout(struct watchcat_device *wdd)
{
}

static inline int watchcat_pretimeout_available_governors_get(char *buf)
{
	return -EINVAL;
}

static inline int watchcat_pretimeout_governor_get(struct watchcat_device *wdd,
						   char *buf)
{
	return -EINVAL;
}

static inline int watchcat_pretimeout_governor_set(struct watchcat_device *wdd,
						   const char *buf)
{
	return -EINVAL;
}
#endif

#endif
