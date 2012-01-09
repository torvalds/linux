/*
 * Copyright (C) 2011 ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 * Debugfs interface to the AB5500 core driver
 */

#ifdef CONFIG_DEBUG_FS

void ab5500_setup_debugfs(struct ab5500 *ab);
void ab5500_remove_debugfs(void);

#else /* !CONFIG_DEBUG_FS */

static inline void ab5500_setup_debugfs(struct ab5500 *ab)
{
}

static inline void ab5500_remove_debugfs(void)
{
}

#endif
