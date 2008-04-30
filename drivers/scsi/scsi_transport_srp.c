/*
 * SCSI RDMA (SRP) transport class
 *
 * Copyright (C) 2007 FUJITA Tomonori <tomof@acm.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation, version 2 of the
 * License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 */
#include <linux/init.h>
#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/string.h>

#include <scsi/scsi.h>
#include <scsi/scsi_device.h>
#include <scsi/scsi_host.h>
#include <scsi/scsi_transport.h>
#include <scsi/scsi_transport_srp.h>
#include "scsi_transport_srp_internal.h"

struct srp_host_attrs {
	atomic_t next_port_id;
};
#define to_srp_host_attrs(host)	((struct srp_host_attrs *)(host)->shost_data)

#define SRP_HOST_ATTRS 0
#define SRP_RPORT_ATTRS 2

struct srp_internal {
	struct scsi_transport_template t;
	struct srp_function_template *f;

	struct device_attribute *host_attrs[SRP_HOST_ATTRS + 1];

	struct device_attribute *rport_attrs[SRP_RPORT_ATTRS + 1];
	struct device_attribute private_rport_attrs[SRP_RPORT_ATTRS];
	struct transport_container rport_attr_cont;
};

#define to_srp_internal(tmpl) container_of(tmpl, struct srp_internal, t)

#define	dev_to_rport(d)	container_of(d, struct srp_rport, dev)
#define transport_class_to_srp_rport(dev) dev_to_rport((dev)->parent)

static int srp_host_setup(struct transport_container *tc, struct device *dev,
			  struct device *cdev)
{
	struct Scsi_Host *shost = dev_to_shost(dev);
	struct srp_host_attrs *srp_host = to_srp_host_attrs(shost);

	atomic_set(&srp_host->next_port_id, 0);
	return 0;
}

static DECLARE_TRANSPORT_CLASS(srp_host_class, "srp_host", srp_host_setup,
			       NULL, NULL);

static DECLARE_TRANSPORT_CLASS(srp_rport_class, "srp_remote_ports",
			       NULL, NULL, NULL);

#define SETUP_TEMPLATE(attrb, field, perm, test, ro_test, ro_perm)	\
	i->private_##attrb[count] = dev_attr_##field;		\
	i->private_##attrb[count].attr.mode = perm;			\
	if (ro_test) {							\
		i->private_##attrb[count].attr.mode = ro_perm;		\
		i->private_##attrb[count].store = NULL;			\
	}								\
	i->attrb[count] = &i->private_##attrb[count];			\
	if (test)							\
		count++

#define SETUP_RPORT_ATTRIBUTE_RD(field)					\
	SETUP_TEMPLATE(rport_attrs, field, S_IRUGO, 1, 0, 0)

#define SETUP_RPORT_ATTRIBUTE_RW(field)					\
	SETUP_TEMPLATE(rport_attrs, field, S_IRUGO | S_IWUSR,		\
		       1, 1, S_IRUGO)

#define SRP_PID(p) \
	(p)->port_id[0], (p)->port_id[1], (p)->port_id[2], (p)->port_id[3], \
	(p)->port_id[4], (p)->port_id[5], (p)->port_id[6], (p)->port_id[7], \
	(p)->port_id[8], (p)->port_id[9], (p)->port_id[10], (p)->port_id[11], \
	(p)->port_id[12], (p)->port_id[13], (p)->port_id[14], (p)->port_id[15]

#define SRP_PID_FMT "%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:" \
	"%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x"

static ssize_t
show_srp_rport_id(struct device *dev, struct device_attribute *attr,
		  char *buf)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	return sprintf(buf, SRP_PID_FMT "\n", SRP_PID(rport));
}

static DEVICE_ATTR(port_id, S_IRUGO, show_srp_rport_id, NULL);

static const struct {
	u32 value;
	char *name;
} srp_rport_role_names[] = {
	{SRP_RPORT_ROLE_INITIATOR, "SRP Initiator"},
	{SRP_RPORT_ROLE_TARGET, "SRP Target"},
};

