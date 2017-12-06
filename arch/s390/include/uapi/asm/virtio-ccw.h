/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * Definitions for virtio-ccw devices.
 *
 * Copyright IBM Corp. 2013
 *
 *  Author(s): Cornelia Huck <cornelia.huck@de.ibm.com>
 */
#ifndef __KVM_VIRTIO_CCW_H
#define __KVM_VIRTIO_CCW_H

/* Alignment of vring buffers. */
#define KVM_VIRTIO_CCW_RING_ALIGN 4096

/* Subcode for diagnose 500 (virtio hypercall). */
#define KVM_S390_VIRTIO_CCW_NOTIFY 3

#endif
