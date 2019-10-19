/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * FSI master definitions. These comprise the core <--> master interface,
 * to allow the core to interact with the (hardware-specific) masters.
 *
 * Copyright (C) IBM Corporation 2016
 */

#ifndef DRIVERS_FSI_MASTER_H
#define DRIVERS_FSI_MASTER_H

#include <linux/device.h>
#include <linux/mutex.h>

/* Various protocol delays */
#define	FSI_ECHO_DELAY_CLOCKS	16	/* Number clocks for echo delay */
#define	FSI_SEND_DELAY_CLOCKS	16	/* Number clocks for send delay */
#define	FSI_PRE_BREAK_CLOCKS	50	/* Number clocks to prep for break */
#define	FSI_BREAK_CLOCKS	256	/* Number of clocks to issue break */
#define	FSI_POST_BREAK_CLOCKS	16000	/* Number clocks to set up cfam */
#define	FSI_INIT_CLOCKS		5000	/* Clock out any old data */
#define	FSI_MASTER_DPOLL_CLOCKS	50      /* < 21 will cause slave to hang */
#define	FSI_MASTER_EPOLL_CLOCKS	50      /* Number of clocks for E_POLL retry */

/* Various retry maximums */
#define FSI_CRC_ERR_RETRIES	10
#define	FSI_MASTER_MAX_BUSY	200
#define	FSI_MASTER_MTOE_COUNT	1000

/* Command encodings */
#define	FSI_CMD_DPOLL		0x2
#define	FSI_CMD_EPOLL		0x3
#define	FSI_CMD_TERM		0x3f
#define FSI_CMD_ABS_AR		0x4
#define FSI_CMD_REL_AR		0x5
#define FSI_CMD_SAME_AR		0x3	/* but only a 2-bit opcode... */

/* Slave responses */
#define	FSI_RESP_ACK		0	/* Success */
#define	FSI_RESP_BUSY		1	/* Slave busy */
#define	FSI_RESP_ERRA		2	/* Any (misc) Error */
#define	FSI_RESP_ERRC		3	/* Slave reports master CRC error */

/* Misc */
#define	FSI_CRC_SIZE		4

/* fsi-master definition and flags */
#define FSI_MASTER_FLAG_SWCLOCK		0x1

struct fsi_master {
	struct device	dev;
	int		idx;
	int		n_links;
	int		flags;
	struct mutex	scan_lock;
	int		(*read)(struct fsi_master *, int link, uint8_t id,
				uint32_t addr, void *val, size_t size);
	int		(*write)(struct fsi_master *, int link, uint8_t id,
				uint32_t addr, const void *val, size_t size);
	int		(*term)(struct fsi_master *, int link, uint8_t id);
	int		(*send_break)(struct fsi_master *, int link);
	int		(*link_enable)(struct fsi_master *, int link);
	int		(*link_config)(struct fsi_master *, int link,
				       u8 t_send_delay, u8 t_echo_delay);
};

#define dev_to_fsi_master(d) container_of(d, struct fsi_master, dev)

/**
 * fsi_master registration & lifetime: the fsi_master_register() and
 * fsi_master_unregister() functions will take ownership of the master, and
 * ->dev in particular. The registration path performs a get_device(), which
 * takes the first reference on the device. Similarly, the unregistration path
 * performs a put_device(), which may well drop the last reference.
 *
 * This means that master implementations *may* need to hold their own
 * reference (via get_device()) on master->dev. In particular, if the device's
 * ->release callback frees the fsi_master, then fsi_master_unregister will
 * invoke this free if no other reference is held.
 *
 * The same applies for the error path of fsi_master_register; if the call
 * fails, dev->release will have been invoked.
 */
extern int fsi_master_register(struct fsi_master *master);
extern void fsi_master_unregister(struct fsi_master *master);

extern int fsi_master_rescan(struct fsi_master *master);

#endif /* DRIVERS_FSI_MASTER_H */
