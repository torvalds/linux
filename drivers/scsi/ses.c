/*
 * SCSI Enclosure Services
 *
 * Copyright (C) 2008 James Bottomley <James.Bottomley@HansenPartnership.com>
 *
**-----------------------------------------------------------------------------
**
**  This program is free software; you can redistribute it and/or
**  modify it under the terms of the GNU General Public License
**  version 2 as published by the Free Software Foundation.
**
**  This program is distributed in the hope that it will be useful,
**  but WITHOUT ANY WARRANTY; without even the implied warranty of
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
**  GNU General Public License for more details.
**
**  You should have received a copy of the GNU General Public License
**  along with this program; if not, write to the Free Software
**  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**-----------------------------------------------------------------------------
*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/enclosure.h>

#include <scsi/scsi.h>
#include <scsi/scsi_cmnd.h>
#include <scsi/scsi_dbg.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_driver.h>
#include <scsi/scsi_host.h>

struct ses_device {
	unsigned char *page1;
	unsigned char *page2;
	unsigned char *page10;
	short page1_len;
	short page2_len;
	short page10_len;
};

struct ses_component {
	u64 addr;
	unsigned char *desc;
};

static int ses_probe(struct device *dev)
{
	struct scsi_device *sdev = to_scsi_device(dev);
	int err = -ENODEV;

	if (sdev->type != TYPE_ENCLOSURE)
		goto out;

	err = 0;
	sdev_printk(KERN_NOTICE, sdev, "Attached Enclosure device\n");

 out:
	return err;
}

#define SES_TIMEOUT (30 * HZ)
#define SES_RETRIES 3

static int ses_recv_diag(struct scsi_device *sdev, int page_code,
			 void *buf, int bufflen)
{
	unsigned char cmd[] = {
		RECEIVE_DIAGNOSTIC,
		1,		/* Set PCV bit */
		page_code,
		bufflen >> 8,
		bufflen & 0xff,
		0
	};

	return scsi_execute_req(sdev, cmd, DMA_FROM_DEVICE, buf, bufflen,
				NULL, SES_TIMEOUT, SES_RETRIES);
}

static int ses_send_diag(struct scsi_device *sdev, int page_code,
			 void *buf, int bufflen)
{
	u32 result;

	unsigned char cmd[] = {
		SEND_DIAGNOSTIC,
		0x10,		/* Set PF bit */
		0,
		bufflen >> 8,
		bufflen & 0xff,
		0
	};

	result = scsi_execute_req(sdev, cmd, DMA_TO_DEVICE, buf, bufflen,
				  NULL, SES_TIMEOUT, SES_RETRIES);
	if (result)
		sdev_printk(KERN_ERR, sdev, "SEND DIAGNOSTIC result: %8x\n",
			    result);
	return result;
}

static int ses_set_page2_descriptor(struct enclosure_device *edev,
				      struct enclosure_component *ecomp,
				      unsigned char *desc)
{
	int i, j, count = 0, descriptor = ecomp->number;
	struct scsi_device *sdev = to_scsi_device(edev->edev.parent);
	struct ses_device *ses_dev = edev->scratch;
	unsigned char *type_ptr = ses_dev->page1 + 12 + ses_dev->page1[11];
	unsigned char *desc_ptr = ses_dev->page2 + 8;

	/* Clear everything */
	memset(desc_ptr, 0, ses_dev->page2_len - 8);
	for (i = 0; i < ses_dev->page1[10]; i++, type_ptr += 4) {
		for (j = 0; j < type_ptr[1]; j++) {
			desc_ptr += 4;
			if (type_ptr[0] != ENCLOSURE_COMPONENT_DEVICE &&
			    type_ptr[0] != ENCLOSURE_COMPONENT_ARRAY_DEVICE)
				continue;
			if (count++ == descriptor) {
				memcpy(desc_ptr, desc, 4);
				/* set select */
				desc_ptr[0] |= 0x80;
				/* clear reserved, just in case */
				desc_ptr[0] &= 0xf0;
			}
		}
	}

	return ses_send_diag(sdev, 2, ses_dev->page2, ses_dev->page2_len);
}

