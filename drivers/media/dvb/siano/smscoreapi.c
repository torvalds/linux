/*
 *  Siano core API module
 *
 *  This file contains implementation for the interface to sms core component
 *
 *  author: Anatoly Greenblat
 *
 *  Copyright (c), 2005-2008 Siano Mobile Silicon, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation;
 *
 *  Software distributed under the License is distributed on an "AS IS"
 *  basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 *
 *  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>

#include <linux/firmware.h>

#include "smscoreapi.h"
#include "sms-cards.h"

int sms_debug;
module_param_named(debug, sms_debug, int, 0644);
MODULE_PARM_DESC(debug, "set debug level (info=1, adv=2 (or-able))");

struct smscore_device_notifyee_t {
	struct list_head entry;
	hotplug_t hotplug;
};

struct smscore_idlist_t {
	struct list_head entry;
	int		id;
	int		data_type;
};

struct smscore_client_t {
	struct list_head entry;
	struct smscore_device_t *coredev;
	void			*context;
	struct list_head 	idlist;
	onresponse_t	onresponse_handler;
	onremove_t		onremove_handler;
};

struct smscore_device_t {
	struct list_head entry;

	struct list_head clients;
	struct list_head subclients;
	spinlock_t		clientslock;

	struct list_head buffers;
	spinlock_t		bufferslock;
	int				num_buffers;

	void			*common_buffer;
	int				common_buffer_size;
	dma_addr_t		common_buffer_phys;

	void			*context;
	struct device	*device;

	char			devpath[32];
	unsigned long	device_flags;

	setmode_t		setmode_handler;
	detectmode_t	detectmode_handler;
	sendrequest_t	sendrequest_handler;
	preload_t		preload_handler;
	postload_t		postload_handler;

	int				mode, modes_supported;

	struct completion version_ex_done, data_download_done, trigger_done;
	struct completion init_device_done, reload_start_done, resume_done;

	int board_id;
};

void smscore_set_board_id(struct smscore_device_t *core, int id)
{
	core->board_id = id;
}

int smscore_get_board_id(struct smscore_device_t *core)
{
	return core->board_id;
}

struct smscore_registry_entry_t {
	struct list_head entry;
	char			devpath[32];
	int				mode;
	enum sms_device_type_st	type;
};

static struct list_head g_smscore_notifyees;
static struct list_head g_smscore_devices;
static struct mutex g_smscore_deviceslock;

static struct list_head g_smscore_registry;
static struct mutex g_smscore_registrylock;

static int default_mode = 4;

module_param(default_mode, int, 0644);
MODULE_PARM_DESC(default_mode, "default firmware id (device mode)");

static struct smscore_registry_entry_t *smscore_find_registry(char *devpath)
{
	struct smscore_registry_entry_t *entry;
	struct list_head *next;

	kmutex_lock(&g_smscore_registrylock);
	for (next = g_smscore_registry.next;
	     next != &g_smscore_registry;
	     next = next->next) {
		entry = (struct smscore_registry_entry_t *) next;
		if (!strcmp(entry->devpath, devpath)) {
			kmutex_unlock(&g_smscore_registrylock);
			return entry;
		}
	}
	entry = (struct smscore_registry_entry_t *)
			kmalloc(sizeof(struct smscore_registry_entry_t),
				GFP_KERNEL);
	if (entry) {
		entry->mode = default_mode;
		strcpy(entry->devpath, devpath);
		list_add(&entry->entry, &g_smscore_registry);
	} else
		sms_err("failed to create smscore_registry.");
	kmutex_unlock(&g_smscore_registrylock);
	return entry;
}

int smscore_registry_getmode(char *devpath)
{
	struct smscore_registry_entry_t *entry;

	entry = smscore_find_registry(devpath);
	if (entry)
		return entry->mode;
	else
		sms_err("No registry found.");

	return default_mode;
}

static enum sms_device_type_st smscore_registry_gettype(char *devpath)
{
	struct smscore_registry_entry_t *entry;

	entry = smscore_find_registry(devpath);
	if (entry)
		return entry->type;
	else
		sms_err("No registry found.");

	return -1;
}

void smscore_registry_setmode(char *devpath, int mode)
{
	struct smscore_registry_entry_t *entry;

	entry = smscore_find_registry(devpath);
	if (entry)
		entry->mode = mode;
	else
		sms_err("No registry found.");
}

static void smscore_registry_settype(char *devpath,
				     enum sms_device_type_st type)
{
	struct smscore_registry_entry_t *entry;

	entry = smscore_find_registry(devpath);
	if (entry)
		entry->type = type;
	else
		sms_err("No registry found.");
}


static void list_add_locked(struct list_head *new, struct list_head *head,
			    spinlock_t *lock)
{
	unsigned long flags;

	spin_lock_irqsave(lock, flags);

	list_add(new, head);

	spin_unlock_irqrestore(lock, flags);
}

/**
 * register a client callback that called when device plugged in/unplugged
 * NOTE: if devices exist callback is called immediately for each device
 *
 * @param hotplug callback
 *
 * @return 0 on success, <0 on error.
 */
