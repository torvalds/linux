#ifndef _QED_SELFTEST_API_H
#define _QED_SELFTEST_API_H
#include <linux/types.h>

/**
 * @brief qed_selftest_memory - Perform memory test
 *
 * @param cdev
 *
 * @return int
 */
int qed_selftest_memory(struct qed_dev *cdev);

/**
 * @brief qed_selftest_interrupt - Perform interrupt test
 *
 * @param cdev
 *
 * @return int
 */
int qed_selftest_interrupt(struct qed_dev *cdev);

/**
 * @brief qed_selftest_register - Perform register test
 *
 * @param cdev
 *
 * @return int
 */
int qed_selftest_register(struct qed_dev *cdev);

/**
 * @brief qed_selftest_clock - Perform clock test
 *
 * @param cdev
 *
 * @return int
 */
int qed_selftest_clock(struct qed_dev *cdev);

/**
 * @brief qed_selftest_nvram - Perform nvram test
 *
 * @param cdev
 *
 * @return int
 */
int qed_selftest_nvram(struct qed_dev *cdev);

#endif
