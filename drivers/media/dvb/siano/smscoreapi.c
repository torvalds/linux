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
 *  it under the terms of the GNU General Public License version 3 as
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
#include <asm/io.h>

#include <linux/firmware.h>

#include "smscoreapi.h"

#define PERROR(fmt, args...)\
	printk(KERN_ERR "smscore error: line %d- %s(): " fmt, \
		__LINE__,  __func__, ## args)

#ifdef SMSCORE_DEBUG
#undef PWARNING
#  define PWARNING(fmt, args...) printk(KERN_INFO "smscore warning: " \
					"line %d- %s(): " fmt, \
					__LINE__, __func__, ## args)
#undef PDEBUG					/* undef it, just in case */
#  define PDEBUG(fmt, args...)   printk(KERN_INFO "smscore - %s(): " fmt, \
					__func__, ## args)
#else /*SMSCORE_DEBUG*/
#define PDEBUG(fmt, args...)
#define PWARNING(fmt, args...)
#endif

typedef struct _smscore_device_notifyee
{
	struct list_head entry;
	hotplug_t hotplug;
} smscore_device_notifyee_t;

typedef struct _smscore_subclient
{
	struct list_head entry;
	int		id;
	int		data_type;
} smscore_idlist_t;

typedef struct _smscore_client
{
	struct list_head entry;
	smscore_device_t *coredev;
	void			*context;
	struct list_head 	idlist;
	onresponse_t	onresponse_handler;
	onremove_t		onremove_handler;
} *psmscore_client_t;



typedef struct _smscore_device
{
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
} *psmscore_device_t;

typedef struct _smscore_registry_entry
{
	struct list_head entry;
	char			devpath[32];
	int				mode;
	sms_device_type_st	type;
} smscore_registry_entry_t;

struct list_head g_smscore_notifyees;
struct list_head g_smscore_devices;
kmutex_t g_smscore_deviceslock;

struct list_head g_smscore_registry;
kmutex_t g_smscore_registrylock;

static int default_mode = 1;

module_param(default_mode, int, 0644);
MODULE_PARM_DESC(default_mode, "default firmware id (device mode)");

static smscore_registry_entry_t *smscore_find_registry(char *devpath)
{
	smscore_registry_entry_t *entry;
	struct list_head *next;

	kmutex_lock(&g_smscore_registrylock);
	for (next = g_smscore_registry.next;
	     next != &g_smscore_registry;
	     next = next->next) {
		entry = (smscore_registry_entry_t *) next;
		if (!strcmp(entry->devpath, devpath)) {
			kmutex_unlock(&g_smscore_registrylock);
			return entry;
		}
	}
	entry = (smscore_registry_entry_t *)
			kmalloc(sizeof(smscore_registry_entry_t), GFP_KERNEL);
	if (entry) {
		entry->mode = default_mode;
		strcpy(entry->devpath, devpath);
		list_add(&entry->entry, &g_smscore_registry);
	} else
		printk(KERN_ERR "%s failed to create smscore_registry.\n",
		       __func__);
	kmutex_unlock(&g_smscore_registrylock);
	return entry;
}

int smscore_registry_getmode(char *devpath)
{
	smscore_registry_entry_t *entry;

	entry = smscore_find_registry(devpath);
	if (entry)
		return entry->mode;
	else
		printk(KERN_ERR "%s No registry found.\n", __func__);

	return default_mode;
}

sms_device_type_st smscore_registry_gettype(char *devpath)
{
	smscore_registry_entry_t *entry;

	entry = smscore_find_registry(devpath);
	if (entry)
		return entry->type;
	else
		printk(KERN_ERR "%s No registry found.\n", __func__);

	return -1;
}

void smscore_registry_setmode(char *devpath, int mode)
{
	smscore_registry_entry_t *entry;

	entry = smscore_find_registry(devpath);
	if (entry)
		entry->mode = mode;
	else
		printk(KERN_ERR "%s No registry found.\n", __func__);
}

void smscore_registry_settype(char *devpath, sms_device_type_st type)
{
	smscore_registry_entry_t *entry;

	entry = smscore_find_registry(devpath);
	if (entry)
		entry->type = type;
	else
		printk(KERN_ERR "%s No registry found.\n", __func__);
}



void list_add_locked(struct list_head *new, struct list_head *head,
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
	smscore_device_notifyee_t *notifyee;
	struct list_head *next, *first;
	int rc = 0;

	kmutex_lock(&g_smscore_deviceslock);

	notifyee = kmalloc(sizeof(smscore_device_notifyee_t), GFP_KERNEL);
	if (notifyee) {
		/* now notify callback about existing devices */
		first = &g_smscore_devices;
		for (next = first->next;
		     next != first && !rc;
		     next = next->next) {
			smscore_device_t *coredev = (smscore_device_t *) next;
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
		smscore_device_notifyee_t *notifyee =
			(smscore_device_notifyee_t *) next;
		next = next->next;

		if (notifyee->hotplug == hotplug) {
			list_del(&notifyee->entry);
			kfree(notifyee);
		}
	}

	kmutex_unlock(&g_smscore_deviceslock);
}

void smscore_notify_clients(smscore_device_t *coredev)
{
	smscore_client_t *client;

	/* the client must call smscore_unregister_client from remove handler */
	while (!list_empty(&coredev->clients)) {
		client = (smscore_client_t *) coredev->clients.next;
		client->onremove_handler(client->context);
	}
}

int smscore_notify_callbacks(smscore_device_t *coredev, struct device *device,
			     int arrival)
{
	struct list_head *next, *first;
	int rc = 0;

	/* note: must be called under g_deviceslock */

	first = &g_smscore_notifyees;

	for (next = first->next; next != first; next = next->next) {
		rc = ((smscore_device_notifyee_t *) next)->
				hotplug(coredev, device, arrival);
		if (rc < 0)
			break;
	}

	return rc;
}

smscore_buffer_t *smscore_createbuffer(u8 *buffer, void *common_buffer,
				       dma_addr_t common_buffer_phys)
{
	smscore_buffer_t *cb = kmalloc(sizeof(smscore_buffer_t), GFP_KERNEL);
	if (!cb) {
		printk(KERN_INFO "%s kmalloc(...) failed\n", __func__);
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
int smscore_register_device(smsdevice_params_t *params,
			    smscore_device_t **coredev)
{
	smscore_device_t *dev;
	u8 *buffer;

	dev = kzalloc(sizeof(smscore_device_t), GFP_KERNEL);
	if (!dev) {
		printk(KERN_INFO "%s kzalloc(...) failed\n", __func__);
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
		smscore_buffer_t *cb =
			smscore_createbuffer(buffer, dev->common_buffer,
					     dev->common_buffer_phys);
		if (!cb) {
			smscore_unregister_device(dev);
			return -ENOMEM;
		}

		smscore_putbuffer(dev, cb);
	}

	printk(KERN_INFO "%s allocated %d buffers\n",
	       __func__, dev->num_buffers);

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

	printk(KERN_INFO "%s device %p created\n", __func__, dev);

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
int smscore_start_device(smscore_device_t *coredev)
{
	int rc = smscore_set_device_mode(
			coredev, smscore_registry_getmode(coredev->devpath));
	if (rc < 0) {
		printk(KERN_INFO "%s set device mode faile , rc %d\n",
		       __func__, rc);
		return rc;
	}

	kmutex_lock(&g_smscore_deviceslock);

	rc = smscore_notify_callbacks(coredev, coredev->device, 1);

	printk(KERN_INFO "%s device %p started, rc %d\n",
	       __func__, coredev, rc);

	kmutex_unlock(&g_smscore_deviceslock);

	return rc;
}

int smscore_sendrequest_and_wait(smscore_device_t *coredev, void *buffer,
				 size_t size, struct completion *completion)
{
	int rc = coredev->sendrequest_handler(coredev->context, buffer, size);
	if (rc < 0) {
		printk(KERN_INFO "%s sendrequest returned error %d\n",
		       __func__, rc);
		return rc;
	}

	return wait_for_completion_timeout(completion,
					   msecs_to_jiffies(10000)) ?
						0 : -ETIME;
}

int smscore_load_firmware_family2(smscore_device_t *coredev, void *buffer,
				  size_t size)
{
	SmsFirmware_ST *firmware = (SmsFirmware_ST *) buffer;
	SmsMsgHdr_ST *msg;
	u32 mem_address = firmware->StartAddress;
	u8 *payload = firmware->Payload;
	int rc = 0;

	printk(KERN_INFO "%s loading FW to addr 0x%x size %d\n",
	       __func__, mem_address, firmware->Length);
	if (coredev->preload_handler) {
		rc = coredev->preload_handler(coredev->context);
		if (rc < 0)
			return rc;
	}

	/* PAGE_SIZE buffer shall be enough and dma aligned */
	msg = (SmsMsgHdr_ST *) kmalloc(PAGE_SIZE, GFP_KERNEL | GFP_DMA);
	if (!msg)
		return -ENOMEM;

	if (coredev->mode != DEVICE_MODE_NONE) {
		PDEBUG("Sending reload command\n");
		SMS_INIT_MSG(msg, MSG_SW_RELOAD_START_REQ,
			     sizeof(SmsMsgHdr_ST));
		rc = smscore_sendrequest_and_wait(coredev, msg,
						  msg->msgLength,
						  &coredev->reload_start_done);
		mem_address = *(u32 *) &payload[20];
	}

	while (size && rc >= 0) {
		SmsDataDownload_ST *DataMsg = (SmsDataDownload_ST *) msg;
		int payload_size = min((int) size, SMS_MAX_PAYLOAD_SIZE);

		SMS_INIT_MSG(msg, MSG_SMS_DATA_DOWNLOAD_REQ,
			     (u16)(sizeof(SmsMsgHdr_ST) +
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
			SmsMsgData_ST *TriggerMsg = (SmsMsgData_ST *) msg;

			SMS_INIT_MSG(msg, MSG_SMS_SWDOWNLOAD_TRIGGER_REQ,
				     sizeof(SmsMsgHdr_ST) +
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
				     sizeof(SmsMsgHdr_ST));

			rc = coredev->sendrequest_handler(coredev->context,
							  msg, msg->msgLength);
		}
		msleep(500);
	}

	printk(KERN_DEBUG "%s rc=%d, postload=%p \n", __func__, rc,
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
int smscore_load_firmware_from_file(smscore_device_t *coredev, char *filename,
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
		printk(KERN_INFO "%s failed to open \"%s\"\n",
		       __func__, filename);
		return rc;
	}
	printk(KERN_INFO "%s read FW %s, size=%d\"\n", __func__,
	       filename, fw->size);
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
		printk(KERN_INFO "%s failed to allocate firmware buffer\n",
		       __func__);
		rc = -ENOMEM;
	}

	release_firmware(fw);

	return rc;
}

int smscore_load_firmware_from_buffer(smscore_device_t *coredev, u8 *buffer,
				      int size, int new_mode)
{
	PERROR("Feature not implemented yet\n");
	return -EFAULT;
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
void smscore_unregister_device(smscore_device_t *coredev)
{
	smscore_buffer_t *cb;
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
			printk(KERN_INFO "%s exiting although "
			       "not all buffers released.\n", __func__);
			break;
		}

		printk(KERN_INFO "%s waiting for %d buffer(s)\n", __func__,
		       coredev->num_buffers - num_buffers);
		msleep(100);
	}

	printk(KERN_INFO "%s freed %d buffers\n", __func__, num_buffers);

	if (coredev->common_buffer)
		dma_free_coherent(NULL, coredev->common_buffer_size,
				  coredev->common_buffer,
				  coredev->common_buffer_phys);

	list_del(&coredev->entry);
	kfree(coredev);

	kmutex_unlock(&g_smscore_deviceslock);

	printk(KERN_INFO "%s device %p destroyed\n", __func__, coredev);
}

int smscore_detect_mode(smscore_device_t *coredev)
{
	void *buffer = kmalloc(sizeof(SmsMsgHdr_ST) + SMS_DMA_ALIGNMENT,
			       GFP_KERNEL | GFP_DMA);
	SmsMsgHdr_ST *msg = (SmsMsgHdr_ST *) SMS_ALIGN_ADDRESS(buffer);
	int rc;

	if (!buffer)
		return -ENOMEM;

	SMS_INIT_MSG(msg, MSG_SMS_GET_VERSION_EX_REQ, sizeof(SmsMsgHdr_ST));

	rc = smscore_sendrequest_and_wait(coredev, msg, msg->msgLength,
					  &coredev->version_ex_done);
	if (rc == -ETIME) {
		printk(KERN_ERR "%s: MSG_SMS_GET_VERSION_EX_REQ "
		       "failed first try\n", __func__);

		if (wait_for_completion_timeout(&coredev->resume_done,
						msecs_to_jiffies(5000))) {
			rc = smscore_sendrequest_and_wait(
				coredev, msg, msg->msgLength,
				&coredev->version_ex_done);
			if (rc < 0)
				printk(KERN_ERR "%s: "
				       "MSG_SMS_GET_VERSION_EX_REQ failed "
				       "second try, rc %d\n", __func__, rc);
		} else
			rc = -ETIME;
	}

	kfree(buffer);

	return rc;
}

char *smscore_fw_lkup[][SMS_NUM_OF_DEVICE_TYPES] = {
	/*Stellar			NOVA A0			Nova B0				VEGA*/
	/*DVBT*/  {"none", "dvb_nova_12mhz.inp", "dvb_nova_12mhz_b0.inp", "none"},
	/*DVBH*/  {"none", "dvb_nova_12mhz.inp", "dvb_nova_12mhz_b0.inp", "none"},
	/*TDMB*/  {"none", "tdmb_nova_12mhz.inp", "none", "none"},
	/*DABIP*/ {"none", "none", "none", "none"},
	/*BDA*/	  {"none", "dvb_nova_12mhz.inp", "dvb_nova_12mhz_b0.inp", "none"},
	/*ISDBT*/ {"none", "isdbt_nova_12mhz.inp", "dvb_nova_12mhz.inp", "none"},
	/*ISDBTBDA*/{"none", "isdbt_nova_12mhz.inp", "isdbt_nova_12mhz_b0.inp", "none"},
	/*CMMB*/  {"none", "none", "none", "cmmb_vega_12mhz.inp"}
};


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
int smscore_set_device_mode(smscore_device_t *coredev, int mode)
{
	void *buffer;
	int rc = 0;
	sms_device_type_st type;

	PDEBUG("set device mode to %d\n", mode);
	if (coredev->device_flags & SMS_DEVICE_FAMILY2) {
		if (mode < DEVICE_MODE_DVBT || mode > DEVICE_MODE_RAW_TUNER) {
			printk(KERN_INFO "%s invalid mode specified %d\n",
			       __func__, mode);
			return -EINVAL;
		}

		smscore_registry_setmode(coredev->devpath, mode);

		if (!(coredev->device_flags & SMS_DEVICE_NOT_READY)) {
			rc = smscore_detect_mode(coredev);
			if (rc < 0) {
				printk(KERN_INFO "%s mode detect failed %d\n",
				       __func__, rc);
				return rc;
			}
		}

		if (coredev->mode == mode) {
			printk(KERN_INFO "%s device mode %d already set\n",
			       __func__, mode);
			return 0;
		}

		if (!(coredev->modes_supported & (1 << mode))) {
			type = smscore_registry_gettype(coredev->devpath);
			rc = smscore_load_firmware_from_file(
				coredev, smscore_fw_lkup[mode][type], NULL);
			if (rc < 0) {
				printk(KERN_INFO "%s load firmware "
				       "failed %d\n", __func__, rc);
				return rc;
			}
		} else
			printk(KERN_INFO "%s mode %d supported by running "
			       "firmware\n", __func__, mode);

		buffer = kmalloc(sizeof(SmsMsgData_ST) + SMS_DMA_ALIGNMENT,
				 GFP_KERNEL | GFP_DMA);
		if (buffer) {
			SmsMsgData_ST *msg =
				(SmsMsgData_ST *) SMS_ALIGN_ADDRESS(buffer);

			SMS_INIT_MSG(&msg->xMsgHeader, MSG_SMS_INIT_DEVICE_REQ,
				     sizeof(SmsMsgData_ST));
			msg->msgData[0] = mode;

			rc = smscore_sendrequest_and_wait(
				coredev, msg, msg->xMsgHeader.msgLength,
				&coredev->init_device_done);

			kfree(buffer);
		} else {
			printk(KERN_INFO "%s Could not allocate buffer for "
			       "init device message.\n", __func__);
			rc = -ENOMEM;
		}
	} else {
		if (mode < DEVICE_MODE_DVBT || mode > DEVICE_MODE_DVBT_BDA) {
			printk(KERN_INFO "%s invalid mode specified %d\n",
			       __func__, mode);
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
		printk(KERN_INFO "%s return error code %d.\n", __func__, rc);
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
int smscore_get_device_mode(smscore_device_t *coredev)
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
smscore_client_t *smscore_find_client(smscore_device_t *coredev,
				      int data_type, int id)
{
	smscore_client_t *client = NULL;
	struct list_head *next, *first;
	unsigned long flags;
	struct list_head *firstid, *nextid;


	spin_lock_irqsave(&coredev->clientslock, flags);
	first = &coredev->clients;
	for (next = first->next;
	     (next != first) && !client;
	     next = next->next) {
		firstid = &((smscore_client_t *)next)->idlist;
		for (nextid = firstid->next;
		     nextid != firstid;
		     nextid = nextid->next) {
			if ((((smscore_idlist_t *)nextid)->id  == id) &&
			    (((smscore_idlist_t *)nextid)->data_type  == data_type ||
			    (((smscore_idlist_t *)nextid)->data_type  == 0))) {
				client = (smscore_client_t *) next;
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
void smscore_onresponse(smscore_device_t *coredev, smscore_buffer_t *cb)
{
	SmsMsgHdr_ST *phdr = (SmsMsgHdr_ST *)((u8 *) cb->p + cb->offset);
	smscore_client_t *client =
		smscore_find_client(coredev, phdr->msgType, phdr->msgDstId);
	int rc = -EBUSY;

	static unsigned long last_sample_time = 0;
	static int data_total = 0;
	unsigned long time_now = jiffies_to_msecs(jiffies);

	if (!last_sample_time)
		last_sample_time = time_now;

	if (time_now - last_sample_time > 10000) {
		printk(KERN_DEBUG "\n%s data rate %d bytes/secs\n", __func__,
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
			SmsVersionRes_ST *ver = (SmsVersionRes_ST *) phdr;
			printk(KERN_DEBUG "%s: MSG_SMS_GET_VERSION_EX_RES "
			       "id %d prots 0x%x ver %d.%d\n", __func__,
			       ver->FirmwareId, ver->SupportedProtocols,
			       ver->RomVersionMajor, ver->RomVersionMinor);

			coredev->mode = ver->FirmwareId == 255 ?
				DEVICE_MODE_NONE : ver->FirmwareId;
			coredev->modes_supported = ver->SupportedProtocols;

			complete(&coredev->version_ex_done);
			break;
		}
		case MSG_SMS_INIT_DEVICE_RES:
			printk(KERN_DEBUG "%s: MSG_SMS_INIT_DEVICE_RES\n",
			       __func__);
			complete(&coredev->init_device_done);
			break;
		case MSG_SW_RELOAD_START_RES:
			printk(KERN_DEBUG "%s: MSG_SW_RELOAD_START_RES\n",
			       __func__);
			complete(&coredev->reload_start_done);
			break;
		case MSG_SMS_DATA_DOWNLOAD_RES:
			complete(&coredev->data_download_done);
			break;
		case MSG_SW_RELOAD_EXEC_RES:
			printk(KERN_DEBUG "%s: MSG_SW_RELOAD_EXEC_RES\n",
			       __func__);
			break;
		case MSG_SMS_SWDOWNLOAD_TRIGGER_RES:
			printk(KERN_DEBUG
			       "%s: MSG_SMS_SWDOWNLOAD_TRIGGER_RES\n",
			       __func__);
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
smscore_buffer_t *smscore_getbuffer(smscore_device_t *coredev)
{
	smscore_buffer_t *cb = NULL;
	unsigned long flags;

	spin_lock_irqsave(&coredev->bufferslock, flags);

	if (!list_empty(&coredev->buffers)) {
		cb = (smscore_buffer_t *) coredev->buffers.next;
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
void smscore_putbuffer(smscore_device_t *coredev, smscore_buffer_t *cb)
{
	list_add_locked(&cb->entry, &coredev->buffers, &coredev->bufferslock);
}

int smscore_validate_client(smscore_device_t *coredev,
			    smscore_client_t *client, int data_type, int id)
{
	smscore_idlist_t *listentry;
	smscore_client_t *registered_client;

	if (!client) {
		PERROR("bad parameter.\n");
		return -EFAULT;
	}
	registered_client = smscore_find_client(coredev, data_type, id);
	if (registered_client == client)
		return 0;

	if (registered_client) {
		PERROR("The msg ID already registered to another client.\n");
		return -EEXIST;
	}
	listentry = kzalloc(sizeof(smscore_idlist_t), GFP_KERNEL);
	if (!listentry) {
		PERROR("Can't allocate memory for client id.\n");
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
int smscore_register_client(smscore_device_t *coredev,
			    smsclient_params_t *params,
			    smscore_client_t **client)
{
	smscore_client_t *newclient;
	/* check that no other channel with same parameters exists */
	if (smscore_find_client(coredev, params->data_type,
				params->initial_id)) {
		PERROR("Client already exist.\n");
		return -EEXIST;
	}

	newclient = kzalloc(sizeof(smscore_client_t), GFP_KERNEL);
	if (!newclient) {
		PERROR("Failed to allocate memory for client.\n");
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
	PDEBUG("%p %d %d\n", params->context, params->data_type,
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
void smscore_unregister_client(smscore_client_t *client)
{
	smscore_device_t *coredev = client->coredev;
	unsigned long flags;

	spin_lock_irqsave(&coredev->clientslock, flags);


	while (!list_empty(&client->idlist)) {
		smscore_idlist_t *identry =
			(smscore_idlist_t *) client->idlist.next;
		list_del(&identry->entry);
		kfree(identry);
	}

	printk(KERN_INFO "%s %p\n", __func__, client->context);

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
int smsclient_sendrequest(smscore_client_t *client, void *buffer, size_t size)
{
	smscore_device_t *coredev;
	SmsMsgHdr_ST *phdr = (SmsMsgHdr_ST *) buffer;
	int rc;

	if (client == NULL) {
		printk(KERN_ERR "%s Got NULL client\n", __func__);
		return -EINVAL;
	}

	coredev = client->coredev;

	/* check that no other channel with same id exists */
	if (coredev == NULL) {
		printk(KERN_ERR "%s Got NULL coredev\n", __func__);
		return -EINVAL;
	}

	rc = smscore_validate_client(client->coredev, client, 0,
				     phdr->msgSrcId);
	if (rc < 0)
		return rc;

	return coredev->sendrequest_handler(coredev->context, buffer, size);
}

/**
 * return the size of large (common) buffer
 *
 * @param coredev pointer to a coredev object from clients hotplug
 *
 * @return size (in bytes) of the buffer
 */
int smscore_get_common_buffer_size(smscore_device_t *coredev)
{
	return coredev->common_buffer_size;
}

/**
 * maps common buffer (if supported by platform)
 *
 * @param coredev pointer to a coredev object from clients hotplug
 * @param vma pointer to vma struct from mmap handler
 *
 * @return 0 on success, <0 on error.
 */
int smscore_map_common_buffer(smscore_device_t *coredev,
			       struct vm_area_struct *vma)
{
	unsigned long end = vma->vm_end,
		      start = vma->vm_start,
		      size = PAGE_ALIGN(coredev->common_buffer_size);

	if (!(vma->vm_flags & (VM_READ | VM_SHARED)) ||
	     (vma->vm_flags & VM_WRITE)) {
		printk(KERN_INFO "%s invalid vm flags\n", __func__);
		return -EINVAL;
	}

	if ((end - start) != size) {
		printk(KERN_INFO "%s invalid size %d expected %d\n",
		       __func__, (int)(end - start), (int) size);
		return -EINVAL;
	}

	if (remap_pfn_range(vma, start,
			    coredev->common_buffer_phys >> PAGE_SHIFT,
			    size, pgprot_noncached(vma->vm_page_prot)))
	{
		printk(KERN_INFO "%s remap_page_range failed\n", __func__);
		return -EAGAIN;
	}

	return 0;
}

int smscore_module_init(void)
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

	printk(KERN_INFO "%s, rc %d\n", __func__, rc);

	return rc;
}

void smscore_module_exit(void)
{

	kmutex_lock(&g_smscore_deviceslock);
	while (!list_empty(&g_smscore_notifyees)) {
		smscore_device_notifyee_t *notifyee =
			(smscore_device_notifyee_t *) g_smscore_notifyees.next;

		list_del(&notifyee->entry);
		kfree(notifyee);
	}
	kmutex_unlock(&g_smscore_deviceslock);

	kmutex_lock(&g_smscore_registrylock);
	while (!list_empty(&g_smscore_registry)) {
		smscore_registry_entry_t *entry =
			(smscore_registry_entry_t *) g_smscore_registry.next;

		list_del(&entry->entry);
		kfree(entry);
	}
	kmutex_unlock(&g_smscore_registrylock);

	/* DVB UnRegister */
	smsdvb_unregister();

	/* Unregister USB */
	smsusb_unregister();

	printk(KERN_INFO "%s\n", __func__);
}

module_init(smscore_module_init);
module_exit(smscore_module_exit);

MODULE_DESCRIPTION("smscore");
MODULE_AUTHOR("Siano Mobile Silicon,,, (doronc@siano-ms.com)");
MODULE_LICENSE("GPL");
