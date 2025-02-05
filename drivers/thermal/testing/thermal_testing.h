/* SPDX-License-Identifier: GPL-2.0 */

extern struct dentry *d_testing;

int tt_add_tz(void);
int tt_del_tz(const char *arg);
int tt_zone_add_trip(const char *arg);
int tt_zone_reg(const char *arg);
int tt_zone_unreg(const char *arg);

void tt_zone_cleanup(void);