static unsigned char *ses_get_page2_descriptor(struct enclosure_device *edev,
				      struct enclosure_component *ecomp)
{
	int i, j, count = 0, descriptor = ecomp->number;
	struct scsi_device *sdev = to_scsi_device(edev->edev.parent);
	struct ses_device *ses_dev = edev->scratch;
	unsigned char *type_ptr = ses_dev->page1 + 12 + ses_dev->page1[11];
	unsigned char *desc_ptr = ses_dev->page2 + 8;

	ses_recv_diag(sdev, 2, ses_dev->page2, ses_dev->page2_len);

	for (i = 0; i < ses_dev->page1[10]; i++, type_ptr += 4) {
		for (j = 0; j < type_ptr[1]; j++) {
			desc_ptr += 4;
			if (type_ptr[0] != ENCLOSURE_COMPONENT_DEVICE &&
			    type_ptr[0] != ENCLOSURE_COMPONENT_ARRAY_DEVICE)
				continue;
			if (count++ == descriptor)
				return desc_ptr;
		}
	}
	return NULL;
}

static void ses_get_fault(struct enclosure_device *edev,
			  struct enclosure_component *ecomp)
{
	unsigned char *desc;

	desc = ses_get_page2_descriptor(edev, ecomp);
	if (desc)
		ecomp->fault = (desc[3] & 0x60) >> 4;
}

static int ses_set_fault(struct enclosure_device *edev,
			  struct enclosure_component *ecomp,
			 enum enclosure_component_setting val)
{
	unsigned char desc[4] = {0 };

	switch (val) {
	case ENCLOSURE_SETTING_DISABLED:
		/* zero is disabled */
		break;
	case ENCLOSURE_SETTING_ENABLED:
		desc[2] = 0x02;
		break;
	default:
		/* SES doesn't do the SGPIO blink settings */
		return -EINVAL;
	}

	return ses_set_page2_descriptor(edev, ecomp, desc);
}

static void ses_get_status(struct enclosure_device *edev,
			   struct enclosure_component *ecomp)
{
	unsigned char *desc;

	desc = ses_get_page2_descriptor(edev, ecomp);
	if (desc)
		ecomp->status = (desc[0] & 0x0f);
}

static void ses_get_locate(struct enclosure_device *edev,
			   struct enclosure_component *ecomp)
{
	unsigned char *desc;

	desc = ses_get_page2_descriptor(edev, ecomp);
	if (desc)
		ecomp->locate = (desc[2] & 0x02) ? 1 : 0;
}

static int ses_set_locate(struct enclosure_device *edev,
			  struct enclosure_component *ecomp,
			  enum enclosure_component_setting val)
{
	unsigned char desc[4] = {0 };

	switch (val) {
	case ENCLOSURE_SETTING_DISABLED:
		/* zero is disabled */
		break;
	case ENCLOSURE_SETTING_ENABLED:
		desc[2] = 0x02;
		break;
	default:
		/* SES doesn't do the SGPIO blink settings */
		return -EINVAL;
	}
	return ses_set_page2_descriptor(edev, ecomp, desc);
}

static int ses_set_active(struct enclosure_device *edev,
			  struct enclosure_component *ecomp,
			  enum enclosure_component_setting val)
{
	unsigned char desc[4] = {0 };

	switch (val) {
	case ENCLOSURE_SETTING_DISABLED:
		/* zero is disabled */
		ecomp->active = 0;
		break;
	case ENCLOSURE_SETTING_ENABLED:
		desc[2] = 0x80;
		ecomp->active = 1;
		break;
	default:
		/* SES doesn't do the SGPIO blink settings */
		return -EINVAL;
	}
	return ses_set_page2_descriptor(edev, ecomp, desc);
}

static struct enclosure_component_callbacks ses_enclosure_callbacks = {
	.get_fault		= ses_get_fault,
	.set_fault		= ses_set_fault,
	.get_status		= ses_get_status,
	.get_locate		= ses_get_locate,
	.set_locate		= ses_set_locate,
	.set_active		= ses_set_active,
};

struct ses_host_edev {
	struct Scsi_Host *shost;
	struct enclosure_device *edev;
};

int ses_match_host(struct enclosure_device *edev, void *data)
{
	struct ses_host_edev *sed = data;
	struct scsi_device *sdev;

	if (!scsi_is_sdev_device(edev->edev.parent))
		return 0;

	sdev = to_scsi_device(edev->edev.parent);

	if (sdev->host != sed->shost)
		return 0;

	sed->edev = edev;
	return 1;
}

