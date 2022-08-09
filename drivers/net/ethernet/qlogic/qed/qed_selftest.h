/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-3-Clause) */
/* Copyright (c) 2019-2020 Marvell International Ltd. */

#ifndef _QED_SELFTEST_API_H
#define _QED_SELFTEST_API_H
#include <linux/types.h>

/**
 * qed_selftest_memory(): Perform memory test.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Int.
 */
int qed_selftest_memory(struct qed_dev *cdev);

/**
 * qed_selftest_interrupt(): Perform interrupt test.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Int.
 */
int qed_selftest_interrupt(struct qed_dev *cdev);

/**
 * qed_selftest_register(): Perform register test.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Int.
 */
int qed_selftest_register(struct qed_dev *cdev);

/**
 * qed_selftest_clock(): Perform clock test.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Int.
 */
int qed_selftest_clock(struct qed_dev *cdev);

/**
 * qed_selftest_nvram(): Perform nvram test.
 *
 * @cdev: Qed dev pointer.
 *
 * Return: Int.
 */
int qed_selftest_nvram(struct qed_dev *cdev);

#endif
