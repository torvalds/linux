/* 
 * iSCSI transport class definitions
 *
 * Copyright (C) IBM Corporation, 2004
 * Copyright (C) Mike Christie, 2004
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#include <linux/module.h>
#include <scsi/scsi.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_iscsi.h>

#define ISCSI_SESSION_ATTRS 20
#define ISCSI_HOST_ATTRS 2

struct iscsi_internal {
	struct scsi_transport_template t;
	struct iscsi_function_template *fnt;
	/*
	 * We do not have any private or other attrs.
	 */
	struct class_device_attribute *session_attrs[ISCSI_SESSION_ATTRS + 1];
	struct class_device_attribute *host_attrs[ISCSI_HOST_ATTRS + 1];
};

#define to_iscsi_internal(tmpl) container_of(tmpl, struct iscsi_internal, t)

static DECLARE_TRANSPORT_CLASS(iscsi_transport_class,
			       "iscsi_transport",
			       NULL,
			       NULL,
			       NULL);

static DECLARE_TRANSPORT_CLASS(iscsi_host_class,
			       "iscsi_host",
			       NULL,
			       NULL,
			       NULL);
/*
 * iSCSI target and session attrs
 */
#define iscsi_session_show_fn(field, format)				\
									\
static ssize_t								\
show_session_##field(struct class_device *cdev, char *buf)		\
{									\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt); \
									\
	if (i->fnt->get_##field)					\
		i->fnt->get_##field(starget);				\
	return snprintf(buf, 20, format"\n", iscsi_##field(starget));	\
}

#define iscsi_session_rd_attr(field, format)				\
	iscsi_session_show_fn(field, format)				\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_session_##field, NULL);

iscsi_session_rd_attr(tpgt, "%hu");
iscsi_session_rd_attr(tsih, "%2x");
iscsi_session_rd_attr(max_recv_data_segment_len, "%u");
iscsi_session_rd_attr(max_burst_len, "%u");
iscsi_session_rd_attr(first_burst_len, "%u");
iscsi_session_rd_attr(def_time2wait, "%hu");
iscsi_session_rd_attr(def_time2retain, "%hu");
iscsi_session_rd_attr(max_outstanding_r2t, "%hu");
iscsi_session_rd_attr(erl, "%d");


#define iscsi_session_show_bool_fn(field)				\
									\
static ssize_t								\
show_session_bool_##field(struct class_device *cdev, char *buf)		\
{									\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt); \
									\
	if (i->fnt->get_##field)					\
		i->fnt->get_##field(starget);				\
									\
	if (iscsi_##field(starget))					\
		return sprintf(buf, "Yes\n");				\
	return sprintf(buf, "No\n");					\
}

#define iscsi_session_rd_bool_attr(field)				\
	iscsi_session_show_bool_fn(field)				\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_session_bool_##field, NULL);

iscsi_session_rd_bool_attr(initial_r2t);
iscsi_session_rd_bool_attr(immediate_data);
iscsi_session_rd_bool_attr(data_pdu_in_order);
iscsi_session_rd_bool_attr(data_sequence_in_order);

#define iscsi_session_show_digest_fn(field)				\
									\
static ssize_t								\
show_##field(struct class_device *cdev, char *buf)			\
{									\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt); \
									\
	if (i->fnt->get_##field)					\
		i->fnt->get_##field(starget);				\
									\
	if (iscsi_##field(starget))					\
		return sprintf(buf, "CRC32C\n");			\
	return sprintf(buf, "None\n");					\
}

#define iscsi_session_rd_digest_attr(field)				\
	iscsi_session_show_digest_fn(field)				\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_##field, NULL);

iscsi_session_rd_digest_attr(header_digest);
iscsi_session_rd_digest_attr(data_digest);

static ssize_t
show_port(struct class_device *cdev, char *buf)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt);

	if (i->fnt->get_port)
		i->fnt->get_port(starget);

	return snprintf(buf, 20, "%hu\n", ntohs(iscsi_port(starget)));
}
static CLASS_DEVICE_ATTR(port, S_IRUGO, show_port, NULL);

static ssize_t
show_ip_address(struct class_device *cdev, char *buf)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt);

	if (i->fnt->get_ip_address)
		i->fnt->get_ip_address(starget);

	if (iscsi_addr_type(starget) == AF_INET)
		return sprintf(buf, "%u.%u.%u.%u\n",
			       NIPQUAD(iscsi_sin_addr(starget)));
	else if(iscsi_addr_type(starget) == AF_INET6)
		return sprintf(buf, "%04x:%04x:%04x:%04x:%04x:%04x:%04x:%04x\n",
			       NIP6(iscsi_sin6_addr(starget)));
	return -EINVAL;
}
static CLASS_DEVICE_ATTR(ip_address, S_IRUGO, show_ip_address, NULL);

