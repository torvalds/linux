/* qmidevice.h - gobi QMI device header
 * Copyright (c) 2010, Code Aurora Forum. All rights reserved.

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
 * 02110-1301, USA.
 */

#ifndef QCUSBNET_QMIDEVICE_H
#define QCUSBNET_QMIDEVICE_H

#include "structs.h"
#include "qmi.h"

void qc_setdown(struct qcusbnet *dev, u8 reason);
void qc_cleardown(struct qcusbnet *dev, u8 reason);
bool qc_isdown(struct qcusbnet *dev, u8 reason);

int qc_startread(struct qcusbnet *dev);
void qc_stopread(struct qcusbnet *dev);

int qc_register(struct qcusbnet *dev);
void qc_deregister(struct qcusbnet *dev);

#endif /* !QCUSBNET_QMIDEVICE_H */
