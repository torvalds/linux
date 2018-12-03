/* Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _DPU_POWER_HANDLE_H_
#define _DPU_POWER_HANDLE_H_

#define MAX_CLIENT_NAME_LEN 128

#define DPU_POWER_HANDLE_ENABLE_BUS_AB_QUOTA	0
#define DPU_POWER_HANDLE_DISABLE_BUS_AB_QUOTA	0
#define DPU_POWER_HANDLE_ENABLE_BUS_IB_QUOTA	1600000000
#define DPU_POWER_HANDLE_DISABLE_BUS_IB_QUOTA	0

#include "dpu_io_util.h"

/* events will be triggered on power handler enable/disable */
#define DPU_POWER_EVENT_DISABLE	BIT(0)
#define DPU_POWER_EVENT_ENABLE	BIT(1)

/**
 * mdss_bus_vote_type: register bus vote type
 * VOTE_INDEX_DISABLE: removes the client vote
 * VOTE_INDEX_LOW: keeps the lowest vote for register bus
 * VOTE_INDEX_MAX: invalid
 */
enum mdss_bus_vote_type {
	VOTE_INDEX_DISABLE,
	VOTE_INDEX_LOW,
	VOTE_INDEX_MAX,
};

/**
 * enum dpu_power_handle_data_bus_client - type of axi bus clients
 * @DPU_POWER_HANDLE_DATA_BUS_CLIENT_RT: core real-time bus client
 * @DPU_POWER_HANDLE_DATA_BUS_CLIENT_NRT: core non-real-time bus client
 * @DPU_POWER_HANDLE_DATA_BUS_CLIENT_MAX: maximum number of bus client type
 */
enum dpu_power_handle_data_bus_client {
	DPU_POWER_HANDLE_DATA_BUS_CLIENT_RT,
	DPU_POWER_HANDLE_DATA_BUS_CLIENT_NRT,
	DPU_POWER_HANDLE_DATA_BUS_CLIENT_MAX
};

/**
 * enum DPU_POWER_HANDLE_DBUS_ID - data bus identifier
 * @DPU_POWER_HANDLE_DBUS_ID_MNOC: DPU/MNOC data bus
 * @DPU_POWER_HANDLE_DBUS_ID_LLCC: MNOC/LLCC data bus
 * @DPU_POWER_HANDLE_DBUS_ID_EBI: LLCC/EBI data bus
 */
enum DPU_POWER_HANDLE_DBUS_ID {
	DPU_POWER_HANDLE_DBUS_ID_MNOC,
	DPU_POWER_HANDLE_DBUS_ID_LLCC,
	DPU_POWER_HANDLE_DBUS_ID_EBI,
	DPU_POWER_HANDLE_DBUS_ID_MAX,
};

/**
 * struct dpu_power_client: stores the power client for dpu driver
 * @name:	name of the client
 * @usecase_ndx: current regs bus vote type
 * @refcount:	current refcount if multiple modules are using same
 *              same client for enable/disable. Power module will
 *              aggregate the refcount and vote accordingly for this
 *              client.
 * @id:		assigned during create. helps for debugging.
 * @list:	list to attach power handle master list
 * @ab:         arbitrated bandwidth for each bus client
 * @ib:         instantaneous bandwidth for each bus client
 * @active:	inidcates the state of dpu power handle
 */
struct dpu_power_client {
	char name[MAX_CLIENT_NAME_LEN];
	short usecase_ndx;
	short refcount;
	u32 id;
	struct list_head list;
	u64 ab[DPU_POWER_HANDLE_DATA_BUS_CLIENT_MAX];
	u64 ib[DPU_POWER_HANDLE_DATA_BUS_CLIENT_MAX];
	bool active;
};

/*
 * struct dpu_power_event - local event registration structure
 * @client_name: name of the client registering
 * @cb_fnc: pointer to desired callback function
 * @usr: user pointer to pass to callback event trigger
 * @event: refer to DPU_POWER_HANDLE_EVENT_*
 * @list: list to attach event master list
 * @active: indicates the state of dpu power handle
 */