int smscore_register_hotplug(hotplug_t hotplug)
{
	struct smscore_device_notifyee_t *notifyee;
	struct list_head *next, *first;
	int rc = 0;

	kmutex_lock(&g_smscore_deviceslock);

	notifyee = kmalloc(sizeof(struct smscore_device_notifyee_t),
			   GFP_KERNEL);
	if (notifyee) {
		/* now notify callback about existing devices */
		first = &g_smscore_devices;
		for (next = first->next;
		     next != first && !rc;
		     next = next->next) {
			struct smscore_device_t *coredev =
				(struct smscore_device_t *) next;
			rc = hotplug(coredev, coredev->device, 1);
		}

		if (rc >= 0) {
			notifyee->hotplug = hotplug;
			list_add(&notifyee->entry, &g_smscore_notifyees);
		} else
			kfree(notifyee);
	} else
		rc = -ENOMEM;

	kmutex_unlock(&g_smscore_deviceslock);

	return rc;
}

/**
 * unregister a client callback that called when device plugged in/unplugged
 *
 * @param hotplug callback
 *
 */
void smscore_unregister_hotplug(hotplug_t hotplug)
{
	struct list_head *next, *first;

	kmutex_lock(&g_smscore_deviceslock);

	first = &g_smscore_notifyees;

	for (next = first->next; next != first;) {
		struct smscore_device_notifyee_t *notifyee =
			(struct smscore_device_notifyee_t *) next;
		next = next->next;

		if (notifyee->hotplug == hotplug) {
			list_del(&notifyee->entry);
			kfree(notifyee);
		}
	}

	kmutex_unlock(&g_smscore_deviceslock);
}

static void smscore_notify_clients(struct smscore_device_t *coredev)
{
	struct smscore_client_t *client;

	/* the client must call smscore_unregister_client from remove handler */
	while (!list_empty(&coredev->clients)) {
		client = (struct smscore_client_t *) coredev->clients.next;
		client->onremove_handler(client->context);
	}
}

static int smscore_notify_callbacks(struct smscore_device_t *coredev,
				    struct device *device, int arrival)
{
	struct list_head *next, *first;
	int rc = 0;

	/* note: must be called under g_deviceslock */

	first = &g_smscore_notifyees;

	for (next = first->next; next != first; next = next->next) {
		rc = ((struct smscore_device_notifyee_t *) next)->
				hotplug(coredev, device, arrival);
		if (rc < 0)
			break;
	}

	return rc;
}

static struct
smscore_buffer_t *smscore_createbuffer(u8 *buffer, void *common_buffer,
				       dma_addr_t common_buffer_phys)
{
	struct smscore_buffer_t *cb =
		kmalloc(sizeof(struct smscore_buffer_t), GFP_KERNEL);
	if (!cb) {
		sms_info("kmalloc(...) failed");
		return NULL;
	}

	cb->p = buffer;
	cb->offset_in_common = buffer - (u8 *) common_buffer;
	cb->phys = common_buffer_phys + cb->offset_in_common;

	return cb;
}

/**
 * creates coredev object for a device, prepares buffers,
 * creates buffer mappings, notifies registered hotplugs about new device.
 *
 * @param params device pointer to struct with device specific parameters
 *               and handlers
 * @param coredev pointer to a value that receives created coredev object
 *
 * @return 0 on success, <0 on error.
 */
int smscore_register_device(struct smsdevice_params_t *params,
			    struct smscore_device_t **coredev)
{
	struct smscore_device_t *dev;
	u8 *buffer;

	dev = kzalloc(sizeof(struct smscore_device_t), GFP_KERNEL);
	if (!dev) {
		sms_info("kzalloc(...) failed");
		return -ENOMEM;
	}

	/* init list entry so it could be safe in smscore_unregister_device */
	INIT_LIST_HEAD(&dev->entry);

	/* init queues */
	INIT_LIST_HEAD(&dev->clients);
	INIT_LIST_HEAD(&dev->buffers);

	/* init locks */
	spin_lock_init(&dev->clientslock);
	spin_lock_init(&dev->bufferslock);

	/* init completion events */
	init_completion(&dev->version_ex_done);
	init_completion(&dev->data_download_done);
	init_completion(&dev->trigger_done);
	init_completion(&dev->init_device_done);
	init_completion(&dev->reload_start_done);
	init_completion(&dev->resume_done);

