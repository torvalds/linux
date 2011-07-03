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
	PacketInfo const *pa = a;
	PacketInfo const *pb = b;

	if (!pa->bValid || !pb->bValid)
		return 0;

	return pa->u8TrafficPriority - pb->u8TrafficPriority;
}

VOID SortPackInfo(PMINI_ADAPTER Adapter)
{
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG,
			DBG_LVL_ALL, "<=======");

	sort(Adapter->PackInfo, NO_OF_QUEUES, sizeof(PacketInfo),
		compare_packet_info, NULL);
}

static int compare_classifiers(void const *a, void const *b)
{
	S_CLASSIFIER_RULE const *pa = a;
	S_CLASSIFIER_RULE const *pb = b;

	if (!pa->bUsed || !pb->bUsed)
		return 0;

	return pa->u8ClassifierRulePriority - pb->u8ClassifierRulePriority;
}

VOID SortClassifiers(PMINI_ADAPTER Adapter)
{
	BCM_DEBUG_PRINT(Adapter, DBG_TYPE_OTHERS, CONN_MSG,
			DBG_LVL_ALL, "<=======");

	sort(Adapter->astClassifierTable, MAX_CLASSIFIERS,
		sizeof(S_CLASSIFIER_RULE), compare_classifiers, NULL);
}