static void ses_process_descriptor(struct enclosure_component *ecomp,
				   unsigned char *desc)
{
	int eip = desc[0] & 0x10;
	int invalid = desc[0] & 0x80;
	enum scsi_protocol proto = desc[0] & 0x0f;
	u64 addr = 0;
	struct ses_component *scomp = ecomp->scratch;
	unsigned char *d;

	scomp->desc = desc;

	if (invalid)
		return;

	switch (proto) {
	case SCSI_PROTOCOL_SAS:
		if (eip)
			d = desc + 8;
		else
			d = desc + 4;
		/* only take the phy0 addr */
		addr = (u64)d[12] << 56 |
			(u64)d[13] << 48 |
			(u64)d[14] << 40 |
			(u64)d[15] << 32 |
			(u64)d[16] << 24 |
			(u64)d[17] << 16 |
			(u64)d[18] << 8 |
			(u64)d[19];
		break;
	default:
		/* FIXME: Need to add more protocols than just SAS */
		break;
	}
	scomp->addr = addr;
}

struct efd {
	u64 addr;
	struct device *dev;
};

static int ses_enclosure_find_by_addr(struct enclosure_device *edev,
				      void *data)
{
	struct efd *efd = data;
	int i;
	struct ses_component *scomp;

	if (!edev->component[0].scratch)
		return 0;

	for (i = 0; i < edev->components; i++) {
		scomp = edev->component[i].scratch;
		if (scomp->addr != efd->addr)
			continue;

		enclosure_add_device(edev, i, efd->dev);
		return 1;
	}
	return 0;
}

#define VPD_INQUIRY_SIZE 512

static void ses_match_to_enclosure(struct enclosure_device *edev,
				   struct scsi_device *sdev)
{
	unsigned char *buf = kmalloc(VPD_INQUIRY_SIZE, GFP_KERNEL);
	unsigned char *desc;
	int len;
	struct efd efd = {
		.addr = 0,
	};
	unsigned char cmd[] = {
		INQUIRY,
		1,
		0x83,
		VPD_INQUIRY_SIZE >> 8,
		VPD_INQUIRY_SIZE & 0xff,
		0
	};

	if (!buf)
		return;

	if (scsi_execute_req(sdev, cmd, DMA_FROM_DEVICE, buf,
			     VPD_INQUIRY_SIZE, NULL, SES_TIMEOUT, SES_RETRIES))
		goto free;

	len = (buf[2] << 8) + buf[3];
	desc = buf + 4;
	while (desc < buf + len) {
		enum scsi_protocol proto = desc[0] >> 4;
		u8 code_set = desc[0] & 0x0f;
		u8 piv = desc[1] & 0x80;
		u8 assoc = (desc[1] & 0x30) >> 4;
		u8 type = desc[1] & 0x0f;
		u8 len = desc[3];

		if (piv && code_set == 1 && assoc == 1 && code_set == 1
		    && proto == SCSI_PROTOCOL_SAS && type == 3 && len == 8)
			efd.addr = (u64)desc[4] << 56 |
				(u64)desc[5] << 48 |
				(u64)desc[6] << 40 |
				(u64)desc[7] << 32 |
				(u64)desc[8] << 24 |
				(u64)desc[9] << 16 |
				(u64)desc[10] << 8 |
				(u64)desc[11];

		desc += len + 4;
	}
	if (!efd.addr)
		goto free;

	efd.dev = &sdev->sdev_gendev;

	enclosure_for_each_device(ses_enclosure_find_by_addr, &efd);
 free:
	kfree(buf);
}

#define INIT_ALLOC_SIZE 32

static int ses_intf_add(struct device *cdev,
			struct class_interface *intf)
{
	struct scsi_device *sdev = to_scsi_device(cdev->parent);
	struct scsi_device *tmp_sdev;
	unsigned char *buf = NULL, *hdr_buf, *type_ptr, *desc_ptr = NULL,
		*addl_desc_ptr = NULL;
	struct ses_device *ses_dev;
	u32 result;
	int i, j, types, len, page7_len = 0, components = 0;
	int err = -ENOMEM;
	struct enclosure_device *edev;
	struct ses_component *scomp = NULL;

