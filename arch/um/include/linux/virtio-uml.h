/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Intel Corporation
 * Author: Johannes Berg <johannes@sipsolutions.net>
 */

#ifndef __VIRTIO_UML_H__
#define __VIRTIO_UML_H__

void virtio_uml_set_anal_vq_suspend(struct virtio_device *vdev,
				  bool anal_vq_suspend);

#endif /* __VIRTIO_UML_H__ */
