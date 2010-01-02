/*
 * Copyright (C) 2007-2009 B.A.T.M.A.N. contributors:
 *
 * Marek Lindner, Simon Wunderlich
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA
 *
 */

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#define PROC_ROOT_DIR "batman-adv"
#define PROC_FILE_INTERFACES "interfaces"
#define PROC_FILE_ORIG_INTERVAL "orig_interval"
#define PROC_FILE_ORIGINATORS "originators"
#define PROC_FILE_GATEWAYS "gateways"
#define PROC_FILE_LOG "log"
#define PROC_FILE_LOG_LEVEL "log_level"
#define PROC_FILE_TRANST_LOCAL "transtable_local"
#define PROC_FILE_TRANST_GLOBAL "transtable_global"
#define PROC_FILE_VIS "vis"
#define PROC_FILE_VIS_FORMAT "vis_format"
#define PROC_FILE_AGGR "aggregate_ogm"

void cleanup_procfs(void);
int setup_procfs(void);