	if (!scsi_device_enclosure(sdev)) {
		/* not an enclosure, but might be in one */
		edev = enclosure_find(&sdev->host->shost_gendev);
		if (edev) {
			ses_match_to_enclosure(edev, sdev);
			put_device(&edev->edev);
		}
		return -ENODEV;
	}

	/* TYPE_ENCLOSURE prints a message in probe */
	if (sdev->type != TYPE_ENCLOSURE)
		sdev_printk(KERN_NOTICE, sdev, "Embedded Enclosure Device\n");

	ses_dev = kzalloc(sizeof(*ses_dev), GFP_KERNEL);
	hdr_buf = kzalloc(INIT_ALLOC_SIZE, GFP_KERNEL);
	if (!hdr_buf || !ses_dev)
		goto err_init_free;

	result = ses_recv_diag(sdev, 1, hdr_buf, INIT_ALLOC_SIZE);
	if (result)
		goto recv_failed;

	if (hdr_buf[1] != 0) {
		/* FIXME: need subenclosure support; I've just never
		 * seen a device with subenclosures and it makes the
		 * traversal routines more complex */
		sdev_printk(KERN_ERR, sdev,
			"FIXME driver has no support for subenclosures (%d)\n",
			hdr_buf[1]);
		goto err_free;
	}

	len = (hdr_buf[2] << 8) + hdr_buf[3] + 4;
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		goto err_free;

	result = ses_recv_diag(sdev, 1, buf, len);
	if (result)
		goto recv_failed;

	types = buf[10];

	type_ptr = buf + 12 + buf[11];

	for (i = 0; i < types; i++, type_ptr += 4) {
		if (type_ptr[0] == ENCLOSURE_COMPONENT_DEVICE ||
		    type_ptr[0] == ENCLOSURE_COMPONENT_ARRAY_DEVICE)
			components += type_ptr[1];
	}
	ses_dev->page1 = buf;
	ses_dev->page1_len = len;
	buf = NULL;

	result = ses_recv_diag(sdev, 2, hdr_buf, INIT_ALLOC_SIZE);
	if (result)
		goto recv_failed;

	len = (hdr_buf[2] << 8) + hdr_buf[3] + 4;
	buf = kzalloc(len, GFP_KERNEL);
	if (!buf)
		goto err_free;

	/* make sure getting page 2 actually works */
	result = ses_recv_diag(sdev, 2, buf, len);
	if (result)
		goto recv_failed;
	ses_dev->page2 = buf;
	ses_dev->page2_len = len;
	buf = NULL;

	/* The additional information page --- allows us
	 * to match up the devices */
	result = ses_recv_diag(sdev, 10, hdr_buf, INIT_ALLOC_SIZE);
	if (!result) {

		len = (hdr_buf[2] << 8) + hdr_buf[3] + 4;
		buf = kzalloc(len, GFP_KERNEL);
		if (!buf)
			goto err_free;

		result = ses_recv_diag(sdev, 10, buf, len);
		if (result)
			goto recv_failed;
		ses_dev->page10 = buf;
		ses_dev->page10_len = len;
		buf = NULL;
	}

	scomp = kzalloc(sizeof(struct ses_component) * components, GFP_KERNEL);
	if (!scomp)
		goto err_free;

	edev = enclosure_register(cdev->parent, sdev->sdev_gendev.bus_id,
				  components, &ses_enclosure_callbacks);
	if (IS_ERR(edev)) {
		err = PTR_ERR(edev);
		goto err_free;
	}

	edev->scratch = ses_dev;
	for (i = 0; i < components; i++)
		edev->component[i].scratch = scomp + i;

	/* Page 7 for the descriptors is optional */
	result = ses_recv_diag(sdev, 7, hdr_buf, INIT_ALLOC_SIZE);
	if (result)
		goto simple_populate;

