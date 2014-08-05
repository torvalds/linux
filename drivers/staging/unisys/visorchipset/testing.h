/* testing.h
 *
 * Copyright (C) 2010 - 2013 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __VISORCHIPSET_TESTING_H__
#define __VISORCHIPSET_TESTING_H__

#define VISORCHIPSET_TEST_PROC
#include <linux/uuid.h>
#include "globals.h"
#include "controlvmchannel.h"

void test_produce_test_message(CONTROLVM_MESSAGE *msg, int isLocalTestAddr);
BOOL test_consume_test_message(CONTROLVM_MESSAGE *msg);
void test_manufacture_vnic_client_add(void *p);
void test_manufacture_vnic_client_add_phys(HOSTADDRESS addr);
void test_manufacture_preamble_messages(void);
void test_manufacture_device_attach(ulong busNo, ulong devNo);
void test_manufacture_device_add(ulong busNo, ulong devNo, uuid_le dataTypeGuid,
				 void *pChannel);
void test_manufacture_add_bus(ulong busNo, ulong maxDevices,
			      uuid_le id, u8 *name, BOOL isServer);
void test_manufacture_device_destroy(ulong busNo, ulong devNo);
void test_manufacture_bus_destroy(ulong busNo);
void test_manufacture_detach_externalPort(ulong switchNo, ulong externalPortNo);
void test_manufacture_detach_internalPort(ulong switchNo, ulong internalPortNo);
void test_cleanup(void);

#endif
