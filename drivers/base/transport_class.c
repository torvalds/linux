// SPDX-License-Identifier: GPL-2.0
/*
 * transport_class.c - implementation of generic transport classes
 *                     using attribute_containers
 *
 * Copyright (c) 2005 - James Bottomley <James.Bottomley@steeleye.com>
 *
 * The basic idea here is to allow any "device controller" (which
 * would most often be a Host Bus Adapter to use the services of one
 * or more tranport classes for performing transport specific
 * services.  Transport specific services are things that the generic
 * command layer doesn't want to know about (speed settings, line
 * condidtioning, etc), but which the user might be interested in.
 * Thus, the HBA's use the routines exported by the transport classes
 * to perform these functions.  The transport classes export certain
 * values to the user via sysfs using attribute containers.
 *
 * Note: because not every HBA will care about every transport
 * attribute, there's a many to one relationship that goes like this:
 *
 * transport class<-----attribute container<----class device
 *
 * Usually the attribute container is per-HBA, but the design doesn't
 * mandate that.  Although most of the services will be specific to
 * the actual external storage connection used by the HBA, the generic
 * transport class is framed entirely in terms of generic devices to
 * allow it to be used by any physical HBA in the system.
 */
#include <linux/export.h>
#include <linux/attribute_container.h>
#include <linux/transport_class.h>

static int transport_remove_classdev(struct attribute_container *cont,
				     struct device *dev,
				     struct device *classdev);

/**
 * transport_class_register - register an initial transport class
 *
 * @tclass:	a pointer to the transport class structure to be initialised
 *
 * The transport class contains an embedded class which is used to
 * identify it.  The caller should initialise this structure with
 * zeros and then generic class must have been initialised with the
 * actual transport class unique name.  There's a macro
 * DECLARE_TRANSPORT_CLASS() to do this (declared classes still must
 * be registered).
 *
 * Returns 0 on success or error on failure.
 */
int transport_class_register(struct transport_class *tclass)
{
	return class_register(&tclass->class);
}
EXPORT_SYMBOL_GPL(transport_class_register);

/**
 * transport_class_unregister - unregister a previously registered class
 *
 * @tclass: The transport class to unregister
 *
 * Must be called prior to deallocating the memory for the transport
 * class.
 */
void transport_class_unregister(struct transport_class *tclass)
{
	class_unregister(&tclass->class);
}
EXPORT_SYMBOL_GPL(transport_class_unregister);

static int anon_transport_dummy_function(struct transport_container *tc,
					 struct device *dev,
					 struct device *cdev)
{
	/* do nothing */
	return 0;
}

/**
 * anon_transport_class_register - register an anonymous class
 *
 * @atc: The anon transport class to register
 *
 * The anonymous transport class contains both a transport class and a
 * container.  The idea of an anonymous class is that it never
 * actually has any device attributes associated with it (and thus
 * saves on container storage).  So it can only be used for triggering
 * events.  Use prezero and then use DECLARE_ANON_TRANSPORT_CLASS() to
 * initialise the anon transport class storage.
 */
int anon_transport_class_register(struct anon_transport_class *atc)
{
	int error;
	atc->container.class = &atc->tclass.class;
	attribute_container_set_no_classdevs(&atc->container);
	error = attribute_container_register(&atc->container);
	if (error)
		return error;
	atc->tclass.setup = anon_transport_dummy_function;
	atc->tclass.remove = anon_transport_dummy_function;
	return 0;
}
EXPORT_SYMBOL_GPL(anon_transport_class_register);

/**
 * anon_transport_class_unregister - unregister an anon class
 *
 * @atc: Pointer to the anon transport class to unregister
 *
 * Must be called prior to deallocating the memory for the anon
 * transport class.
 */
void anon_transport_class_unregister(struct anon_transport_class *atc)
{
	if (unlikely(attribute_container_unregister(&atc->container)))
		BUG();
}
EXPORT_SYMBOL_GPL(anon_transport_class_unregister);

static int transport_setup_classdev(struct attribute_container *cont,
				    struct device *dev,
				    struct device *classdev)
{
	struct transport_class *tclass = class_to_transport_class(cont->class);
	struct transport_container *tcont = attribute_container_to_transport_container(cont);

	if (tclass->setup)
		tclass->setup(tcont, dev, classdev);

	return 0;
}

/**
 * transport_setup_device - declare a new dev for transport class association but don't make it visible yet.
 * @dev: the generic device representing the entity being added
 *
 * Usually, dev represents some component in the HBA system (either
 * the HBA itself or a device remote across the HBA bus).  This
 * routine is simply a trigger point to see if any set of transport
 * classes wishes to associate with the added device.  This allocates
 * storage for the class device and initialises it, but does not yet
 * add it to the system or add attributes to it (you do this with
 * transport_add_device).  If you have no need for a separate setup
 * and add operations, use transport_register_device (see
 * transport_class.h).
 */