	page7_len = len = (hdr_buf[2] << 8) + hdr_buf[3] + 4;
	/* add 1 for trailing '\0' we'll use */
	buf = kzalloc(len + 1, GFP_KERNEL);
	if (!buf)
		goto simple_populate;
	result = ses_recv_diag(sdev, 7, buf, len);
	if (result) {
 simple_populate:
		kfree(buf);
		buf = NULL;
		desc_ptr = NULL;
		addl_desc_ptr = NULL;
	} else {
		desc_ptr = buf + 8;
		len = (desc_ptr[2] << 8) + desc_ptr[3];
		/* skip past overall descriptor */
		desc_ptr += len + 4;
		if (ses_dev->page10)
			addl_desc_ptr = ses_dev->page10 + 8;
	}
	type_ptr = ses_dev->page1 + 12 + ses_dev->page1[11];
	components = 0;
	for (i = 0; i < types; i++, type_ptr += 4) {
		for (j = 0; j < type_ptr[1]; j++) {
			char *name = NULL;
			struct enclosure_component *ecomp;

			if (desc_ptr) {
				if (desc_ptr >= buf + page7_len) {
					desc_ptr = NULL;
				} else {
					len = (desc_ptr[2] << 8) + desc_ptr[3];
					desc_ptr += 4;
					/* Add trailing zero - pushes into
					 * reserved space */
					desc_ptr[len] = '\0';
					name = desc_ptr;
				}
			}
			if (type_ptr[0] == ENCLOSURE_COMPONENT_DEVICE ||
			    type_ptr[0] == ENCLOSURE_COMPONENT_ARRAY_DEVICE) {

				ecomp =	enclosure_component_register(edev,
							     components++,
							     type_ptr[0],
							     name);

				if (!IS_ERR(ecomp) && addl_desc_ptr)
					ses_process_descriptor(ecomp,
							       addl_desc_ptr);
			}
			if (desc_ptr)
				desc_ptr += len;

			if (addl_desc_ptr)
				addl_desc_ptr += addl_desc_ptr[1] + 2;

		}
	}
	kfree(buf);
	kfree(hdr_buf);

	/* see if there are any devices matching before
	 * we found the enclosure */
	shost_for_each_device(tmp_sdev, sdev->host) {
		if (tmp_sdev->lun != 0 || scsi_device_enclosure(tmp_sdev))
			continue;
		ses_match_to_enclosure(edev, tmp_sdev);
	}

	return 0;

 recv_failed:
	sdev_printk(KERN_ERR, sdev, "Failed to get diagnostic page 0x%x\n",
		    result);
	err = -ENODEV;
 err_free:
	kfree(buf);
	kfree(scomp);
	kfree(ses_dev->page10);
	kfree(ses_dev->page2);
	kfree(ses_dev->page1);
 err_init_free:
	kfree(ses_dev);
	kfree(hdr_buf);
	sdev_printk(KERN_ERR, sdev, "Failed to bind enclosure %d\n", err);
	return err;
}

static int ses_remove(struct device *dev)
{
	return 0;
}

static void ses_intf_remove(struct device *cdev,
			    struct class_interface *intf)
{
	struct scsi_device *sdev = to_scsi_device(cdev->parent);
	struct enclosure_device *edev;
	struct ses_device *ses_dev;

	if (!scsi_device_enclosure(sdev))
		return;

	edev = enclosure_find(cdev->parent);
	if (!edev)
		return;

	ses_dev = edev->scratch;
	edev->scratch = NULL;

	kfree(ses_dev->page10);
	kfree(ses_dev->page1);
	kfree(ses_dev->page2);
	kfree(ses_dev);

	kfree(edev->component[0].scratch);

	put_device(&edev->edev);
	enclosure_unregister(edev);
}

static struct class_interface ses_interface = {
	.add_dev	= ses_intf_add,
	.remove_dev	= ses_intf_remove,
};

static struct scsi_driver ses_template = {
	.owner			= THIS_MODULE,
	.gendrv = {
		.name		= "ses",
		.probe		= ses_probe,
		.remove		= ses_remove,
	},
};

static int __init ses_init(void)
{
	int err;

	err = scsi_register_interface(&ses_interface);
	if (err)
		return err;

	err = scsi_register_driver(&ses_template.gendrv);
	if (err)
		goto out_unreg;

	return 0;

 out_unreg:
	scsi_unregister_interface(&ses_interface);
	return err;
}

static void __exit ses_exit(void)
{
	scsi_unregister_driver(&ses_template.gendrv);
	scsi_unregister_interface(&ses_interface);
}

module_init(ses_init);
module_exit(ses_exit);

MODULE_ALIAS_SCSI_DEVICE(TYPE_ENCLOSURE);

MODULE_AUTHOR("James Bottomley");
MODULE_DESCRIPTION("SCSI Enclosure Services (ses) driver");
MODULE_LICENSE("GPL v2");
