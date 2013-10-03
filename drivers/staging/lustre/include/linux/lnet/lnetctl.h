/*
 *   This file is part of Portals, http://www.sf.net/projects/lustre/
 *
 *   Portals is free software; you can redistribute it and/or
 *   modify it under the terms of version 2 of the GNU General Public
 *   License as published by the Free Software Foundation.
 *
 *   Portals is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with Portals; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * header for libptlctl.a
 */
#ifndef _PTLCTL_H_
#define _PTLCTL_H_

#include <linux/libcfs/libcfs.h>
#include <linux/lnet/types.h>

#define LNET_DEV_ID 0
#define LNET_DEV_PATH "/dev/lnet"
#define LNET_DEV_MAJOR 10
#define LNET_DEV_MINOR 240
#define OBD_DEV_ID 1
#define OBD_DEV_NAME "obd"
#define OBD_DEV_PATH "/dev/" OBD_DEV_NAME
#define OBD_DEV_MAJOR 10
#define OBD_DEV_MINOR 241
#define SMFS_DEV_ID  2
#define SMFS_DEV_PATH "/dev/snapdev"
#define SMFS_DEV_MAJOR 10
#define SMFS_DEV_MINOR 242

int ptl_initialize(int argc, char **argv);
int jt_ptl_network(int argc, char **argv);
int jt_ptl_list_nids(int argc, char **argv);
int jt_ptl_which_nid(int argc, char **argv);
int jt_ptl_print_interfaces(int argc, char **argv);
int jt_ptl_add_interface(int argc, char **argv);
int jt_ptl_del_interface(int argc, char **argv);
int jt_ptl_print_peers (int argc, char **argv);
int jt_ptl_add_peer (int argc, char **argv);
int jt_ptl_del_peer (int argc, char **argv);
int jt_ptl_print_connections (int argc, char **argv);
int jt_ptl_disconnect(int argc, char **argv);
int jt_ptl_push_connection(int argc, char **argv);
int jt_ptl_print_active_txs(int argc, char **argv);
int jt_ptl_ping(int argc, char **argv);
int jt_ptl_mynid(int argc, char **argv);
int jt_ptl_add_uuid(int argc, char **argv);
int jt_ptl_add_uuid_old(int argc, char **argv); /* backwards compatibility  */
int jt_ptl_close_uuid(int argc, char **argv);
int jt_ptl_del_uuid(int argc, char **argv);
int jt_ptl_add_route (int argc, char **argv);
int jt_ptl_del_route (int argc, char **argv);
int jt_ptl_notify_router (int argc, char **argv);
int jt_ptl_print_routes (int argc, char **argv);
int jt_ptl_fail_nid (int argc, char **argv);
int jt_ptl_lwt(int argc, char **argv);
int jt_ptl_testprotocompat(int argc, char **argv);
int jt_ptl_memhog(int argc, char **argv);

int dbg_initialize(int argc, char **argv);
int jt_dbg_filter(int argc, char **argv);
int jt_dbg_show(int argc, char **argv);
int jt_dbg_list(int argc, char **argv);
int jt_dbg_debug_kernel(int argc, char **argv);
int jt_dbg_debug_daemon(int argc, char **argv);
int jt_dbg_debug_file(int argc, char **argv);
int jt_dbg_clear_debug_buf(int argc, char **argv);
int jt_dbg_mark_debug_buf(int argc, char **argv);
int jt_dbg_modules(int argc, char **argv);
int jt_dbg_panic(int argc, char **argv);

#endif
