/*
 * Linux 2.6.32 and later Kernel module for VMware MVP PVTCP Server
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 *  @file
 *
 *  @brief Linux-specific module loading, unloading functions.
 */

#include "comm_os.h"
#include "comm_os_mod_ver.h"

#include <linux/moduleparam.h>


/* Module parameters -- passed as one 'name=value'-list string. */

static char modParams[256];
module_param_string(COMM_OS_MOD_SHORT_NAME, modParams, sizeof modParams, 0644);


/**
 *  @brief Module initialization entry point. Calls the commOSModInit
 *      function pointer to perform upper layer initialization.
 *  @return zero if successful, non-zero otherwise.
 */

static int __init
ModInit(void)
{
   int rc;

   if (!commOSModInit) {
      CommOS_Log(("%s: Can't find \'init\' function for module \'" \
                  COMM_OS_MOD_SHORT_NAME_STRING "\'.\n", __FUNCTION__));
      return -1;
   }

   CommOS_Debug(("%s: Module parameters: [%s].\n", __FUNCTION__, modParams));

   rc = (*commOSModInit)(modParams);
   if (rc == 0) {
      CommOS_Log(("%s: Module \'" COMM_OS_MOD_SHORT_NAME_STRING \
                  "\' has been successfully initialized.\n", __FUNCTION__));
   } else {
      CommOS_Log(("%s: Module \'" COMM_OS_MOD_SHORT_NAME_STRING \
                  "\' could not be initialized [%d].\n", __FUNCTION__, rc));
   }

   return rc > 0 ? -rc : rc;
}


/**
 *  @brief Module exit function. Calls the commOSModExit function pointer
 *      to perform upper layer cleanup.
 */

static void __exit
ModExit(void)
{
   if (!commOSModExit) {
      CommOS_Log(("%s: Can't find \'fini\' function for module \'" \
                  COMM_OS_MOD_SHORT_NAME_STRING "\'.\n", __FUNCTION__));
      return;
   }

   (*commOSModExit)();
   CommOS_Log(("%s: Module \'" COMM_OS_MOD_SHORT_NAME_STRING \
               "\' has been stopped.\n", __FUNCTION__));
}


module_init(ModInit);
module_exit(ModExit);

/* Module information. */
MODULE_AUTHOR("VMware, Inc.");
MODULE_DESCRIPTION(COMM_OS_MOD_NAME_STRING);
MODULE_VERSION(COMM_OS_MOD_VERSION_STRING);
MODULE_LICENSE("GPL v2");
/*
 * Starting with SLE10sp2, Novell requires that IHVs sign a support agreement
 * with them and mark their kernel modules as externally supported via a
 * change to the module header. If this isn't done, the module will not load
 * by default (i.e., neither mkinitrd nor modprobe will accept it).
 */
MODULE_INFO(supported, "external");