static ssize_t
show_srp_rport_roles(struct device *dev, struct device_attribute *attr,
		     char *buf)
{
	struct srp_rport *rport = transport_class_to_srp_rport(dev);
	int i;
	char *name = NULL;

	for (i = 0; i < ARRAY_SIZE(srp_rport_role_names); i++)
		if (srp_rport_role_names[i].value == rport->roles) {
			name = srp_rport_role_names[i].name;
			break;
		}
	return sprintf(buf, "%s\n", name ? : "unknown");
}

static DEVICE_ATTR(roles, S_IRUGO, show_srp_rport_roles, NULL);

static void srp_rport_release(struct device *dev)
{
	struct srp_rport *rport = dev_to_rport(dev);

	put_device(dev->parent);
	kfree(rport);
}

static int scsi_is_srp_rport(const struct device *dev)
{
	return dev->release == srp_rport_release;
}

static int srp_rport_match(struct attribute_container *cont,
			   struct device *dev)
{
	struct Scsi_Host *shost;
	struct srp_internal *i;

	if (!scsi_is_srp_rport(dev))
		return 0;

	shost = dev_to_shost(dev->parent);
	if (!shost->transportt)
		return 0;
	if (shost->transportt->host_attrs.ac.class != &srp_host_class.class)
		return 0;

	i = to_srp_internal(shost->transportt);
	return &i->rport_attr_cont.ac == cont;
}

static int srp_host_match(struct attribute_container *cont, struct device *dev)
{
	struct Scsi_Host *shost;
	struct srp_internal *i;

	if (!scsi_is_host_device(dev))
		return 0;

	shost = dev_to_shost(dev);
	if (!shost->transportt)
		return 0;
	if (shost->transportt->host_attrs.ac.class != &srp_host_class.class)
		return 0;

	i = to_srp_internal(shost->transportt);
	return &i->t.host_attrs.ac == cont;
}

/**
 * srp_rport_add - add a SRP remote port to the device hierarchy
 * @shost:	scsi host the remote port is connected to.
 * @ids:	The port id for the remote port.
 *
 * Publishes a port to the rest of the system.
 */
struct srp_rport *srp_rport_add(struct Scsi_Host *shost,
				struct srp_rport_identifiers *ids)
{
	struct srp_rport *rport;
	struct device *parent = &shost->shost_gendev;
	int id, ret;

	rport = kzalloc(sizeof(*rport), GFP_KERNEL);
	if (!rport)
		return ERR_PTR(-ENOMEM);

	device_initialize(&rport->dev);

	rport->dev.parent = get_device(parent);
	rport->dev.release = srp_rport_release;

	memcpy(rport->port_id, ids->port_id, sizeof(rport->port_id));
	rport->roles = ids->roles;

	id = atomic_inc_return(&to_srp_host_attrs(shost)->next_port_id);
	sprintf(rport->dev.bus_id, "port-%d:%d", shost->host_no, id);

	transport_setup_device(&rport->dev);

	ret = device_add(&rport->dev);
	if (ret) {
		transport_destroy_device(&rport->dev);
		put_device(&rport->dev);
		return ERR_PTR(ret);
	}

	if (shost->active_mode & MODE_TARGET &&
	    ids->roles == SRP_RPORT_ROLE_INITIATOR) {
		ret = srp_tgt_it_nexus_create(shost, (unsigned long)rport,
					      rport->port_id);
		if (ret) {
			device_del(&rport->dev);
			transport_destroy_device(&rport->dev);
			put_device(&rport->dev);
			return ERR_PTR(ret);
		}
	}

	transport_add_device(&rport->dev);
	transport_configure_device(&rport->dev);

	return rport;
}
EXPORT_SYMBOL_GPL(srp_rport_add);

/**
 * srp_rport_del  -  remove a SRP remote port
 * @rport:	SRP remote port to remove
 *
 * Removes the specified SRP remote port.
 */
