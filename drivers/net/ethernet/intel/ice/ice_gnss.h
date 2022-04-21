/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2018-2021, Intel Corporation. */

#ifndef _ICE_GNSS_H_
#define _ICE_GNSS_H_

#include <linux/tty.h>
#include <linux/tty_flip.h>

#define ICE_E810T_GNSS_I2C_BUS		0x2
#define ICE_GNSS_UBX_I2C_BUS_ADDR	0x42
/* Data length register is big endian */
#define ICE_GNSS_UBX_DATA_LEN_H		0xFD
#define ICE_GNSS_UBX_DATA_LEN_WIDTH	2
#define ICE_GNSS_UBX_EMPTY_DATA		0xFF
#define ICE_GNSS_TIMER_DELAY_TIME	(HZ / 10) /* 0.1 second per message */
#define ICE_MAX_I2C_DATA_SIZE		FIELD_MAX(ICE_AQC_I2C_DATA_SIZE_M)
#define ICE_MAX_UBX_READ_TRIES		255

/**
 * struct gnss_serial - data used to initialize GNSS TTY port
 * @back: back pointer to PF
 * @tty: pointer to the tty for this device
 * @open_count: number of times this port has been opened
 * @gnss_mutex: gnss_mutex used to protect GNSS serial operations
 * @kworker: kwork thread for handling periodic work
 * @read_work: read_work function for handling GNSS reads
 */
struct gnss_serial {
	struct ice_pf *back;
	struct tty_struct *tty;
	int open_count;
	struct mutex gnss_mutex; /* protects GNSS serial structure */
	struct kthread_worker *kworker;
	struct kthread_delayed_work read_work;
};

#if IS_ENABLED(CONFIG_TTY)
void ice_gnss_init(struct ice_pf *pf);
void ice_gnss_exit(struct ice_pf *pf);
bool ice_gnss_is_gps_present(struct ice_hw *hw);
#else
static inline void ice_gnss_init(struct ice_pf *pf) { }
static inline void ice_gnss_exit(struct ice_pf *pf) { }
static inline bool ice_gnss_is_gps_present(struct ice_hw *hw)
{
	return false;
}
#endif /* IS_ENABLED(CONFIG_TTY) */
#endif /* _ICE_GNSS_H_ */
