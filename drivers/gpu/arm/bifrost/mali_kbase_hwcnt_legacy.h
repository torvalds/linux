/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2018, 2020-2021 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * Legacy hardware counter interface, giving userspace clients simple,
 * synchronous access to hardware counters.
 *
 * Any functions operating on an single legacy hardware counter client instance
 * must be externally synchronised.
 * Different clients may safely be used concurrently.
 */

#ifndef _KBASE_HWCNT_LEGACY_H_
#define _KBASE_HWCNT_LEGACY_H_

struct kbase_hwcnt_legacy_client;
struct kbase_ioctl_hwcnt_enable;
struct kbase_hwcnt_virtualizer;

/**
 * kbase_hwcnt_legacy_client_create() - Create a legacy hardware counter client.
 * @hvirt:     Non-NULL pointer to hardware counter virtualizer the client
 *             should be attached to.
 * @enable:    Non-NULL pointer to hwcnt_enable structure, containing a valid
 *             pointer to a user dump buffer large enough to hold a dump, and
 *             the counters that should be enabled.
 * @out_hlcli: Non-NULL pointer to where the pointer to the created client will
 *             be stored on success.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_legacy_client_create(
	struct kbase_hwcnt_virtualizer *hvirt,
	struct kbase_ioctl_hwcnt_enable *enable,
	struct kbase_hwcnt_legacy_client **out_hlcli);

/**
 * kbase_hwcnt_legacy_client_destroy() - Destroy a legacy hardware counter
 *                                       client.
 * @hlcli: Pointer to the legacy hardware counter client.
 *
 * Will safely destroy a client in any partial state of construction.
 */
void kbase_hwcnt_legacy_client_destroy(struct kbase_hwcnt_legacy_client *hlcli);

/**
 * kbase_hwcnt_legacy_client_dump() - Perform a hardware counter dump into the
 *                                    client's user buffer.
 * @hlcli: Non-NULL pointer to the legacy hardware counter client.
 *
 * This function will synchronously dump hardware counters into the user buffer
 * specified on client creation, with the counters specified on client creation.
 *
 * The counters are automatically cleared after each dump, such that the next
 * dump performed will return the counter values accumulated between the time of
 * this function call and the next dump.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_legacy_client_dump(struct kbase_hwcnt_legacy_client *hlcli);

/**
 * kbase_hwcnt_legacy_client_clear() - Perform and discard a hardware counter
 *                                     dump.
 * @hlcli: Non-NULL pointer to the legacy hardware counter client.
 *
 * This function will synchronously clear the hardware counters, such that the
 * next dump performed will return the counter values accumulated between the
 * time of this function call and the next dump.
 *
 * Return: 0 on success, else error code.
 */
int kbase_hwcnt_legacy_client_clear(struct kbase_hwcnt_legacy_client *hlcli);

#endif /* _KBASE_HWCNT_LEGACY_H_ */
