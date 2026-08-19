/* SPDX-License-Identifier: GPL-2.0-only OR BSD-2-Clause */
/*
 * Copyright (C) 2024 Canaan Bright Sight Co. Ltd
 * Copyright (C) 2024 Ze Huang <18771902331@163.com>
 */

#ifndef _K230_PINCTRL_H
#define _K230_PINCTRL_H

#define K230_MSC_3V3 0
#define K230_MSC_1V8 1

#define BANK_VOLTAGE_DEFAULT       K230_MSC_1V8
#define BANK_VOLTAGE_IO50_IO61     K230_MSC_3V3

#define K230_PINMUX(pin, mode) (((pin) << 8) | (mode))

#endif /* _K230_PINCTRL_H */