	/* alloc common buffer */
	dev->common_buffer_size = params->buffer_size * params->num_buffers;
	dev->common_buffer = dma_alloc_coherent(NULL, dev->common_buffer_size,
						&dev->common_buffer_phys,
						GFP_KERNEL | GFP_DMA);
	if (!dev->common_buffer) {
		smscore_unregister_device(dev);
		return -ENOMEM;
	}

	/* prepare dma buffers */
	for (buffer = dev->common_buffer;
	     dev->num_buffers < params->num_buffers;
	     dev->num_buffers++, buffer += params->buffer_size) {
		struct smscore_buffer_t *cb =
			smscore_createbuffer(buffer, dev->common_buffer,
					     dev->common_buffer_phys);
		if (!cb) {
			smscore_unregister_device(dev);
			return -ENOMEM;
		}

		smscore_putbuffer(dev, cb);
	}

	sms_info("allocated %d buffers", dev->num_buffers);

	dev->mode = DEVICE_MODE_NONE;
	dev->context = params->context;
	dev->device = params->device;
	dev->setmode_handler = params->setmode_handler;
	dev->detectmode_handler = params->detectmode_handler;
	dev->sendrequest_handler = params->sendrequest_handler;
	dev->preload_handler = params->preload_handler;
	dev->postload_handler = params->postload_handler;

	dev->device_flags = params->flags;
	strcpy(dev->devpath, params->devpath);

	smscore_registry_settype(dev->devpath, params->device_type);

	/* add device to devices list */
	kmutex_lock(&g_smscore_deviceslock);
	list_add(&dev->entry, &g_smscore_devices);
	kmutex_unlock(&g_smscore_deviceslock);

	*coredev = dev;

	sms_info("device %p created", dev);

	return 0;
}

/**
 * sets initial device mode and notifies client hotplugs that device is ready
 *
 * @param coredev pointer to a coredev object returned by
 * 		  smscore_register_device
 *
 * @return 0 on success, <0 on error.
 */
int smscore_start_device(struct smscore_device_t *coredev)
{
	int rc = smscore_set_device_mode(
			coredev, smscore_registry_getmode(coredev->devpath));
	if (rc < 0) {
		sms_info("set device mode faile , rc %d", rc);
		return rc;
	}

	kmutex_lock(&g_smscore_deviceslock);

	rc = smscore_notify_callbacks(coredev, coredev->device, 1);

	sms_info("device %p started, rc %d", coredev, rc);

	kmutex_unlock(&g_smscore_deviceslock);

	return rc;
}

static int smscore_sendrequest_and_wait(struct smscore_device_t *coredev,
					void *buffer, size_t size,
					struct completion *completion)
{
	int rc = coredev->sendrequest_handler(coredev->context, buffer, size);
	if (rc < 0) {
		sms_info("sendrequest returned error %d", rc);
		return rc;
	}

	return wait_for_completion_timeout(completion,
					   msecs_to_jiffies(10000)) ?
						0 : -ETIME;
}

