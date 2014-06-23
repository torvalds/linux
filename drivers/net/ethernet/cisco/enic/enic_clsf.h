#ifndef _ENIC_CLSF_H_
#define _ENIC_CLSF_H_

#include "vnic_dev.h"
#include "enic.h"

int enic_addfltr_5t(struct enic *enic, struct flow_keys *keys, u16 rq);
int enic_delfltr(struct enic *enic, u16 filter_id);

#endif /* _ENIC_CLSF_H_ */
