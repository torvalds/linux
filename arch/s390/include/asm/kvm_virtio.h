/*
 * kvm_virtio.h - definition for virtio for kvm on s390
 *
 * Copyright IBM Corp. 2008
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 */

#ifndef __KVM_S390_VIRTIO_H
#define __KVM_S390_VIRTIO_H

#include <linux/types.h>

struct kvm_device_desc {
	/* The device type: console, network, disk etc.  Type 0 terminates. */
	__u8 type;
	/* The number of virtqueues (first in config array) */
	__u8 num_vq;
	/*
	 * The number of bytes of feature bits.  Multiply by 2: one for host
	 * features and one for guest acknowledgements.
	 */
	__u8 feature_len;
	/* The number of bytes of the config array after virtqueues. */
	__u8 config_len;
	/* A status byte, written by the Guest. */
	__u8 status;
	__u8 config[0];
};

/*
 * This is how we expect the device configuration field for a virtqueue
 * to be laid out in config space.
 */
struct kvm_vqconfig {
	/* The token returned with an interrupt. Set by the guest */
	__u64 token;
	/* The address of the virtio ring */
	__u64 address;
	/* The number of entries in the virtio_ring */
	__u16 num;

};

#define KVM_S390_VIRTIO_NOTIFY		0
#define KVM_S390_VIRTIO_RESET		1
#define KVM_S390_VIRTIO_SET_STATUS	2

/* The alignment to use between consumer and producer parts of vring.
 * This is pagesize for historical reasons. */
#define KVM_S390_VIRTIO_RING_ALIGN	4096

#endif