static int smscore_load_firmware_family2(struct smscore_device_t *coredev,
					 void *buffer, size_t size)
{
	struct SmsFirmware_ST *firmware = (struct SmsFirmware_ST *) buffer;
	struct SmsMsgHdr_ST *msg;
	u32 mem_address = firmware->StartAddress;
	u8 *payload = firmware->Payload;
	int rc = 0;

	sms_info("loading FW to addr 0x%x size %d",
		 mem_address, firmware->Length);
	if (coredev->preload_handler) {
		rc = coredev->preload_handler(coredev->context);
		if (rc < 0)
			return rc;
	}

	/* PAGE_SIZE buffer shall be enough and dma aligned */
	msg = kmalloc(PAGE_SIZE, GFP_KERNEL | GFP_DMA);
	if (!msg)
		return -ENOMEM;

	if (coredev->mode != DEVICE_MODE_NONE) {
		sms_debug("sending reload command.");
		SMS_INIT_MSG(msg, MSG_SW_RELOAD_START_REQ,
			     sizeof(struct SmsMsgHdr_ST));
		rc = smscore_sendrequest_and_wait(coredev, msg,
						  msg->msgLength,
						  &coredev->reload_start_done);
		mem_address = *(u32 *) &payload[20];
	}

	while (size && rc >= 0) {
		struct SmsDataDownload_ST *DataMsg =
			(struct SmsDataDownload_ST *) msg;
		int payload_size = min((int) size, SMS_MAX_PAYLOAD_SIZE);

		SMS_INIT_MSG(msg, MSG_SMS_DATA_DOWNLOAD_REQ,
			     (u16)(sizeof(struct SmsMsgHdr_ST) +
				      sizeof(u32) + payload_size));

		DataMsg->MemAddr = mem_address;
		memcpy(DataMsg->Payload, payload, payload_size);

		if ((coredev->device_flags & SMS_ROM_NO_RESPONSE) &&
		    (coredev->mode == DEVICE_MODE_NONE))
			rc = coredev->sendrequest_handler(
				coredev->context, DataMsg,
				DataMsg->xMsgHeader.msgLength);
		else
			rc = smscore_sendrequest_and_wait(
				coredev, DataMsg,
				DataMsg->xMsgHeader.msgLength,
				&coredev->data_download_done);

		payload += payload_size;
		size -= payload_size;
		mem_address += payload_size;
	}

	if (rc >= 0) {
		if (coredev->mode == DEVICE_MODE_NONE) {
			struct SmsMsgData_ST *TriggerMsg =
				(struct SmsMsgData_ST *) msg;

			SMS_INIT_MSG(msg, MSG_SMS_SWDOWNLOAD_TRIGGER_REQ,
				     sizeof(struct SmsMsgHdr_ST) +
				     sizeof(u32) * 5);

			TriggerMsg->msgData[0] = firmware->StartAddress;
						/* Entry point */
			TriggerMsg->msgData[1] = 5; /* Priority */
			TriggerMsg->msgData[2] = 0x200; /* Stack size */
			TriggerMsg->msgData[3] = 0; /* Parameter */
			TriggerMsg->msgData[4] = 4; /* Task ID */

			if (coredev->device_flags & SMS_ROM_NO_RESPONSE) {
				rc = coredev->sendrequest_handler(
					coredev->context, TriggerMsg,
					TriggerMsg->xMsgHeader.msgLength);
				msleep(100);
			} else
				rc = smscore_sendrequest_and_wait(
					coredev, TriggerMsg,
					TriggerMsg->xMsgHeader.msgLength,
					&coredev->trigger_done);
		} else {
			SMS_INIT_MSG(msg, MSG_SW_RELOAD_EXEC_REQ,
				     sizeof(struct SmsMsgHdr_ST));

			rc = coredev->sendrequest_handler(coredev->context,
							  msg, msg->msgLength);
		}
		msleep(500);
	}

	sms_debug("rc=%d, postload=%p ", rc,
		  coredev->postload_handler);

	kfree(msg);

	return ((rc >= 0) && coredev->postload_handler) ?
		coredev->postload_handler(coredev->context) :
		rc;
}

/**
 * loads specified firmware into a buffer and calls device loadfirmware_handler
 *
 * @param coredev pointer to a coredev object returned by
 *                smscore_register_device
 * @param filename null-terminated string specifies firmware file name
 * @param loadfirmware_handler device handler that loads firmware
 *
 * @return 0 on success, <0 on error.
 */
static int smscore_load_firmware_from_file(struct smscore_device_t *coredev,
					   char *filename,
					   loadfirmware_t loadfirmware_handler)
{
	int rc = -ENOENT;
	const struct firmware *fw;
	u8 *fw_buffer;

	if (loadfirmware_handler == NULL && !(coredev->device_flags &
					      SMS_DEVICE_FAMILY2))
		return -EINVAL;

	rc = request_firmware(&fw, filename, coredev->device);
	if (rc < 0) {
		sms_info("failed to open \"%s\"", filename);
		return rc;
	}
	sms_info("read FW %s, size=%zd", filename, fw->size);
	fw_buffer = kmalloc(ALIGN(fw->size, SMS_ALLOC_ALIGNMENT),
			    GFP_KERNEL | GFP_DMA);
	if (fw_buffer) {
		memcpy(fw_buffer, fw->data, fw->size);

		rc = (coredev->device_flags & SMS_DEVICE_FAMILY2) ?
		      smscore_load_firmware_family2(coredev,
						    fw_buffer,
						    fw->size) :
		      loadfirmware_handler(coredev->context,
					   fw_buffer, fw->size);

		kfree(fw_buffer);
	} else {
		sms_info("failed to allocate firmware buffer");
		rc = -ENOMEM;
	}

	release_firmware(fw);

	return rc;
}

/**
 * notifies all clients registered with the device, notifies hotplugs,
 * frees all buffers and coredev object
 *
 * @param coredev pointer to a coredev object returned by
 *                smscore_register_device
 *
 * @return 0 on success, <0 on error.
 */
