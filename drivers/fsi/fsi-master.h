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

/*
 * Master registers
 *
 * These are used by hardware masters, such as the one in the FSP2, AST2600 and
 * the hub master in POWER processors.
 */

/* Control Registers */
#define FSI_MMODE		0x0		/* R/W: mode */
#define FSI_MDLYR		0x4		/* R/W: delay */
#define FSI_MCRSP		0x8		/* R/W: clock rate */
#define FSI_MENP0		0x10		/* R/W: enable */
#define FSI_MLEVP0		0x18		/* R: plug detect */
#define FSI_MSENP0		0x18		/* S: Set enable */
#define FSI_MCENP0		0x20		/* C: Clear enable */
#define FSI_MAEB		0x70		/* R: Error address */
#define FSI_MVER		0x74		/* R: master version/type */
#define FSI_MSTAP0		0xd0		/* R: Port status */
#define FSI_MRESP0		0xd0		/* W: Port reset */
#define FSI_MESRB0		0x1d0		/* R: Master error status */
#define FSI_MRESB0		0x1d0		/* W: Reset bridge */
#define FSI_MSCSB0		0x1d4		/* R: Master sub command stack */
#define FSI_MATRB0		0x1d8		/* R: Master address trace */
#define FSI_MDTRB0		0x1dc		/* R: Master data trace */
#define FSI_MECTRL		0x2e0		/* W: Error control */

/* MMODE: Mode control */
#define FSI_MMODE_EIP		0x80000000	/* Enable interrupt polling */
#define FSI_MMODE_ECRC		0x40000000	/* Enable error recovery */
#define FSI_MMODE_RELA		0x20000000	/* Enable relative address commands */
#define FSI_MMODE_EPC		0x10000000	/* Enable parity checking */
#define FSI_MMODE_P8_TO_LSB	0x00000010	/* Timeout value LSB */
						/*   MSB=1, LSB=0 is 0.8 ms */
						/*   MSB=0, LSB=1 is 0.9 ms */
#define FSI_MMODE_CRS0SHFT	18		/* Clk rate selection 0 shift */
#define FSI_MMODE_CRS0MASK	0x3ff		/* Clk rate selection 0 mask */
#define FSI_MMODE_CRS1SHFT	8		/* Clk rate selection 1 shift */
#define FSI_MMODE_CRS1MASK	0x3ff		/* Clk rate selection 1 mask */

/* MRESB: Reset brindge */
#define FSI_MRESB_RST_GEN	0x80000000	/* General reset */
#define FSI_MRESB_RST_ERR	0x40000000	/* Error Reset */

/* MRESP: Reset port */
#define FSI_MRESP_RST_ALL_MASTER 0x20000000	/* Reset all FSI masters */
#define FSI_MRESP_RST_ALL_LINK	0x10000000	/* Reset all FSI port contr. */
#define FSI_MRESP_RST_MCR	0x08000000	/* Reset FSI master reg. */
#define FSI_MRESP_RST_PYE	0x04000000	/* Reset FSI parity error */
#define FSI_MRESP_RST_ALL	0xfc000000	/* Reset any error */

/* MECTRL: Error control */
#define FSI_MECTRL_EOAE		0x8000		/* Enable machine check when */
						/* master 0 in error */
#define FSI_MECTRL_P8_AUTO_TERM	0x4000		/* Auto terminate */

#define FSI_HUB_LINK_OFFSET		0x80000
#define FSI_HUB_LINK_SIZE		0x80000
#define FSI_HUB_MASTER_MAX_LINKS	8

/*
 * Protocol definitions
 *
 * These are used by low level masters that bit-bang out the protocol
 */

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

/*
 * Structures and function prototypes
 *
 * These are common to all masters
 */

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
	int		(*link_enable)(struct fsi_master *, int link,
				       bool enable);
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
