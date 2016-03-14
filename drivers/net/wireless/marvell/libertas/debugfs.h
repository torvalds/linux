#ifndef _LBS_DEBUGFS_H_
#define _LBS_DEBUGFS_H_

void lbs_debugfs_init(void);
void lbs_debugfs_remove(void);

void lbs_debugfs_init_one(struct lbs_private *priv, struct net_device *dev);
void lbs_debugfs_remove_one(struct lbs_private *priv);

#endif