void transport_setup_device(struct device *dev)
{
	attribute_container_add_device(dev, transport_setup_classdev);
}
EXPORT_SYMBOL_GPL(transport_setup_device);

static int transport_add_class_device(struct attribute_container *cont,
				      struct device *dev,
				      struct device *classdev)
{
	struct transport_class *tclass = class_to_transport_class(cont->class);
	int error = attribute_container_add_class_device(classdev);
	struct transport_container *tcont = 
		attribute_container_to_transport_container(cont);

	if (error)
		goto err_remove;

	if (tcont->statistics) {
		error = sysfs_create_group(&classdev->kobj, tcont->statistics);
		if (error)
			goto err_del;
	}

	return 0;

err_del:
	attribute_container_class_device_del(classdev);
err_remove:
	if (tclass->remove)
		tclass->remove(tcont, dev, classdev);

	return error;
}


/**
 * transport_add_device - declare a new dev for transport class association
 *
 * @dev: the generic device representing the entity being added
 *
 * Usually, dev represents some component in the HBA system (either
 * the HBA itself or a device remote across the HBA bus).  This
 * routine is simply a trigger point used to add the device to the
 * system and register attributes for it.
 */
int transport_add_device(struct device *dev)
{
	return attribute_container_device_trigger_safe(dev,
					transport_add_class_device,
					transport_remove_classdev);
}
EXPORT_SYMBOL_GPL(transport_add_device);

static int transport_configure(struct attribute_container *cont,
			       struct device *dev,
			       struct device *cdev)
{
	struct transport_class *tclass = class_to_transport_class(cont->class);
	struct transport_container *tcont = attribute_container_to_transport_container(cont);

	if (tclass->configure)
		tclass->configure(tcont, dev, cdev);

	return 0;
}

/**
 * transport_configure_device - configure an already set up device
 *
 * @dev: generic device representing device to be configured
 *
 * The idea of configure is simply to provide a point within the setup
 * process to allow the transport class to extract information from a
 * device after it has been setup.  This is used in SCSI because we
 * have to have a setup device to begin using the HBA, but after we
 * send the initial inquiry, we use configure to extract the device
 * parameters.  The device need not have been added to be configured.
 */
void transport_configure_device(struct device *dev)
{
	attribute_container_device_trigger(dev, transport_configure);
}
EXPORT_SYMBOL_GPL(transport_configure_device);

static int transport_remove_classdev(struct attribute_container *cont,
				     struct device *dev,
				     struct device *classdev)
{
	struct transport_container *tcont = 
		attribute_container_to_transport_container(cont);
	struct transport_class *tclass = class_to_transport_class(cont->class);

	if (tclass->remove)
		tclass->remove(tcont, dev, classdev);

	if (tclass->remove != anon_transport_dummy_function) {
		if (tcont->statistics)
			sysfs_remove_group(&classdev->kobj, tcont->statistics);
		attribute_container_class_device_del(classdev);
	}

	return 0;
}


/**
 * transport_remove_device - remove the visibility of a device
 *
 * @dev: generic device to remove
 *
 * This call removes the visibility of the device (to the user from
 * sysfs), but does not destroy it.  To eliminate a device entirely
 * you must also call transport_destroy_device.  If you don't need to
 * do remove and destroy as separate operations, use
 * transport_unregister_device() (see transport_class.h) which will
 * perform both calls for you.
 */
void transport_remove_device(struct device *dev)
{
	attribute_container_device_trigger(dev, transport_remove_classdev);
}
EXPORT_SYMBOL_GPL(transport_remove_device);

static void transport_destroy_classdev(struct attribute_container *cont,
				      struct device *dev,
				      struct device *classdev)
{
	struct transport_class *tclass = class_to_transport_class(cont->class);

	if (tclass->remove != anon_transport_dummy_function)
		put_device(classdev);
}


/**
 * transport_destroy_device - destroy a removed device
 *
 * @dev: device to eliminate from the transport class.
 *
 * This call triggers the elimination of storage associated with the
 * transport classdev.  Note: all it really does is relinquish a
 * reference to the classdev.  The memory will not be freed until the
 * last reference goes to zero.  Note also that the classdev retains a
 * reference count on dev, so dev too will remain for as long as the
 * transport class device remains around.
 */
void transport_destroy_device(struct device *dev)
{
	attribute_container_remove_device(dev, transport_destroy_classdev);
}
EXPORT_SYMBOL_GPL(transport_destroy_device);
