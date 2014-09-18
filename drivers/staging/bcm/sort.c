#include "headers.h"
#include <linux/sort.h>

/*
 * File Name: sort.c
 *
 * Author: Beceem Communications Pvt. Ltd
 *
 * Abstract: This file contains the routines sorting the classification rules.
 *
 * Copyright (c) 2007 Beceem Communications Pvt. Ltd
 */

static int compare_packet_info(void const *a, void const *b)
{
	struct bcm_packet_info const *pa = a;
	struct bcm_packet_info const *pb = b;

	if (!pa->bValid || !pb->bValid)
		return 0;

	return pa->u8TrafficPriority - pb->u8TrafficPriority;
}

VOID SortPackInfo(struct bcm_mini_adapter *Adapter)
{
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG,
			DBG_LVL_ALL, "<=======");

	sort(Adapter->PackInfo, NO_OF_QUEUES, sizeof(struct bcm_packet_info),
		compare_packet_info, NULL);
}

static int compare_classifiers(void const *a, void const *b)
{
	struct bcm_classifier_rule const *pa = a;
	struct bcm_classifier_rule const *pb = b;

	if (!pa->bUsed || !pb->bUsed)
		return 0;

	return pa->u8ClassifierRulePriority - pb->u8ClassifierRulePriority;
}

VOID SortClassifiers(struct bcm_mini_adapter *Adapter)
{
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG,
			DBG_LVL_ALL, "<=======");

	sort(Adapter->astClassifierTable, MAX_CLASSIFIERS,
		sizeof(struct bcm_classifier_rule), compare_classifiers, NULL);
}