void smscore_unregister_device(struct smscore_device_t *coredev)
{
	struct smscore_buffer_t *cb;
	int num_buffers = 0;
	int retry = 0;

	kmutex_lock(&g_smscore_deviceslock);

	smscore_notify_clients(coredev);
	smscore_notify_callbacks(coredev, NULL, 0);

	/* at this point all buffers should be back
	 * onresponse must no longer be called */

	while (1) {
		while ((cb = smscore_getbuffer(coredev))) {
			kfree(cb);
			num_buffers++;
		}
		if (num_buffers == coredev->num_buffers)
			break;
		if (++retry > 10) {
			sms_info("exiting although "
				 "not all buffers released.");
			break;
		}

		sms_info("waiting for %d buffer(s)",
			 coredev->num_buffers - num_buffers);
		msleep(100);
	}

	sms_info("freed %d buffers", num_buffers);

	if (coredev->common_buffer)
		dma_free_coherent(NULL, coredev->common_buffer_size,
				  coredev->common_buffer,
				  coredev->common_buffer_phys);

	list_del(&coredev->entry);
	kfree(coredev);

	kmutex_unlock(&g_smscore_deviceslock);

	sms_info("device %p destroyed", coredev);
}

static int smscore_detect_mode(struct smscore_device_t *coredev)
{
	void *buffer = kmalloc(sizeof(struct SmsMsgHdr_ST) + SMS_DMA_ALIGNMENT,
			       GFP_KERNEL | GFP_DMA);
	struct SmsMsgHdr_ST *msg =
		(struct SmsMsgHdr_ST *) SMS_ALIGN_ADDRESS(buffer);
	int rc;

	if (!buffer)
		return -ENOMEM;

	SMS_INIT_MSG(msg, MSG_SMS_GET_VERSION_EX_REQ,
		     sizeof(struct SmsMsgHdr_ST));

	rc = smscore_sendrequest_and_wait(coredev, msg, msg->msgLength,
					  &coredev->version_ex_done);
	if (rc == -ETIME) {
		sms_err("MSG_SMS_GET_VERSION_EX_REQ failed first try");

		if (wait_for_completion_timeout(&coredev->resume_done,
						msecs_to_jiffies(5000))) {
			rc = smscore_sendrequest_and_wait(
				coredev, msg, msg->msgLength,
				&coredev->version_ex_done);
			if (rc < 0)
				sms_err("MSG_SMS_GET_VERSION_EX_REQ failed "
					"second try, rc %d", rc);
		} else
			rc = -ETIME;
	}

	kfree(buffer);

	return rc;
}

static char *smscore_fw_lkup[][SMS_NUM_OF_DEVICE_TYPES] = {
	/*Stellar		NOVA A0		Nova B0		VEGA*/
	/*DVBT*/
	{"none", "dvb_nova_12mhz.inp", "dvb_nova_12mhz_b0.inp", "none"},
	/*DVBH*/
	{"none", "dvb_nova_12mhz.inp", "dvb_nova_12mhz_b0.inp", "none"},
	/*TDMB*/
	{"none", "tdmb_nova_12mhz.inp", "none", "none"},
	/*DABIP*/
	{"none", "none", "none", "none"},
	/*BDA*/
	{"none", "dvb_nova_12mhz.inp", "dvb_nova_12mhz_b0.inp", "none"},
	/*ISDBT*/
	{"none", "isdbt_nova_12mhz.inp", "dvb_nova_12mhz.inp", "none"},
	/*ISDBTBDA*/
	{"none", "isdbt_nova_12mhz.inp", "isdbt_nova_12mhz_b0.inp", "none"},
	/*CMMB*/
	{"none", "none", "none", "cmmb_vega_12mhz.inp"}
};

static inline char *sms_get_fw_name(struct smscore_device_t *coredev,
				    int mode, enum sms_device_type_st type)
{
	char **fw = sms_get_board(smscore_get_board_id(coredev))->fw;
	return (fw && fw[mode]) ? fw[mode] : smscore_fw_lkup[mode][type];
}

/**
 * calls device handler to change mode of operation
 * NOTE: stellar/usb may disconnect when changing mode
 *
 * @param coredev pointer to a coredev object returned by
 *                smscore_register_device
 * @param mode requested mode of operation
 *
 * @return 0 on success, <0 on error.
 */
