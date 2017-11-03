/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_VHOST_TEST_H
#define LINUX_VHOST_TEST_H

/* Start a given test on the virtio null device. 0 stops all tests. */
#define VHOST_TEST_RUN _IOW(VHOST_VIRTIO, 0x31, int)

#endif
