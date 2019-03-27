/*
 * Hotspot 2.0 client - Web browser
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef BROWSER_H
#define BROWSER_H

#ifdef CONFIG_NO_BROWSER
static inline int hs20_web_browser(const char *url)
{
	return -1;
}
#else /* CONFIG_NO_BROWSER */
int hs20_web_browser(const char *url);
#endif /* CONFIG_NO_BROWSER */

#endif /* BROWSER_H */
