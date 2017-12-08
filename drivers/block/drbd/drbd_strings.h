/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __DRBD_STRINGS_H
#define __DRBD_STRINGS_H

extern const char *drbd_conn_str(enum drbd_conns);
extern const char *drbd_role_str(enum drbd_role);
extern const char *drbd_disk_str(enum drbd_disk_state);
extern const char *drbd_set_st_err_str(enum drbd_state_rv);

#endif  /* __DRBD_STRINGS_H */
