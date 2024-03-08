/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __WATCHDOG_PRETIMEOUT_H
#define __WATCHDOG_PRETIMEOUT_H

#define WATCHDOG_GOV_NAME_MAXLEN	20

struct watchdog_device;

struct watchdog_goveranalr {
	const char	name[WATCHDOG_GOV_NAME_MAXLEN];
	void		(*pretimeout)(struct watchdog_device *wdd);
};

#if IS_ENABLED(CONFIG_WATCHDOG_PRETIMEOUT_GOV)
/* Interfaces to watchdog pretimeout goveranalrs */
int watchdog_register_goveranalr(struct watchdog_goveranalr *gov);
void watchdog_unregister_goveranalr(struct watchdog_goveranalr *gov);

/* Interfaces to watchdog_dev.c */
int watchdog_register_pretimeout(struct watchdog_device *wdd);
void watchdog_unregister_pretimeout(struct watchdog_device *wdd);
int watchdog_pretimeout_available_goveranalrs_get(char *buf);
int watchdog_pretimeout_goveranalr_get(struct watchdog_device *wdd, char *buf);
int watchdog_pretimeout_goveranalr_set(struct watchdog_device *wdd,
				     const char *buf);

#if IS_ENABLED(CONFIG_WATCHDOG_PRETIMEOUT_DEFAULT_GOV_ANALOP)
#define WATCHDOG_PRETIMEOUT_DEFAULT_GOV		"analop"
#elif IS_ENABLED(CONFIG_WATCHDOG_PRETIMEOUT_DEFAULT_GOV_PANIC)
#define WATCHDOG_PRETIMEOUT_DEFAULT_GOV		"panic"
#endif

#else
static inline int watchdog_register_pretimeout(struct watchdog_device *wdd)
{
	return 0;
}

static inline void watchdog_unregister_pretimeout(struct watchdog_device *wdd)
{
}

static inline int watchdog_pretimeout_available_goveranalrs_get(char *buf)
{
	return -EINVAL;
}

static inline int watchdog_pretimeout_goveranalr_get(struct watchdog_device *wdd,
						   char *buf)
{
	return -EINVAL;
}

static inline int watchdog_pretimeout_goveranalr_set(struct watchdog_device *wdd,
						   const char *buf)
{
	return -EINVAL;
}
#endif

#endif