static ssize_t
show_isid(struct class_device *cdev, char *buf)
{
	struct scsi_target *starget = transport_class_to_starget(cdev);
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt);

	if (i->fnt->get_isid)
		i->fnt->get_isid(starget);

	return sprintf(buf, "%02x%02x%02x%02x%02x%02x\n",
		       iscsi_isid(starget)[0], iscsi_isid(starget)[1],
		       iscsi_isid(starget)[2], iscsi_isid(starget)[3],
		       iscsi_isid(starget)[4], iscsi_isid(starget)[5]);
}
static CLASS_DEVICE_ATTR(isid, S_IRUGO, show_isid, NULL);

/*
 * This is used for iSCSI names. Normally, we follow
 * the transport class convention of having the lld
 * set the field, but in these cases the value is
 * too large.
 */
#define iscsi_session_show_str_fn(field)				\
									\
static ssize_t								\
show_session_str_##field(struct class_device *cdev, char *buf)		\
{									\
	ssize_t ret = 0;						\
	struct scsi_target *starget = transport_class_to_starget(cdev);	\
	struct Scsi_Host *shost = dev_to_shost(starget->dev.parent);	\
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt); \
									\
	if (i->fnt->get_##field)					\
		ret = i->fnt->get_##field(starget, buf, PAGE_SIZE);	\
	return ret;							\
}

#define iscsi_session_rd_str_attr(field)				\
	iscsi_session_show_str_fn(field)				\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_session_str_##field, NULL);

iscsi_session_rd_str_attr(target_name);
iscsi_session_rd_str_attr(target_alias);

/*
 * iSCSI host attrs
 */

/*
 * Again, this is used for iSCSI names. Normally, we follow
 * the transport class convention of having the lld set
 * the field, but in these cases the value is too large.
 */
#define iscsi_host_show_str_fn(field)					\
									\