int smscore_set_device_mode(struct smscore_device_t *coredev, int mode)
{
	void *buffer;
	int rc = 0;
	enum sms_device_type_st type;

	sms_debug("set device mode to %d", mode);
	if (coredev->device_flags & SMS_DEVICE_FAMILY2) {
		if (mode < DEVICE_MODE_DVBT || mode > DEVICE_MODE_RAW_TUNER) {
			sms_err("invalid mode specified %d", mode);
			return -EINVAL;
		}

		smscore_registry_setmode(coredev->devpath, mode);

		if (!(coredev->device_flags & SMS_DEVICE_NOT_READY)) {
			rc = smscore_detect_mode(coredev);
			if (rc < 0) {
				sms_err("mode detect failed %d", rc);
				return rc;
			}
		}

		if (coredev->mode == mode) {
			sms_info("device mode %d already set", mode);
			return 0;
		}

		if (!(coredev->modes_supported & (1 << mode))) {
			char *fw_filename;

			type = smscore_registry_gettype(coredev->devpath);
			fw_filename = sms_get_fw_name(coredev, mode, type);

			rc = smscore_load_firmware_from_file(coredev,
							     fw_filename, NULL);
			if (rc < 0) {
				sms_warn("error %d loading firmware: %s, "
					 "trying again with default firmware",
					 rc, fw_filename);

				/* try again with the default firmware */
				fw_filename = smscore_fw_lkup[mode][type];
				rc = smscore_load_firmware_from_file(coredev,
							     fw_filename, NULL);

				if (rc < 0) {
					sms_warn("error %d loading "
						 "firmware: %s", rc,
						 fw_filename);
					return rc;
				}
			}
			sms_log("firmware download success: %s", fw_filename);
		} else
			sms_info("mode %d supported by running "
				 "firmware", mode);

		buffer = kmalloc(sizeof(struct SmsMsgData_ST) +
				 SMS_DMA_ALIGNMENT, GFP_KERNEL | GFP_DMA);
		if (buffer) {
			struct SmsMsgData_ST *msg =
				(struct SmsMsgData_ST *)
					SMS_ALIGN_ADDRESS(buffer);

			SMS_INIT_MSG(&msg->xMsgHeader, MSG_SMS_INIT_DEVICE_REQ,
				     sizeof(struct SmsMsgData_ST));
			msg->msgData[0] = mode;

			rc = smscore_sendrequest_and_wait(
				coredev, msg, msg->xMsgHeader.msgLength,
				&coredev->init_device_done);

			kfree(buffer);
		} else {
			sms_err("Could not allocate buffer for "
				"init device message.");
			rc = -ENOMEM;
		}
	} else {
		if (mode < DEVICE_MODE_DVBT || mode > DEVICE_MODE_DVBT_BDA) {
			sms_err("invalid mode specified %d", mode);
			return -EINVAL;
		}

		smscore_registry_setmode(coredev->devpath, mode);

		if (coredev->detectmode_handler)
			coredev->detectmode_handler(coredev->context,
						    &coredev->mode);

		if (coredev->mode != mode && coredev->setmode_handler)
			rc = coredev->setmode_handler(coredev->context, mode);
	}

	if (rc >= 0) {
		coredev->mode = mode;
		coredev->device_flags &= ~SMS_DEVICE_NOT_READY;
	}

	if (rc != 0)
		sms_err("return error code %d.", rc);
	return rc;
}

/**
 * calls device handler to get current mode of operation
 *
 * @param coredev pointer to a coredev object returned by
 *                smscore_register_device
 *
 * @return current mode
 */
int smscore_get_device_mode(struct smscore_device_t *coredev)
{
	return coredev->mode;
}

/**
 * find client by response id & type within the clients list.
 * return client handle or NULL.
 *
 * @param coredev pointer to a coredev object returned by
 *                smscore_register_device
 * @param data_type client data type (SMS_DONT_CARE for all types)
 * @param id client id (SMS_DONT_CARE for all id)
 *
 */
static struct
smscore_client_t *smscore_find_client(struct smscore_device_t *coredev,
				      int data_type, int id)
{
	struct smscore_client_t *client = NULL;
	struct list_head *next, *first;
	unsigned long flags;
	struct list_head *firstid, *nextid;


	spin_lock_irqsave(&coredev->clientslock, flags);
	first = &coredev->clients;
	for (next = first->next;
	     (next != first) && !client;
	     next = next->next) {
		firstid = &((struct smscore_client_t *)next)->idlist;
		for (nextid = firstid->next;
		     nextid != firstid;
		     nextid = nextid->next) {
			if ((((struct smscore_idlist_t *)nextid)->id == id) &&
			    (((struct smscore_idlist_t *)nextid)->data_type == data_type ||
			    (((struct smscore_idlist_t *)nextid)->data_type == 0))) {
				client = (struct smscore_client_t *) next;
				break;
			}
		}
	}
	spin_unlock_irqrestore(&coredev->clientslock, flags);
	return client;
}

/**
 * find client by response id/type, call clients onresponse handler
 * return buffer to pool on error
 *
 * @param coredev pointer to a coredev object returned by
 *                smscore_register_device
 * @param cb pointer to response buffer descriptor
 *
 */
void smscore_onresponse(struct smscore_device_t *coredev,
			struct smscore_buffer_t *cb)
{
	struct SmsMsgHdr_ST *phdr =
		(struct SmsMsgHdr_ST *)((u8 *) cb->p + cb->offset);
	struct smscore_client_t *client =
		smscore_find_client(coredev, phdr->msgType, phdr->msgDstId);
	int rc = -EBUSY;

	static unsigned long last_sample_time; /* = 0; */
	static int data_total; /* = 0; */
	unsigned long time_now = jiffies_to_msecs(jiffies);