void srp_rport_del(struct srp_rport *rport)
{
	struct device *dev = &rport->dev;
	struct Scsi_Host *shost = dev_to_shost(dev->parent);

	if (shost->active_mode & MODE_TARGET &&
	    rport->roles == SRP_RPORT_ROLE_INITIATOR)
		srp_tgt_it_nexus_destroy(shost, (unsigned long)rport);

	transport_remove_device(dev);
	device_del(dev);
	transport_destroy_device(dev);
	put_device(dev);
}
EXPORT_SYMBOL_GPL(srp_rport_del);

static int do_srp_rport_del(struct device *dev, void *data)
{
	if (scsi_is_srp_rport(dev))
		srp_rport_del(dev_to_rport(dev));
	return 0;
}

/**
 * srp_remove_host  -  tear down a Scsi_Host's SRP data structures
 * @shost:	Scsi Host that is torn down
 *
 * Removes all SRP remote ports for a given Scsi_Host.
 * Must be called just before scsi_remove_host for SRP HBAs.
 */
void srp_remove_host(struct Scsi_Host *shost)
{
	device_for_each_child(&shost->shost_gendev, NULL, do_srp_rport_del);
}
EXPORT_SYMBOL_GPL(srp_remove_host);

static int srp_tsk_mgmt_response(struct Scsi_Host *shost, u64 nexus, u64 tm_id,
				 int result)
{
	struct srp_internal *i = to_srp_internal(shost->transportt);
	return i->f->tsk_mgmt_response(shost, nexus, tm_id, result);
}

static int srp_it_nexus_response(struct Scsi_Host *shost, u64 nexus, int result)
{
	struct srp_internal *i = to_srp_internal(shost->transportt);
	return i->f->it_nexus_response(shost, nexus, result);
}

/**
 * srp_attach_transport  -  instantiate SRP transport template
 * @ft:		SRP transport class function template
 */
struct scsi_transport_template *
srp_attach_transport(struct srp_function_template *ft)
{
	int count;
	struct srp_internal *i;

	i = kzalloc(sizeof(*i), GFP_KERNEL);
	if (!i)
		return NULL;

	i->t.tsk_mgmt_response = srp_tsk_mgmt_response;
	i->t.it_nexus_response = srp_it_nexus_response;

	i->t.host_size = sizeof(struct srp_host_attrs);
	i->t.host_attrs.ac.attrs = &i->host_attrs[0];
	i->t.host_attrs.ac.class = &srp_host_class.class;
	i->t.host_attrs.ac.match = srp_host_match;
	i->host_attrs[0] = NULL;
	transport_container_register(&i->t.host_attrs);

	i->rport_attr_cont.ac.attrs = &i->rport_attrs[0];
	i->rport_attr_cont.ac.class = &srp_rport_class.class;
	i->rport_attr_cont.ac.match = srp_rport_match;
	transport_container_register(&i->rport_attr_cont);

	count = 0;
	SETUP_RPORT_ATTRIBUTE_RD(port_id);
	SETUP_RPORT_ATTRIBUTE_RD(roles);
	i->rport_attrs[count] = NULL;

	i->f = ft;

	return &i->t;
}
EXPORT_SYMBOL_GPL(srp_attach_transport);

/**
 * srp_release_transport  -  release SRP transport template instance
 * @t:		transport template instance
 */
void srp_release_transport(struct scsi_transport_template *t)
{
	struct srp_internal *i = to_srp_internal(t);

	transport_container_unregister(&i->t.host_attrs);
	transport_container_unregister(&i->rport_attr_cont);

	kfree(i);
}
EXPORT_SYMBOL_GPL(srp_release_transport);

static __init int srp_transport_init(void)
{
	int ret;

	ret = transport_class_register(&srp_host_class);
	if (ret)
		return ret;
	ret = transport_class_register(&srp_rport_class);
	if (ret)
		goto unregister_host_class;

	return 0;
unregister_host_class:
	transport_class_unregister(&srp_host_class);
	return ret;
}

static void __exit srp_transport_exit(void)
{
	transport_class_unregister(&srp_host_class);
	transport_class_unregister(&srp_rport_class);
}

MODULE_AUTHOR("FUJITA Tomonori");
MODULE_DESCRIPTION("SRP Transport Attributes");
MODULE_LICENSE("GPL");

module_init(srp_transport_init);
module_exit(srp_transport_exit);
