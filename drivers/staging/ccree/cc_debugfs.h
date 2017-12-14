/*
 * Copyright (C) 2012-2017 ARM Limited or its affiliates.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __CC_DEBUGFS_H__
#define __CC_DEBUGFS_H__

#ifdef CONFIG_DEBUG_FS
int cc_debugfs_global_init(void);
void cc_debugfs_global_fini(void);

int cc_debugfs_init(struct cc_drvdata *drvdata);
void cc_debugfs_fini(struct cc_drvdata *drvdata);

#else

int cc_debugfs_global_init(void)
{
	return 0;
}

void cc_debugfs_global_fini(void) {}

int cc_debugfs_init(struct cc_drvdata *drvdata)
{
	return 0;
}

void cc_debugfs_fini(struct cc_drvdata *drvdata) {}

#endif

#endif /*__CC_SYSFS_H__*/