struct dpu_power_event {
	char client_name[MAX_CLIENT_NAME_LEN];
	void (*cb_fnc)(u32 event_type, void *usr);
	void *usr;
	u32 event_type;
	struct list_head list;
	bool active;
};

/**
 * struct dpu_power_handle: power handle main struct
 * @client_clist: master list to store all clients
 * @phandle_lock: lock to synchronize the enable/disable
 * @dev: pointer to device structure
 * @usecase_ndx: current usecase index
 * @event_list: current power handle event list
 */
struct dpu_power_handle {
	struct list_head power_client_clist;
	struct mutex phandle_lock;
	struct device *dev;
	u32 current_usecase_ndx;
	struct list_head event_list;
};

/**
 * dpu_power_resource_init() - initializes the dpu power handle
 * @pdev:   platform device to search the power resources
 * @pdata:  power handle to store the power resources
 */
void dpu_power_resource_init(struct platform_device *pdev,
	struct dpu_power_handle *pdata);

/**
 * dpu_power_resource_deinit() - release the dpu power handle
 * @pdev:   platform device for power resources
 * @pdata:  power handle containing the resources
 *
 * Return: error code.
 */
void dpu_power_resource_deinit(struct platform_device *pdev,
	struct dpu_power_handle *pdata);

/**
 * dpu_power_client_create() - create the client on power handle
 * @pdata:  power handle containing the resources
 * @client_name: new client name for registration
 *
 * Return: error code.
 */
struct dpu_power_client *dpu_power_client_create(struct dpu_power_handle *pdata,
	char *client_name);

/**
 * dpu_power_client_destroy() - destroy the client on power handle
 * @pdata:  power handle containing the resources
 * @client_name: new client name for registration
 *
 * Return: none
 */
void dpu_power_client_destroy(struct dpu_power_handle *phandle,
	struct dpu_power_client *client);

/**
 * dpu_power_resource_enable() - enable/disable the power resources
 * @pdata:  power handle containing the resources
 * @client: client information to enable/disable its vote
 * @enable: boolean request for enable/disable
 *
 * Return: error code.
 */
int dpu_power_resource_enable(struct dpu_power_handle *pdata,
	struct dpu_power_client *pclient, bool enable);

/**
 * dpu_power_data_bus_bandwidth_ctrl() - control data bus bandwidth enable
 * @phandle:  power handle containing the resources
 * @client: client information to bandwidth control
 * @enable: true to enable bandwidth for data base
 *
 * Return: none
 */
void dpu_power_data_bus_bandwidth_ctrl(struct dpu_power_handle *phandle,
		struct dpu_power_client *pclient, int enable);

/**
 * dpu_power_handle_register_event - register a callback function for an event.
 *	Clients can register for multiple events with a single register.
 *	Any block with access to phandle can register for the event
 *	notification.
 * @phandle:	power handle containing the resources
 * @event_type:	event type to register; refer DPU_POWER_HANDLE_EVENT_*
 * @cb_fnc:	pointer to desired callback function
 * @usr:	user pointer to pass to callback on event trigger
 *
 * Return:	event pointer if success, or error code otherwise
 */
struct dpu_power_event *dpu_power_handle_register_event(
		struct dpu_power_handle *phandle,
		u32 event_type, void (*cb_fnc)(u32 event_type, void *usr),
		void *usr, char *client_name);
/**
 * dpu_power_handle_unregister_event - unregister callback for event(s)
 * @phandle:	power handle containing the resources
 * @event:	event pointer returned after power handle register
 */
void dpu_power_handle_unregister_event(struct dpu_power_handle *phandle,
		struct dpu_power_event *event);

/**
 * dpu_power_handle_get_dbus_name - get name of given data bus identifier
 * @bus_id:	data bus identifier
 * Return:	Pointer to name string if success; NULL otherwise
 */
const char *dpu_power_handle_get_dbus_name(u32 bus_id);

#endif /* _DPU_POWER_HANDLE_H_ */
