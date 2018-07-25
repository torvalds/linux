/*
 * Copyright (c) 2015 South Silicon Valley Microelectronics Inc.
 * Copyright (c) 2015 iComm Corporation
 *
 * This program is free software: you can redistribute it and/or modify 
 * it under the terms of the GNU General Public License as published by 
 * the Free Software Foundation, either version 3 of the License, or 
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/export.h>
#include <asm/unaligned.h>
#include "debug.h"
int ssv_sdiobridge_init_debug(struct ssv_sdiobridge_glue *glue)
{
 glue->debugfs = debugfs_create_dir("ssv",
         NULL);
 if (!glue->debugfs)
  return -ENOMEM;
 glue->dump_entry = debugfs_create_bool("sdiobridge_dump", S_IRUSR, glue->debugfs, &glue->dump);
 return 0;
}
void ssv_sdiobridge_deinit_debug(struct ssv_sdiobridge_glue *glue)
{
    if (!glue->dump_entry)
  debugfs_remove(glue->dump_entry);
}