static ssize_t								\
show_host_str_##field(struct class_device *cdev, char *buf)		\
{									\
	int ret = 0;							\
	struct Scsi_Host *shost = transport_class_to_shost(cdev);	\
	struct iscsi_internal *i = to_iscsi_internal(shost->transportt); \
									\
	if (i->fnt->get_##field)					\
		ret = i->fnt->get_##field(shost, buf, PAGE_SIZE);	\
	return ret;							\
}

#define iscsi_host_rd_str_attr(field)					\
	iscsi_host_show_str_fn(field)					\
static CLASS_DEVICE_ATTR(field, S_IRUGO, show_host_str_##field, NULL);

iscsi_host_rd_str_attr(initiator_name);
iscsi_host_rd_str_attr(initiator_alias);

#define SETUP_SESSION_RD_ATTR(field)					\
	if (i->fnt->show_##field) {					\
		i->session_attrs[count] = &class_device_attr_##field;	\
		count++;						\
	}

#define SETUP_HOST_RD_ATTR(field)					\
	if (i->fnt->show_##field) {					\
		i->host_attrs[count] = &class_device_attr_##field;	\
		count++;						\
	}

static int iscsi_host_match(struct attribute_container *cont,
			  struct device *dev)
{
	struct Scsi_Host *shost;
	struct iscsi_internal *i;

	if (!scsi_is_host_device(dev))
		return 0;

	shost = dev_to_shost(dev);
	if (!shost->transportt  || shost->transportt->host_attrs.ac.class
	    != &iscsi_host_class.class)
		return 0;

	i = to_iscsi_internal(shost->transportt);
	
	return &i->t.host_attrs.ac == cont;
}

static int iscsi_target_match(struct attribute_container *cont,
			    struct device *dev)
{
	struct Scsi_Host *shost;
	struct iscsi_internal *i;

	if (!scsi_is_target_device(dev))
		return 0;

	shost = dev_to_shost(dev->parent);
	if (!shost->transportt  || shost->transportt->host_attrs.ac.class
	    != &iscsi_host_class.class)
		return 0;

	i = to_iscsi_internal(shost->transportt);
	
	return &i->t.target_attrs.ac == cont;
}

struct scsi_transport_template *
iscsi_attach_transport(struct iscsi_function_template *fnt)
{
	struct iscsi_internal *i = kmalloc(sizeof(struct iscsi_internal),
					   GFP_KERNEL);
	int count = 0;

	if (unlikely(!i))
		return NULL;

	memset(i, 0, sizeof(struct iscsi_internal));
	i->fnt = fnt;

	i->t.target_attrs.ac.attrs = &i->session_attrs[0];
	i->t.target_attrs.ac.class = &iscsi_transport_class.class;
	i->t.target_attrs.ac.match = iscsi_target_match;
	transport_container_register(&i->t.target_attrs);
	i->t.target_size = sizeof(struct iscsi_class_session);

	SETUP_SESSION_RD_ATTR(tsih);
	SETUP_SESSION_RD_ATTR(isid);
	SETUP_SESSION_RD_ATTR(header_digest);
	SETUP_SESSION_RD_ATTR(data_digest);
	SETUP_SESSION_RD_ATTR(target_name);
	SETUP_SESSION_RD_ATTR(target_alias);
	SETUP_SESSION_RD_ATTR(port);
	SETUP_SESSION_RD_ATTR(tpgt);
	SETUP_SESSION_RD_ATTR(ip_address);
	SETUP_SESSION_RD_ATTR(initial_r2t);
	SETUP_SESSION_RD_ATTR(immediate_data);
	SETUP_SESSION_RD_ATTR(max_recv_data_segment_len);
	SETUP_SESSION_RD_ATTR(max_burst_len);
	SETUP_SESSION_RD_ATTR(first_burst_len);
	SETUP_SESSION_RD_ATTR(def_time2wait);
	SETUP_SESSION_RD_ATTR(def_time2retain);
	SETUP_SESSION_RD_ATTR(max_outstanding_r2t);
	SETUP_SESSION_RD_ATTR(data_pdu_in_order);
	SETUP_SESSION_RD_ATTR(data_sequence_in_order);
	SETUP_SESSION_RD_ATTR(erl);

	BUG_ON(count > ISCSI_SESSION_ATTRS);
	i->session_attrs[count] = NULL;

	i->t.host_attrs.ac.attrs = &i->host_attrs[0];
	i->t.host_attrs.ac.class = &iscsi_host_class.class;
	i->t.host_attrs.ac.match = iscsi_host_match;
	transport_container_register(&i->t.host_attrs);
	i->t.host_size = 0;

	count = 0;
	SETUP_HOST_RD_ATTR(initiator_name);
	SETUP_HOST_RD_ATTR(initiator_alias);

	BUG_ON(count > ISCSI_HOST_ATTRS);
	i->host_attrs[count] = NULL;

	return &i->t;
}

EXPORT_SYMBOL(iscsi_attach_transport);

void iscsi_release_transport(struct scsi_transport_template *t)
{
	struct iscsi_internal *i = to_iscsi_internal(t);

	transport_container_unregister(&i->t.target_attrs);
	transport_container_unregister(&i->t.host_attrs);
  
	kfree(i);
}

EXPORT_SYMBOL(iscsi_release_transport);

static __init int iscsi_transport_init(void)
{
	int err = transport_class_register(&iscsi_transport_class);

	if (err)
		return err;
	return transport_class_register(&iscsi_host_class);
}

static void __exit iscsi_transport_exit(void)
{
	transport_class_unregister(&iscsi_host_class);
	transport_class_unregister(&iscsi_transport_class);
}

module_init(iscsi_transport_init);
module_exit(iscsi_transport_exit);

MODULE_AUTHOR("Mike Christie");
MODULE_DESCRIPTION("iSCSI Transport Attributes");
MODULE_LICENSE("GPL");