	if (!last_sample_time)
		last_sample_time = time_now;

	if (time_now - last_sample_time > 10000) {
		sms_debug("\ndata rate %d bytes/secs",
			  (int)((data_total * 1000) /
				(time_now - last_sample_time)));

		last_sample_time = time_now;
		data_total = 0;
	}

	data_total += cb->size;
	/* If no client registered for type & id,
	 * check for control client where type is not registered */
	if (client)
		rc = client->onresponse_handler(client->context, cb);

	if (rc < 0) {
		switch (phdr->msgType) {
		case MSG_SMS_GET_VERSION_EX_RES:
		{
			struct SmsVersionRes_ST *ver =
				(struct SmsVersionRes_ST *) phdr;
			sms_debug("MSG_SMS_GET_VERSION_EX_RES "
				  "id %d prots 0x%x ver %d.%d",
				  ver->FirmwareId, ver->SupportedProtocols,
				  ver->RomVersionMajor, ver->RomVersionMinor);

			coredev->mode = ver->FirmwareId == 255 ?
				DEVICE_MODE_NONE : ver->FirmwareId;
			coredev->modes_supported = ver->SupportedProtocols;

			complete(&coredev->version_ex_done);
			break;
		}
		case MSG_SMS_INIT_DEVICE_RES:
			sms_debug("MSG_SMS_INIT_DEVICE_RES");
			complete(&coredev->init_device_done);
			break;
		case MSG_SW_RELOAD_START_RES:
			sms_debug("MSG_SW_RELOAD_START_RES");
			complete(&coredev->reload_start_done);
			break;
		case MSG_SMS_DATA_DOWNLOAD_RES:
			complete(&coredev->data_download_done);
			break;
		case MSG_SW_RELOAD_EXEC_RES:
			sms_debug("MSG_SW_RELOAD_EXEC_RES");
			break;
		case MSG_SMS_SWDOWNLOAD_TRIGGER_RES:
			sms_debug("MSG_SMS_SWDOWNLOAD_TRIGGER_RES");
			complete(&coredev->trigger_done);
			break;
		case MSG_SMS_SLEEP_RESUME_COMP_IND:
			complete(&coredev->resume_done);
			break;
		default:
			break;
		}
		smscore_putbuffer(coredev, cb);
	}
}

/**
 * return pointer to next free buffer descriptor from core pool
 *
 * @param coredev pointer to a coredev object returned by
 *                smscore_register_device
 *
 * @return pointer to descriptor on success, NULL on error.
 */
struct smscore_buffer_t *smscore_getbuffer(struct smscore_device_t *coredev)
{
	struct smscore_buffer_t *cb = NULL;
	unsigned long flags;

	spin_lock_irqsave(&coredev->bufferslock, flags);

	if (!list_empty(&coredev->buffers)) {
		cb = (struct smscore_buffer_t *) coredev->buffers.next;
		list_del(&cb->entry);
	}

	spin_unlock_irqrestore(&coredev->bufferslock, flags);

	return cb;
}

/**
 * return buffer descriptor to a pool
 *
 * @param coredev pointer to a coredev object returned by
 *                smscore_register_device
 * @param cb pointer buffer descriptor
 *
 */
void smscore_putbuffer(struct smscore_device_t *coredev,
		       struct smscore_buffer_t *cb)
{
	list_add_locked(&cb->entry, &coredev->buffers, &coredev->bufferslock);
}

static int smscore_validate_client(struct smscore_device_t *coredev,
				   struct smscore_client_t *client,
				   int data_type, int id)
{
	struct smscore_idlist_t *listentry;
	struct smscore_client_t *registered_client;

	if (!client) {
		sms_err("bad parameter.");
		return -EFAULT;
	}
	registered_client = smscore_find_client(coredev, data_type, id);
	if (registered_client == client)
		return 0;

	if (registered_client) {
		sms_err("The msg ID already registered to another client.");
		return -EEXIST;
	}
	listentry = kzalloc(sizeof(struct smscore_idlist_t), GFP_KERNEL);
	if (!listentry) {
		sms_err("Can't allocate memory for client id.");
		return -ENOMEM;
	}
	listentry->id = id;
	listentry->data_type = data_type;
	list_add_locked(&listentry->entry, &client->idlist,
			&coredev->clientslock);
	return 0;
}

/**
 * creates smsclient object, check that id is taken by another client
 *
 * @param coredev pointer to a coredev object from clients hotplug
 * @param initial_id all messages with this id would be sent to this client
 * @param data_type all messages of this type would be sent to this client
 * @param onresponse_handler client handler that is called to
 *                           process incoming messages
 * @param onremove_handler client handler that is called when device is removed
 * @param context client-specific context
 * @param client pointer to a value that receives created smsclient object
 *
 * @return 0 on success, <0 on error.
 */
