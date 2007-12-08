/* Copyright (C) 2006, Red Hat, Inc. */

#ifndef _LBS_ASSOC_H_
#define _LBS_ASSOC_H_

#include "dev.h"

void lbs_association_worker(struct work_struct *work);
struct assoc_request *lbs_get_association_request(struct lbs_private *priv);
void lbs_sync_channel(struct work_struct *work);

#endif /* _LBS_ASSOC_H */
