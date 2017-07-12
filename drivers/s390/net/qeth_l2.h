/*
 *    Copyright IBM Corp. 2013
 *    Author(s): Eugene Crosser <eugene.crosser@ru.ibm.com>
 */

#ifndef __QETH_L2_H__
#define __QETH_L2_H__

#include "qeth_core.h"

extern const struct attribute_group *qeth_l2_attr_groups[];

int qeth_l2_create_device_attributes(struct device *);
void qeth_l2_remove_device_attributes(struct device *);
void qeth_l2_setup_bridgeport_attrs(struct qeth_card *card);

#endif /* __QETH_L2_H__ */
