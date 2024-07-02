/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Regulator event over netlink
 *
 * Author: Naresh Solanki <Naresh.Solanki@9elements.com>
 */

#ifndef __REGULATOR_EVENT_H
#define __REGULATOR_EVENT_H

int reg_generate_netlink_event(const char *reg_name, u64 event);

#endif
