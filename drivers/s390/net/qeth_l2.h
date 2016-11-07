/*
 *    Copyright IBM Corp. 2013
 *    Author(s): Eugene Crosser <eugene.crosser@ru.ibm.com>
 */

#ifndef __QETH_L2_H__
#define __QETH_L2_H__

#include "qeth_core.h"

int qeth_l2_create_device_attributes(struct device *);
void qeth_l2_remove_device_attributes(struct device *);
void qeth_l2_setup_bridgeport_attrs(struct qeth_card *card);

struct qeth_mac {
	u8 mac_addr[OSA_ADDR_LEN];
	u8 is_uc:1;
	u8 disp_flag:2;
	struct hlist_node hnode;
};

#endif /* __QETH_L2_H__ */