int smscore_register_client(struct smscore_device_t *coredev,
			    struct smsclient_params_t *params,
			    struct smscore_client_t **client)
{
	struct smscore_client_t *newclient;
	/* check that no other channel with same parameters exists */
	if (smscore_find_client(coredev, params->data_type,
				params->initial_id)) {
		sms_err("Client already exist.");
		return -EEXIST;
	}

	newclient = kzalloc(sizeof(struct smscore_client_t), GFP_KERNEL);
	if (!newclient) {
		sms_err("Failed to allocate memory for client.");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&newclient->idlist);
	newclient->coredev = coredev;
	newclient->onresponse_handler = params->onresponse_handler;
	newclient->onremove_handler = params->onremove_handler;
	newclient->context = params->context;
	list_add_locked(&newclient->entry, &coredev->clients,
			&coredev->clientslock);
	smscore_validate_client(coredev, newclient, params->data_type,
				params->initial_id);
	*client = newclient;
	sms_debug("%p %d %d", params->context, params->data_type,
		  params->initial_id);

	return 0;
}

/**
 * frees smsclient object and all subclients associated with it
 *
 * @param client pointer to smsclient object returned by
 *               smscore_register_client
 *
 */
void smscore_unregister_client(struct smscore_client_t *client)
{
	struct smscore_device_t *coredev = client->coredev;
	unsigned long flags;

	spin_lock_irqsave(&coredev->clientslock, flags);


	while (!list_empty(&client->idlist)) {
		struct smscore_idlist_t *identry =
			(struct smscore_idlist_t *) client->idlist.next;
		list_del(&identry->entry);
		kfree(identry);
	}

	sms_info("%p", client->context);

	list_del(&client->entry);
	kfree(client);

	spin_unlock_irqrestore(&coredev->clientslock, flags);
}

/**
 * verifies that source id is not taken by another client,
 * calls device handler to send requests to the device
 *
 * @param client pointer to smsclient object returned by
 *               smscore_register_client
 * @param buffer pointer to a request buffer
 * @param size size (in bytes) of request buffer
 *
 * @return 0 on success, <0 on error.
 */
int smsclient_sendrequest(struct smscore_client_t *client,
			  void *buffer, size_t size)
{
	struct smscore_device_t *coredev;
	struct SmsMsgHdr_ST *phdr = (struct SmsMsgHdr_ST *) buffer;
	int rc;

	if (client == NULL) {
		sms_err("Got NULL client");
		return -EINVAL;
	}

	coredev = client->coredev;

	/* check that no other channel with same id exists */
	if (coredev == NULL) {
		sms_err("Got NULL coredev");
		return -EINVAL;
	}

	rc = smscore_validate_client(client->coredev, client, 0,
				     phdr->msgSrcId);
	if (rc < 0)
		return rc;

	return coredev->sendrequest_handler(coredev->context, buffer, size);
}


static int __init smscore_module_init(void)
{
	int rc = 0;

	INIT_LIST_HEAD(&g_smscore_notifyees);
	INIT_LIST_HEAD(&g_smscore_devices);
	kmutex_init(&g_smscore_deviceslock);

	INIT_LIST_HEAD(&g_smscore_registry);
	kmutex_init(&g_smscore_registrylock);

	/* USB Register */
	rc = smsusb_register();

	/* DVB Register */
	rc = smsdvb_register();

	sms_debug("rc %d", rc);

	return rc;
}

static void __exit smscore_module_exit(void)
{

	kmutex_lock(&g_smscore_deviceslock);
	while (!list_empty(&g_smscore_notifyees)) {
		struct smscore_device_notifyee_t *notifyee =
			(struct smscore_device_notifyee_t *)
				g_smscore_notifyees.next;

		list_del(&notifyee->entry);
		kfree(notifyee);
	}
	kmutex_unlock(&g_smscore_deviceslock);

	kmutex_lock(&g_smscore_registrylock);
	while (!list_empty(&g_smscore_registry)) {
		struct smscore_registry_entry_t *entry =
			(struct smscore_registry_entry_t *)
				g_smscore_registry.next;

		list_del(&entry->entry);
		kfree(entry);
	}
	kmutex_unlock(&g_smscore_registrylock);

	/* DVB UnRegister */
	smsdvb_unregister();

	/* Unregister USB */
	smsusb_unregister();

	sms_debug("");
}

module_init(smscore_module_init);
module_exit(smscore_module_exit);

MODULE_DESCRIPTION("Driver for the Siano SMS1XXX USB dongle");
MODULE_AUTHOR("Siano Mobile Silicon,,, (doronc@siano-ms.com)");
MODULE_LICENSE("GPL");
