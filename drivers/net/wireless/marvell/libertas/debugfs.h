/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LBS_DEFS_H_
#define _LBS_DEFS_H_

void lbs_defs_init(void);
void lbs_defs_remove(void);

void lbs_defs_init_one(struct lbs_private *priv, struct net_device *dev);
void lbs_defs_remove_one(struct lbs_private *priv);

#endif
